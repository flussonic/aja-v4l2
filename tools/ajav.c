// SPDX-License-Identifier: GPL-2.0
/*
 * ajav: a client of the ajv4l2 capture nodes by the SDI frame contract.
 *
 *   ajav info /dev/videoN            what the input carries, the timings set
 *   ajav cap /dev/videoN [options]   capture frames
 *     -n N        frames to take (default 10)
 *     -f v210     pixel format (uyvy, v210)
 *     -t NAME     force timings by name (1080i50, 1080p30, ...) instead of querying
 *     -u          USERPTR buffers instead of MMAP
 *     -a          print the first audio samples of every frame
 *     -A          print the ancillary packets of every frame
 *     -o DIR      write the planes of every frame into DIR
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include "sdi_av.h"
#include "ajav.h"

#define NBUF 4

struct plane_mem {
	void *p;
	size_t len;
};

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (r < 0 && errno == EINTR);
	return r;
}

static const char *fourcc(uint32_t f, char buf[5])
{
	buf[0] = f; buf[1] = f >> 8; buf[2] = f >> 16; buf[3] = f >> 24; buf[4] = 0;
	return buf;
}

static void print_timings(const struct v4l2_dv_timings *t)
{
	const struct v4l2_bt_timings *bt = &t->bt;
	uint64_t htot = V4L2_DV_BT_FRAME_WIDTH(bt), vtot = V4L2_DV_BT_FRAME_HEIGHT(bt);
	double fps = htot && vtot ? (double)bt->pixelclock / (htot * vtot) : 0;

	printf("%ux%u%s %.3f fps (%llu total lines)\n", bt->width, bt->height,
	       bt->interlaced ? "i" : "p", fps * (bt->interlaced ? 2 : 1), (unsigned long long)vtot);
}

static int cmd_info(const char *dev)
{
	struct v4l2_capability cap;
	struct v4l2_input inp = { .index = 0 };
	struct v4l2_dv_timings t;
	int fd = open(dev, O_RDWR);

	if (fd < 0) {
		perror(dev);
		return 1;
	}
	if (!xioctl(fd, VIDIOC_QUERYCAP, &cap))
		printf("%s: %s (%s)\n", cap.driver, cap.card, cap.bus_info);
	if (!xioctl(fd, VIDIOC_ENUMINPUT, &inp))
		printf("input %s: %s\n", inp.name,
		       inp.status & V4L2_IN_ST_NO_SIGNAL ? "no signal" :
		       inp.status & V4L2_IN_ST_NO_SYNC ? "no sync" : "signal");
	memset(&t, 0, sizeof(t));
	if (!xioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &t)) {
		printf("detected: ");
		print_timings(&t);
	} else {
		printf("detected: none (%s)\n", strerror(errno));
	}
	memset(&t, 0, sizeof(t));
	if (!xioctl(fd, VIDIOC_G_DV_TIMINGS, &t)) {
		printf("set:      ");
		print_timings(&t);
	}
	close(fd);
	return 0;
}

static bool timings_by_name(int fd, const char *name, struct v4l2_dv_timings *out)
{
	struct v4l2_enum_dv_timings e;

	for (e.index = 0; ; e.index++) {
		const struct v4l2_bt_timings *bt;
		uint64_t htot, vtot;
		double fps;
		char buf[32];

		memset(&e.reserved, 0, sizeof(e.reserved));
		if (xioctl(fd, VIDIOC_ENUM_DV_TIMINGS, &e))
			return false;
		bt = &e.timings.bt;
		htot = V4L2_DV_BT_FRAME_WIDTH(bt);
		vtot = V4L2_DV_BT_FRAME_HEIGHT(bt);
		fps = (double)bt->pixelclock / (htot * vtot) * (bt->interlaced ? 2 : 1);
		snprintf(buf, sizeof(buf), "%u%s%g", bt->height == 486 ? 525 : bt->height == 576 ? 625 : bt->height,
			 bt->interlaced ? "i" : "p", fps + 0.005 > (int)fps + 1 ? (double)((int)fps + 1) : ((int)(fps * 100 + 0.5)) / 100.0);
		if (!strcmp(buf, name)) {
			*out = e.timings;
			return true;
		}
	}
}

static void write_plane(const char *dir, unsigned int frame, unsigned int plane, const void *p, size_t len)
{
	static const char *names[SDI_NUM_PLANES] = { "video", "audio", "anc", "meta", "vbi" };
	char path[512];
	FILE *f;

	snprintf(path, sizeof(path), "%s/frame%04u.%s", dir, frame, names[plane]);
	f = fopen(path, "wb");
	if (!f)
		return;
	fwrite(p, 1, len, f);
	fclose(f);
}

static void print_anc(const uint8_t *p, size_t len)
{
	size_t off = 0;

	while (off + sizeof(struct sdi_anc_packet) <= len) {
		const struct sdi_anc_packet *pkt = (const void *)(p + off);
		size_t bytes = SDI_ANC_PACKET_BYTES(pkt->data_count);
		unsigned int i;

		if (off + bytes > len)
			break;
		printf("    line %u %s%s did %02x sdid %02x dc %u%s:", pkt->line,
		       pkt->flags & SDI_ANC_F_HANC ? "HANC" : "VANC",
		       pkt->flags & SDI_ANC_F_CHROMA ? " C" : " Y",
		       pkt->did, pkt->sdid, pkt->data_count,
		       pkt->flags & SDI_ANC_F_CS_ERROR ? " CS-ERROR" : "");
		for (i = 0; i < pkt->data_count && i < 16; i++)
			printf(" %03x", pkt->udw[i]);
		printf("%s\n", pkt->data_count > 16 ? " ..." : "");
		off += bytes;
	}
}

static int cmd_cap(int argc, char **argv)
{
	const char *dev = argv[0], *out_dir = NULL, *force = NULL;
	unsigned int count = 10, i, got = 0;
	uint32_t pixfmt = SDI_PIX_FMT_UYVY;
	bool userptr = false, show_audio = false, show_anc = false;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers req;
	struct plane_mem mem[NBUF][SDI_NUM_PLANES];
	struct v4l2_dv_timings t;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	int fd, opt;
	uint32_t last_seq = 0;
	bool have_seq = false;

	optind = 1;
	while ((opt = getopt(argc, argv, "n:f:t:uaAo:")) != -1) {
		switch (opt) {
		case 'n': count = atoi(optarg); break;
		case 'f': pixfmt = !strcmp(optarg, "v210") ? SDI_PIX_FMT_V210 : SDI_PIX_FMT_UYVY; break;
		case 't': force = optarg; break;
		case 'u': userptr = true; break;
		case 'a': show_audio = true; break;
		case 'A': show_anc = true; break;
		case 'o': out_dir = optarg; break;
		default: return 2;
		}
	}
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return 1;
	}
	memset(&t, 0, sizeof(t));
	if (force) {
		if (!timings_by_name(fd, force, &t)) {
			fprintf(stderr, "no such standard: %s\n", force);
			return 1;
		}
	} else if (xioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &t)) {
		perror("QUERY_DV_TIMINGS");
		return 1;
	}
	if (xioctl(fd, VIDIOC_S_DV_TIMINGS, &t)) {
		perror("S_DV_TIMINGS");
		return 1;
	}
	printf("timings: ");
	print_timings(&t);

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = type;
	if (xioctl(fd, VIDIOC_G_FMT, &fmt)) {
		perror("G_FMT");
		return 1;
	}
	fmt.fmt.pix_mp.pixelformat = pixfmt;
	if (xioctl(fd, VIDIOC_S_FMT, &fmt)) {
		perror("S_FMT");
		return 1;
	}
	{
		char f4[5];

		printf("format: %s %ux%u, planes:", fourcc(fmt.fmt.pix_mp.pixelformat, f4),
		       fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height);
		for (i = 0; i < fmt.fmt.pix_mp.num_planes; i++)
			printf(" %u", fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
		printf("\n");
	}

	memset(&req, 0, sizeof(req));
	req.count = NBUF;
	req.type = type;
	req.memory = userptr ? V4L2_MEMORY_USERPTR : V4L2_MEMORY_MMAP;
	if (xioctl(fd, VIDIOC_REQBUFS, &req)) {
		perror("REQBUFS");
		return 1;
	}
	for (i = 0; i < req.count; i++) {
		struct v4l2_buffer b;
		struct v4l2_plane planes[SDI_NUM_PLANES];
		unsigned int p;

		memset(&b, 0, sizeof(b));
		memset(planes, 0, sizeof(planes));
		b.type = type;
		b.memory = req.memory;
		b.index = i;
		b.m.planes = planes;
		b.length = SDI_NUM_PLANES;
		if (!userptr && xioctl(fd, VIDIOC_QUERYBUF, &b)) {
			perror("QUERYBUF");
			return 1;
		}
		for (p = 0; p < SDI_NUM_PLANES; p++) {
			if (userptr) {
				mem[i][p].len = fmt.fmt.pix_mp.plane_fmt[p].sizeimage;
				if (posix_memalign(&mem[i][p].p, 4096, mem[i][p].len))
					return 1;
				planes[p].m.userptr = (unsigned long)mem[i][p].p;
				planes[p].length = mem[i][p].len;
			} else {
				mem[i][p].len = planes[p].length;
				mem[i][p].p = mmap(NULL, planes[p].length, PROT_READ | PROT_WRITE, MAP_SHARED,
						   fd, planes[p].m.mem_offset);
				if (mem[i][p].p == MAP_FAILED) {
					perror("mmap");
					return 1;
				}
			}
		}
		if (xioctl(fd, VIDIOC_QBUF, &b)) {
			perror("QBUF");
			return 1;
		}
	}
	if (xioctl(fd, VIDIOC_STREAMON, &type)) {
		perror("STREAMON");
		return 1;
	}
	while (got < count) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		struct v4l2_buffer b;
		struct v4l2_plane planes[SDI_NUM_PLANES];
		const struct sdi_meta *m;
		int r = poll(&pfd, 1, 2000);

		if (r <= 0) {
			fprintf(stderr, "no frame within 2 s\n");
			break;
		}
		memset(&b, 0, sizeof(b));
		memset(planes, 0, sizeof(planes));
		b.type = type;
		b.memory = req.memory;
		b.m.planes = planes;
		b.length = SDI_NUM_PLANES;
		if (xioctl(fd, VIDIOC_DQBUF, &b)) {
			perror("DQBUF");
			break;
		}
		m = mem[b.index][SDI_PLANE_META].p;
		printf("frame %u seq %u%s ts %llu.%06llu video %u audio %u (%u samples) anc %u meta %u vbi %u",
		       got, b.sequence, have_seq && b.sequence != last_seq + 1 ? " GAP" : "",
		       (unsigned long long)b.timestamp.tv_sec, (unsigned long long)b.timestamp.tv_usec,
		       planes[0].bytesused, planes[1].bytesused, planes[1].bytesused / SDI_AUDIO_FRAME_BYTES,
		       planes[2].bytesused, planes[3].bytesused, planes[4].bytesused);
		if (b.flags & V4L2_BUF_FLAG_ERROR)
			printf(" ERROR");
		if (planes[3].bytesused >= SDI_META_SIZE && m->magic == SDI_META_MAGIC) {
			char f4[5];

			printf("\n  meta v%u flags 0x%x crc %u hw_ts %llu audio_present 0x%04x samples %u/%u rate %u nonpcm 0x%x vendor %s v%u %u bytes",
			       m->version, m->flags, m->crc_errors, (unsigned long long)m->hw_timestamp,
			       m->audio_present, m->audio_samples[0], m->audio_samples[1], m->audio_rate,
			       m->audio_nonpcm, fourcc(m->vendor_magic, f4), m->vendor_version, m->vendor_bytes);
			if (m->vendor_magic == AJAV_VENDOR_MAGIC && m->vendor_bytes >= sizeof(struct ajav_meta)) {
				const struct ajav_meta *v = (const void *)(m + 1);

				printf("\n  aja rp188 %08x %08x %08x rx 0x%08x vpid %08x/%08x dropped %u",
				       v->rp188_dbb, v->rp188_low, v->rp188_high, v->rx_status,
				       v->vpid_a, v->vpid_b, v->frames_dropped);
			}
		}
		printf("\n");
		have_seq = true;
		last_seq = b.sequence;
		if (show_audio) {
			const uint32_t *a = mem[b.index][SDI_PLANE_AUDIO].p;
			unsigned int n = planes[1].bytesused / 4, k;

			printf("  audio:");
			for (k = 0; k < 32 && k < n; k++)
				printf(" %08x", a[k]);
			printf("\n");
		}
		if (show_anc)
			print_anc(mem[b.index][SDI_PLANE_ANC].p, planes[2].bytesused);
		if (out_dir) {
			unsigned int p;

			for (p = 0; p < SDI_NUM_PLANES; p++)
				write_plane(out_dir, got, p, mem[b.index][p].p, planes[p].bytesused);
		}
		got++;
		if (xioctl(fd, VIDIOC_QBUF, &b)) {
			perror("QBUF");
			break;
		}
	}
	xioctl(fd, VIDIOC_STREAMOFF, &type);
	close(fd);
	printf("%u frames\n", got);
	return got == count ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc >= 3 && !strcmp(argv[1], "info"))
		return cmd_info(argv[2]);
	if (argc >= 3 && !strcmp(argv[1], "cap"))
		return cmd_cap(argc - 2, argv + 2);
	fprintf(stderr, "usage: ajav info /dev/videoN | ajav cap /dev/videoN [-n N] [-f uyvy|v210] [-t STD] [-u] [-a] [-A] [-o DIR]\n");
	return 2;
}
