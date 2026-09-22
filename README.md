# aja-v4l2

Linux V4L2 driver for AJA SDI cards (KONA 5 and relatives). Every SDI
input of a card gets a multi-planar capture node (`/dev/videoN`) and
every SDI output a multi-planar output node; their buffers carry a whole
frame: the picture, the frame's audio, the ancillary data packets and
per-frame metadata, laid out by the SDI frame contract that every SDI
driver of ours follows -- `include/sdi_av.h`,
the same file byte for byte in each of them and in the client. The
vendor block declared in `include/ajav.h` carries only what no other
card has, the raw receiver status words, for diagnostics; the timecode
and the payload identifier are ancillary packets in plane 2 like on
every card, lost frames are gaps in `v4l2_buffer.sequence`.

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
apt install ./ajv4l2-dkms_26.09.0_all.deb     # or from apt.flussonic.com
apt install ./ajv4l2-dev_26.09.0_all.deb      # /usr/include/sdi_av.h and ajav.h, for clients
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

The capture nodes come first (`SDI in n`), then the output nodes (`SDI
out n`); `media-ctl -p` shows which is which. The connectors of a KONA 5
are bidirectional: the two nodes of a connector share its frame store
and stream one at a time -- while the output node streams, the capture
node reports no signal and refuses to stream, and the other way round.

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
tools/ajav play /dev/video6 -n 250           # bars, a tone, OP-47, SCTE-104, RP188 on SDI out 3
tools/ajav play /dev/video6 -t 1080p50 -n 250
tools/ajav play /dev/video6 -i /tmp/frames   # replay what cap -o wrote
```

The generator the other SDI drivers are tested with, `sdi_gen` of the
`sdi` crate of the streamer, plays through these nodes as it is.

Planes of every buffer (`include/sdi_av.h` has the details):

| plane | content |
|---|---|
| 0 | picture: `SDUY` (UYVY) or `SD10` (v210) |
| 1 | 16 channels x 32-bit samples, 48 kHz, interleaved, 24-bit sample in the top bits |
| 2 | `struct sdi_anc_packet` back to back: every non-audio packet the card's extractor found in the blanking, VANC and HANC; the payload identifier (DID 0x41) is put there from the receiver's register when the extractor did not deliver it |
| 3 | `struct sdi_meta` (magic `SDI0`, version 4, 128 bytes) followed by `struct ajav_meta` (`AJAV` in the vendor tail) |
| 4 | SD only: the luma of the vertical blanking lines; empty until the SD path is done |

The frame counter and its timestamp (CLOCK_MONOTONIC, derived from the
card's audio clock at the start of the frame) are in `v4l2_buffer`, the
geometry in `G_DV_TIMINGS`, the state of the input in `ENUMINPUT.status`
and `V4L2_EVENT_SOURCE_CHANGE`.

An output buffer is one frame to play: the picture (the whole plane),
the audio to embed (any number of samples, none for silence) and the
packets to insert on the given lines, the field 2 packets by their line
numbers. The transmitter places the audio groups and the payload
identifier itself; a payload identifier in plane 2 replaces the one
derived from the standard, audio packets in plane 2 are dropped and
counted. The list of packets ends at `bytesused` or at an all-zero
header. The card holds a ring of eight frames: a buffer comes back once
its frame has been on air and the next one has replaced it, so up to
seven are held; with nothing queued the last frame stays on air and
`frames_skipped` counts the repeats, and `v4l2_buffer.sequence` counts
frames that went on the wire before this one with the repeats included,
so a gap of n in it is n frames the card sent again. Plane 4 is ignored
until the SD path is done.

Plane 3 is read. `audio_samples` tells the frame's own audio from the
padding the client left in plane 1, which the payload size cannot: on the
59.94 cadence, where the frames are of unequal length, embedding the
padding walks the embedder away from the picture by a sample a frame. Of
`flags` the output reads the colour -- `SDI_F_REC2020`, `SDI_F_HLG` and
`SDI_F_PQ` set the colorimetry and the transfer characteristic of the
payload identifier for the frames from that one on, through the core's
own override registers, so nothing else in the identifier changes. Colour
is stated as a whole or not at all: a frame setting none of the three
leaves the identifier saying whatever the standard implies, which is what
every client got before the flags existed. `SDI_F_LEVEL_B` is **not**
read -- the 3G mapping is the `level_a` setting of the node, and the
card's converter cannot be turned between frames.

The output's standard, link rate (1.5G, 3G, 6G/12G) and payload
identifier follow the timings set with `S_DV_TIMINGS`. Next to the
counters, the output node carries in sysfs the settings that have no
V4L2 control, each read back as written and applied at STREAMON:

| file | values | meaning |
|---|---|---|
| `timing` | `reference` (default), `internal` | `reference`: the outputs of the card lock to its reference input while a signal is present there and free-run otherwise; `internal`: the card's own clock. One setting per card |
| `level_a` | 1 (default), 0 | 3G standards (1080p50/60) as SMPTE 425 level A, or mapped to level B by the output's converter |
| `idle` | `repeat` | what plays when nothing is queued: the last frame again |
| `reference` | read-only | genlock in one word of the shared dictionary: `no_reference` when the reference input carries nothing, `unlocked` when `timing` is `internal` (then the output is certainly not following it), `unknown` otherwise -- the card says a signal is there and not whether it locked to it |

Two limits of the card: its free-running frame pulse has one rate, that
of the output that started last, so outputs of the two rate families
(25/50 and 24/30/60) cannot free-run together -- lock them to a
reference; and the ancillary extractor and inserter of channels 1-4
share the frame size of channel 1, so a 2160p stream and an HD stream
cannot run on two connectors of the same group at once (the second
STREAMON fails with EBUSY).

## State

Brought up on a KONA 5 (8K firmware) under Ubuntu 24.04 with kernel 6.14,
with a 1080p30 source on SDI 2: capture through MMAP and USERPTR, UYVY
and v210, hundreds of frames without a gap, `v4l2-compliance -d -m` on
every node and the media device without a failure or a warning. Against
a DekTec output looped into SDI 1: 1080i50, 2160p25 over 6G and 2160p50
over 12G with 16 channels of audio, the payload identifier, SCTE-104,
OP-47 and RP188 packets, all byte for byte, with no CRC errors and no
gaps. The output nodes were read back by the same DekTec input on the
same three standards: picture without 2SI artefacts, 16 channels of
audio, every ANC packet in every frame, the payload identifier of each
link rate and the ATC timecode. At 2160p50 the output's kernel thread
takes 0.5% of a core: video goes by DMA straight from the buffer, the
two-sample interleave and the v210 packing are the FPGA's, and only the
audio and ANC pass through a bounce buffer. Not done yet: the SD VBI
plane, SD on a live signal, 3G level B on the input, `idle=black`.

## Counters

The names next to a node, their meaning on each side and the rule for a
counter the card cannot report are the contract in `docs/sdi-sysfs.md`,
shared with our other SDI drivers. A capture node carries `frames`,
`frames_skipped`, `no_buffer`, `crc_errors`, `dma_errors` and `signal`;
an output node `frames`, `frames_skipped`, `dma_errors`, `anc_dropped`,
`signal` and `reference`, next to its settings.

What is missing is missing on purpose. This card reports no resync, no
loss of sync, no missed interrupt and no restart, and its playout side
reports neither an empty queue nor a line CRC nor a dropped sample; those
files used to exist and read `0` forever. A permanent zero in a
monitoring system is not a missing measurement, it is a perfect result --
no lost frames, no errors, ever. A reader now gets `ENOENT` and knows it
has nothing to go on.

## Licensing

Our code -- the V4L2 layer in `ajv4l2/`, the vendor block header
`include/ajav.h`, the client in `tools/` and the build files -- is dual
licensed, MIT or GPL-2.0 at your option, and the module declares
`MODULE_LICENSE("Dual MIT/GPL")`: the string the kernel recognises for
that choice, which is what lets the module use the GPL-only exports of
the V4L2, videobuf2, media controller and hwmon cores. `include/sdi_av.h`
carries the uapi syscall note, so a program of any license may include
it. The sources under `vendor/` stay under AJA's own MIT license. See
`LICENSING.md`.
