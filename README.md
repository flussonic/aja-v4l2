# aja-v4l2

Linux V4L2 driver for AJA SDI cards (KONA 5 and relatives). Every SDI
input of a card gets a multi-planar capture node (`/dev/videoN`) whose
buffers carry a whole frame: the picture, the frame's audio, the
ancillary data packets and per-frame metadata, laid out by the SDI frame
contract that every SDI driver of ours follows -- `include/sdi_av.h`,
the same file byte for byte in each of them and in the client. What the
card adds of its own (the RP 188 timecode, the receiver status, the
payload identifiers) is a vendor block declared in `include/ajav.h`.

Underneath is the kernel driver of AJA's open `libajantv2`
(https://github.com/aja-video/libajantv2, MIT): board bring-up, register
access, the DMA engines and the autocirculate frame ring are the vendor's
code, compiled into the same module under `vendor/` with the changes
listed in `vendor/PATCHES.md`. Our layer lives in `ajv4l2/`. Only
standard Linux interfaces face the user: the V4L2 nodes, a media device
per card (`/dev/mediaN`: model, serial, firmware date, which node is on
which connector), a hwmon device with the FPGA temperature and stream
counters in sysfs next to each node. There is no character device, no
`/proc` entry and no ioctl of the vendor's.

## Installing

The module ships as source for DKMS, so that every kernel the machine
boots gets its own build. On Debian and Ubuntu:

```sh
apt install ./ajv4l2-dkms_0.1.0_all.deb     # or from apt.flussonic.com
apt install ./ajv4l2-dev_0.1.0_all.deb      # /usr/include/sdi_av.h and ajav.h, for clients
```

The package builds the module for the installed kernels (it needs their
headers: `linux-headers-generic` on Ubuntu, `linux-headers-amd64` on
Debian) and keeps `ajantv2`, the other driver of these cards, from
autoloading, because whichever loads first owns them. If that module is
already loaded, unload it once (`rmmod ajantv2`, after stopping whatever
holds it) or reboot; after that `modprobe ajv4l2` and the PCI ids of the
cards bring the module up at boot.

## Building from the tree

```sh
make                 # build/ajv4l2.ko for the running kernel
make tools           # tools/ajav
make deb             # the two .deb (dpkg-buildpackage, debhelper, dh-dkms)
sudo make load       # loads the V4L2 dependencies and the module
tools/sync.sh        # copy the tree to the bench machine and build it there
```

`Dockerfile` is the build environment against a distribution kernel; the
CI builds the module against Debian 12, Debian 13, Ubuntu 24.04 and
26.04, builds the package on Ubuntu 24.04 and installs it into a clean
container, where DKMS has to build the module with nothing but the
package.

The module needs `videobuf2-dma-sg`: the frames go by DMA straight into
the buffers of the client, MMAP, USERPTR or DMABUF alike.

## Using the nodes

```sh
tools/ajav info /dev/video1                  # signal, detected and set timings
v4l2-ctl -d /dev/video1 --query-dv-timings
v4l2-ctl -d /dev/video1 --set-dv-bt-timings query
tools/ajav cap /dev/video1 -n 50 -a -A       # capture, print audio and ANC
tools/ajav cap /dev/video1 -f v210 -o /tmp/frames -n 3
tools/ajav cap /dev/video1 -t 1080i50 -n 5   # timings forced, no detection
tools/ajav cap /dev/video1 -u -n 5           # USERPTR buffers
v4l2-ctl -d /dev/video1 --log-status         # input, counters into dmesg
cat /sys/class/video4linux/video1/frames_skipped
media-ctl -p -d /dev/media0                  # which node is on which connector
```

Planes of every buffer (`include/sdi_av.h` has the details):

| plane | content |
|---|---|
| 0 | picture: `SDUY` (UYVY) or `SD10` (v210) |
| 1 | 16 channels x 32-bit samples, 48 kHz, interleaved, 24-bit sample in the top bits |
| 2 | `struct sdi_anc_packet` back to back: every non-audio packet the card's extractor found in the blanking, VANC and HANC |
| 3 | `struct sdi_meta` (magic `SDI0`, version 4, 128 bytes) followed by `struct ajav_meta` (`AJAV` in the vendor tail) |
| 4 | SD only: the luma of the vertical blanking lines; empty until the SD path is done |

The frame counter and its timestamp (CLOCK_MONOTONIC, derived from the
card's audio clock at the start of the frame) are in `v4l2_buffer`, the
geometry in `G_DV_TIMINGS`, the state of the input in `ENUMINPUT.status`
and `V4L2_EVENT_SOURCE_CHANGE`.

## State

Brought up on a KONA 5 (8K firmware) under Ubuntu 24.04 with kernel 6.14,
with a 1080p30 source on SDI 2: capture through MMAP and USERPTR, UYVY
and v210, hundreds of frames without a gap, `v4l2-compliance -d -m` on
every node and the media device without a failure or a warning. Not done
yet: the SD VBI plane, SD and 6G/12G capture on a live signal (the
standards are in the table, the paths are not verified), 3G level B, an
output node, the ancillary and audio planes against a source that
carries them.
