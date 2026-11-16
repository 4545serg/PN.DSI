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

include $(DEVKITARM)/ds_rules

TARGET   := project_noise_dsi
BUILD    := build
SOURCES  := source
INCLUDES := source
DATA     :=
GRAPHICS :=

ARCH     := -mthumb -mthumb-interwork
CFLAGS   := -g -Wall -O2 -march=armv5te -mtune=arm946e-s -fomit-frame-pointer -ffast-math $(ARCH) -MMD -MP
CFLAGS   += $(INCLUDE) -DARM9
CXXFLAGS := $(CFLAGS)
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(TARGET).map
LIBS     := -lfat -lnds9 -lm
LIBDIRS  := $(LIBNDS) $(DEVKITPRO)/libfat

#---------------------------------------------------------------------------------
# Top level: run the real build inside ./build so object files stay out of the way.
#---------------------------------------------------------------------------------
export TOPDIR ?= $(CURDIR)

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

export OUTPUT  := $(TOPDIR)/$(TARGET)
export VPATH   := $(foreach dir,$(SOURCES),$(TOPDIR)/$(dir))

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.c)))
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(TOPDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

DEPENDS := $(OFILES:.o=.d)

.PHONY: all
all: $(OUTPUT).nds

$(OUTPUT).nds: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
