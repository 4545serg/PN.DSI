#---------------------------------------------------------------------------------
# PROJECT NOISE DSi - Makefile (devkitARM + libnds + libfat)
#
#   make deps    fetch minimp3.h into source/ (once)
#   make         build project_noise_dsi.nds
#   make clean
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set. Install devkitPro (https://devkitpro.org) first.")
endif

# ds_rules must come first: it defines LIBNDS, which the include paths below need.
include $(DEVKITARM)/ds_rules

export TOPDIR ?= $(CURDIR)

TARGET   := project_noise_dsi
BUILD    := build
SOURCES  := source
INCLUDES := source
LIBDIRS  := $(LIBNDS) $(DEVKITPRO)/libfat

# Recursive (=) so these expand in the build subdirectory too.
INCLUDE  = $(foreach dir,$(INCLUDES),-I$(TOPDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include)
LIBPATHS = $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Current devkitARM/binutils no longer supports the legacy
# -mthumb-interwork linker emulation. Match the official devkitPro NDS
# ARM9 examples: ARMv5TE/ARM946E-S in Thumb mode.
ARCH     := -march=armv5te -mtune=arm946e-s -mthumb
CFLAGS   = -g -Wall -O2 -fomit-frame-pointer -ffast-math $(ARCH) -MMD -MP $(INCLUDE) -DARM9
CXXFLAGS = $(CFLAGS)
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(TARGET).map
LIBS     := -lfat -lnds9 -lm

#---------------------------------------------------------------------------------
# Top level: the real build runs inside ./build so object files stay out of the way.
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

.PHONY: $(BUILD) clean deps

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(TOPDIR)/Makefile

deps:
	@sh tools/get_deps.sh

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds $(TARGET).arm9 $(TARGET).map

else

export OUTPUT := $(TOPDIR)/$(TARGET)
export VPATH  := $(foreach dir,$(SOURCES),$(TOPDIR)/$(dir))

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.c)))
export OFILES := $(CFILES:.c=.o)

DEPENDS := $(OFILES:.o=.d)

.PHONY: all
all: $(OUTPUT).nds

$(OUTPUT).nds: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
