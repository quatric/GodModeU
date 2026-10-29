.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitPro")
endif

export DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
export WUT_ROOT ?= $(DEVKITPRO)/wut

# wut_rules reads these when it is included, so they must come first.
APP_NAME		:=	GodModeU
APP_SHORTNAME	:=	GodModeU
APP_AUTHOR		:=	quatric
APP_ICON		:=	$(CURDIR)/data/icon.png
APP_TV_SPLASH	:=	$(CURDIR)/data/tv-splash.png
APP_DRC_SPLASH	:=	$(CURDIR)/data/drc-splash.png

include $(WUT_ROOT)/share/wut_rules

TARGET		:=	GodModeU
BUILD		:=	build
UNARR		:=	source/unarr source/unarr/common source/unarr/zip source/unarr/rar source/unarr/_7z source/unarr/lzmasdk
SOURCES		:=	source $(UNARR)
DATA		:=	data
INCLUDES	:=	source $(UNARR)

CFLAGS	= -g -O2 -Wall $(MACHDEP) $(INCLUDE) -DHAVE_7Z
CXXFLAGS	=	$(CFLAGS)
LDFLAGS	=	-g $(RPXSPECS) $(MACHDEP)

# whb (the helper library), coreinit, vpad, padscore, nsysnet, nn::ac and the
# FSA devoptab all live in libwut. libmocha (C++, so it needs libstdc++) comes from scripts/build_libmocha.sh,
# libdvm (FAT32 / exFAT USB drives) from scripts/build_libdvm.sh, libntfs (NTFS) from scripts/build_libntfs.sh.
LIBS	:=	-lntfs -ldvm -lmocha -lstdc++ -lwut -lm
LIBDIRS	:=	$(WUT_ROOT)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=
sFILES		:=
SFILES		:=
BINFILES	:=

export LD	:=	$(CC)
export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
					$(sFILES:.s=.o) $(SFILES:.S=.o)
export INCLUDE	:=	$(foreach dir,$(INCLUDES), -iquote $(CURDIR)/$(dir)) \
					-I$(CURDIR)/$(BUILD) \
					-I$(WUT_ROOT)/include \
					-I$(CURDIR)/portlibs/mocha/include \
					-I$(CURDIR)/portlibs/dvm/include \
					-I$(CURDIR)/portlibs/ntfs/include
export LIBPATHS	:=	-L$(WUT_ROOT)/lib -L$(CURDIR)/portlibs/mocha/lib -L$(CURDIR)/portlibs/dvm/lib \
					-L$(CURDIR)/portlibs/ntfs/lib
export LDFLAGS	:=	$(LDFLAGS)

.PHONY: $(BUILD) clean all
.DEFAULT_GOAL := all

all: $(BUILD)
	@$(MAKE) --no-print-directory $(TARGET).wuhb

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).rpx $(TARGET).wuhb $(TARGET).rpx.map $(TARGET).lst

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).rpx: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
