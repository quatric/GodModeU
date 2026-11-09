.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitPro")
endif

export DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
export WUT_ROOT ?= $(DEVKITPRO)/wut

include $(WUT_ROOT)/share/wut_rules

TARGET		:=	GodModeU
BUILD		:=	build
UNARR		:=	source/unarr source/unarr/common source/unarr/zip source/unarr/rar source/unarr/_7z source/unarr/lzmasdk
SOURCES		:=	source source/wiiu source/wiiufmt source/mobiclip source/zstd source/ajpg $(UNARR)
DATA		:=	res
# source/compat holds the few libogc header names the shared GodModeWii code includes, mapped onto
# Cafe OS (source/wiiu does the work behind them).
INCLUDES	:=	source source/compat source/wiiu source/wiiufmt source/mobiclip source/zstd source/ajpg $(UNARR)

APP_NAME		:=	GodModeU
APP_SHORTNAME	:=	GodModeU
APP_AUTHOR		:=	quatric & cmyksoda
APP_ICON		:=	apps/GodModeU/icon.png

# libwiiuinput: every controller, pen tablets / uDraw (drawing) and microphones (voice memos).
CFLAGS	= -g -O2 -Wall -Wno-unused-function $(MACHDEP) $(INCLUDE) -D__WIIU__ -DGMU -DHAVE_7Z -DHAVE_TABLET -DHAVE_MIC
CXXFLAGS	=	$(CFLAGS)
LDFLAGS	=	-g $(RPXSPECS) $(MACHDEP) -Wl,-Map,$(notdir $*.map)

# whb, coreinit, vpad, padscore, sndcore2, nsysnet, nn::ac, mcp and the FSA devoptab all live in libwut.
LIBS	:=	-linput-wiiu -lpng16 -ljpeg -lwebp -lsharpyuv -lvorbisidec -logg -lbz2 -lz -lwut -lm
LIBDIRS	:=	$(CURDIR)/libwiiuinput $(PORTLIBS) $(WUT_ROOT)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=
sFILES		:=
SFILES		:=
BINFILES	:=	gmsf2.bin sfx.bin pmic_firm.bin

export LD	:=	$(CC)
export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
					$(sFILES:.s=.o) $(SFILES:.S=.o)
export INCLUDE	:=	$(foreach dir,$(INCLUDES), -iquote $(CURDIR)/$(dir)) \
					-I$(CURDIR)/source/compat \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all libwiiuinput-lib
.DEFAULT_GOAL := all

all: $(BUILD)
	@$(MAKE) --no-print-directory $(TARGET).wuhb

$(BUILD): libwiiuinput-lib
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# The controller library, built with its default options (GameCube adapter, USB pads, tablets,
# microphones). The Wii Speak firmware is linked into GodModeU itself (res/pmic_firm.bin).
libwiiuinput-lib:
	@$(MAKE) --no-print-directory -C libwiiuinput PMIC_FIRM=none

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).rpx $(TARGET).wuhb $(TARGET).rpx.map $(TARGET).lst $(TARGET).map

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).rpx: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

# The video decoders run at -O3, as in wii-mobiclip-player.
MOBICLIP_O := $(patsubst %.c,%.o,$(notdir $(wildcard $(OUTPUT:$(TARGET)=)source/mobiclip/*.c)))
$(MOBICLIP_O): CFLAGS += -O3

midisynth.o: gmsf2_bin.h
sfx.o: sfx_bin.h
memo.o: pmic_firm_bin.h

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
