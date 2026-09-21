# Changes to the vendor sources

The sources under `libajantv2/` are the Linux kernel driver of AJA's
libajantv2 at commit 65bac4b (SDK 18.1, MIT, `LICENSE`); the first commit
of the repository imports them untouched, so everything that differs from
it is ours, and every change is listed here. `git diff <first commit> --
vendor/` must agree with this list. Every change is under `#if
defined(AJV4L2)` (set by `Kbuild`), so the files still build as the
vendor's own module without it.

## Build

- `driver/linux/ntv2devicefeatures.c`, `ntv2driverprocamp.c`,
  `ntv2vpidfromspec.c` -- one-line wrappers including the SDK `.cpp` of
  the same name; the vendor Makefile makes them as symlinks at build time,
  kbuild needs real files.

## No second interface

- `driver/linux/ntv2driver.c` -- the PCI driver is named `ajv4l2`
  (`AJV4L2_DRIVER_NAME` from `ajv4l2_hook.h`); the character device, its
  class and the `/proc/driver/aja` entry are not registered (no
  `/dev/ajantv2*`, no ioctls), nor is the UART driver (the cards with a
  serial port are not ours); `MODULE_DEVICE_TABLE` is emitted so that the
  PCI ids autoload the module; the vendor `MODULE_AUTHOR`/`MODULE_LICENSE`
  are left out, the module declares its own.

## V4L2 layer hook

- `driver/linux/ntv2driver.c` -- `ajv4l2_attach(deviceNumber)` is called
  at the end of `probe`, once the core has brought the card up (interrupts,
  DMA engines and monitors enabled); `ajv4l2_detach(deviceNumber)` at the
  start of `remove`.

## Kernel-owned DMA buffers

- `driver/linux/ntv2dma.c`, `ntv2dma.h` -- `dmaPageRootAddSg` registers a
  buffer whose pages are already pinned and mapped for the device (a
  videobuf2 plane, or a kernel bounce buffer) under a cookie address; the
  entries with a DMA length are copied into the flat array the descriptor
  builders index, and the buffer is marked `sgExternal`, so that
  `dmaPageUnlock` neither unmaps it nor releases pages. `dmaTransfer` finds
  it by the cookie like any locked buffer and never calls
  `get_user_pages`, which the capture thread, having no mm, could not.
