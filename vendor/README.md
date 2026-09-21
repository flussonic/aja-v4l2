# Vendor sources

This directory holds the Linux kernel driver of AJA's `libajantv2`
(https://github.com/aja-video/libajantv2, MIT licence, `LICENSE`),
taken from commit `65bac4b763fc0e835d7460716e2c9cc1069e5a31` (SDK 18.1,
2026-09-12). The set of files is exactly what the vendor's own DKMS
package carries (`driver/linux/Makefile`, target `dkms-pkg`), laid out
the same way:

- `libajantv2/driver/` -- the platform-independent part of the driver
  (register access, crosspoints, ancillary data extractors, audio, VPID,
  HDMI, genlock, setup monitors);
- `libajantv2/driver/linux/` -- the Linux part (PCI probe, interrupts,
  DMA engines, autocirculate, register I/O);
- `libajantv2/ajantv2/includes/`, `libajantv2/ajantv2/src/` -- the SDK
  headers and the three sources the driver compiles as C.

`ntv2version.h` is the file the vendor's build generates from
`ntv2version.h.in`.

The first commit of the repository imports these files untouched; every
change made to them afterwards is listed in `PATCHES.md`, and
`git diff <first commit> -- vendor/` must agree with that list.
