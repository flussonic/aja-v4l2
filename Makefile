# Build of the ajv4l2 module: a V4L2 driver for AJA SDI cards (KONA 5 and
# relatives). The driver core from AJA's libajantv2 (vendor/, MIT) is
# compiled into the same module as our V4L2 layer (ajv4l2/).
#
#   make            - build/ajv4l2.ko for the running kernel
#   make KDIR=...   - against another kernel tree
#   make load       - reload the module on this machine (root)
#   make deb        - the DKMS package (debian/), needs dpkg-dev and debhelper
#
# AJV4L2_VERSION is the driver version: the module's MODULE_VERSION, the
# PACKAGE_VERSION in dkms.conf and the version in debian/changelog; the
# package build refuses to proceed when the three disagree. The object list
# and the compiler flags are in Kbuild.

MODULE         := ajv4l2
AJV4L2_VERSION := 0.1.0
export AJV4L2_VERSION
KVER  ?= $(shell uname -r)
KDIR  ?= /lib/modules/$(KVER)/build
BUILD := build
PREFIX ?= /usr/local

.PHONY: all clean load unload install-header tools version-check deb

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
	@mkdir -p $(BUILD) && cp $(MODULE).ko $(BUILD)/

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -rf $(BUILD)
	$(MAKE) -C tools clean

tools:
	$(MAKE) -C tools

# The headers a client program needs.
install-header:
	install -D -m 0644 include/sdi_av.h $(DESTDIR)$(PREFIX)/include/sdi_av.h
	install -D -m 0644 include/ajav.h $(DESTDIR)$(PREFIX)/include/ajav.h

deb: version-check
	dpkg-buildpackage -us -uc -b
	@mkdir -p $(BUILD) && mv ../ajv4l2-dkms_$(AJV4L2_VERSION)_all.deb ../ajv4l2-dev_$(AJV4L2_VERSION)_all.deb $(BUILD)/
	@rm -f ../ajv4l2_$(AJV4L2_VERSION)_*.buildinfo ../ajv4l2_$(AJV4L2_VERSION)_*.changes
	@ls -la $(BUILD)/*.deb

version-check:
	@dkms=$$(sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' dkms.conf); \
	 deb=$$(dpkg-parsechangelog -l debian/changelog -S Version 2>/dev/null); \
	 if [ "$$dkms" != "$(AJV4L2_VERSION)" ] || [ "$$deb" != "$(AJV4L2_VERSION)" ]; then \
	     echo "version mismatch: Makefile $(AJV4L2_VERSION), dkms.conf $$dkms, debian/changelog $$deb"; \
	     exit 1; \
	 fi

unload:
	-rmmod $(MODULE) 2>/dev/null

load: unload
	modprobe videodev
	modprobe videobuf2-v4l2
	modprobe videobuf2-dma-sg
	modprobe v4l2-dv-timings
	insmod $(BUILD)/$(MODULE).ko
