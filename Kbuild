# kbuild description of the module: the object list and the flags. The
# Makefile next to it drives kbuild and the bench targets, so that a
# top-level make can carry KERNELRELEASE (DKMS does) without being taken
# for the kbuild pass.
MODULE          := ajv4l2
AJV4L2_VERSION  ?= dev
V               := vendor/libajantv2
VDRV            := $(V)/driver
VLIN            := $(V)/driver/linux
VINC            := $(V)/ajantv2/includes
VSRC            := $(V)/ajantv2/src
ARCH_DEFINE     := $(if $(filter arm64,$(ARCH)),aarch64,x86_64)

ccflags-y += -I$(src)/$(VINC) -I$(src)/$(VLIN) -I$(src)/$(VSRC) -I$(src)/$(VSRC)/lin -I$(src)/$(VDRV)
ccflags-y += -I$(src)/ajv4l2 -I$(src)/include
# The vendor build's defines: platform, target name, machine, SDK version,
# and the distribution fields its buildenv.h keys RHEL backports on.
ccflags-y += -DAJALinux -DXENA2 -Dajantv2 -D$(ARCH_DEFINE)
ccflags-y += -DSDKVER_MAJ=18 -DSDKVER_MIN=1 -DSDKVER_PNT=0 -DSDKVER_BLD=0 -DAJA_BETA=0 -DAJA_DEBUG=0
ccflags-y += -DDISTRO_TYPE=generic -DDISTRO_IS_RHEL_LIKE=0 -DDISTRO_MAJ_VERSION=0 -DDISTRO_MIN_VERSION=0
ccflags-y += -DDISTRO_KERNEL_PKG_MAJ=0 -DDISTRO_KERNEL_PKG_MIN=0 -DDISTRO_KERNEL_PKG_PNT=0
ccflags-y += -DAJV4L2 -DAJV4L2_VERSION=\"$(AJV4L2_VERSION)\"
# The vendor code predates these warnings; they are not ours to fix.
ccflags-y += -Wall -Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-unused-variable \
             -Wno-missing-prototypes -Wno-missing-declarations -Wno-declaration-after-statement

VENDOR_OBJS := ntv2anc ntv2aux ntv2commonreg ntv2displayid ntv2genlock2 ntv2genlock \
               ntv2hdmiedid ntv2hdmiin4 ntv2hdmiin ntv2hdmiout4 ntv2infoframe ntv2kona \
               ntv2mailbox ntv2mcap ntv2pciconfig ntv2rp188 ntv2setup ntv2stream ntv2system \
               ntv2video ntv2videoraster ntv2vpid ntv2xpt
VENDOR_LIN_OBJS := ntv2dma ntv2driverautocirculate ntv2driver ntv2driverdbgmsgctl \
                   ntv2driverstatus ntv2drivertask ntv2kona2 ntv2serial registerio \
                   ntv2devicefeatures ntv2driverprocamp ntv2vpidfromspec
OUR_OBJS := ajv4l2_module ajv4l2_modes ajv4l2_input ajv4l2_video ajv4l2_hw ajv4l2_capture

obj-m       := $(MODULE).o
$(MODULE)-y := $(addsuffix .o,$(addprefix $(VDRV)/,$(VENDOR_OBJS))) \
               $(addsuffix .o,$(addprefix $(VLIN)/,$(VENDOR_LIN_OBJS))) \
               $(addsuffix .o,$(addprefix ajv4l2/,$(OUR_OBJS)))
