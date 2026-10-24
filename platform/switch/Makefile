# Nintendo Switch (homebrew, libnx) build of the merged engine.
#
#   export DEVKITPRO=/opt/devkitpro
#   (dkp-)pacman -S switch-dev switch-sdl2 switch-sdl2_mixer switch-sdl2_image \
#                   switch-sdl2_ttf switch-sdl2_gfx switch-zlib
#   make -C platform/switch -j$(nproc)
#
# Output: platform/switch/openclaw.nro
#
# Unlike devkitPro's template Makefile this keeps object files in a directory
# tree mirroring the sources, so files with the same name in different folders
# (libwap/Util.cpp vs Engine/Util/Util.cpp) do not collide.
#
# The source selection must stay in sync with platform/linux/CMakeLists.txt,
# which builds the very same code on a desktop for quick testing.

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

#---------------------------------------------------------------------------------
# Application
#---------------------------------------------------------------------------------
TARGET      := openclaw
APP_TITLE   := Captain Claw
APP_AUTHOR  := OpenClaw, claw-web and ClawSwitch contributors
APP_VERSION := 1.0.0
ICON        := icon.jpg

#---------------------------------------------------------------------------------
# Toolchain (overridable, e.g. to syntax-check the Switch code paths on a PC)
#---------------------------------------------------------------------------------
PREFIX      ?= $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
CC          := $(PREFIX)gcc
CXX         := $(PREFIX)g++
PORTLIBS    ?= $(DEVKITPRO)/portlibs/switch
LIBNX       ?= $(DEVKITPRO)/libnx
PKGCONFIG   ?= $(PORTLIBS)/bin/aarch64-none-elf-pkg-config
TOOLS       ?= $(DEVKITPRO)/tools/bin
SPECS       ?= -specs=$(LIBNX)/switch.specs

REPO        := $(abspath $(CURDIR)/../..)
BUILD       := build
ROMFS       := $(BUILD)/romfs

#---------------------------------------------------------------------------------
# Sources (keep in sync with platform/linux/CMakeLists.txt)
#---------------------------------------------------------------------------------
SOURCE_ROOTS := OpenClaw libwap Box2D/Box2D ThirdParty/Tinyxml
EXCLUDE_RE   := /WASM/|^OpenClaw/Engine/Graphics/[^/]+\.cpp$$|^OpenClaw/Engine/Graphics/(Generic|WebGL|WebGPU)/|/Audio/WebAudioAPI\.cpp$$|^libwap/Miniz\.cpp$$|/Tinyxml/xmltest\.cpp$$

SOURCES := $(shell cd $(REPO) && find $(SOURCE_ROOTS) -name '*.cpp' | LC_ALL=C sort | grep -v -E '$(EXCLUDE_RE)')
OBJECTS := $(addprefix $(BUILD)/obj/,$(SOURCES:.cpp=.o))
DEPENDS := $(OBJECTS:.o=.d)

#---------------------------------------------------------------------------------
# Flags
#---------------------------------------------------------------------------------
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

INCLUDES := -I$(REPO)/OpenClaw -I$(REPO)/Box2D -I$(REPO)/libwap \
            -I$(REPO)/ThirdParty -I$(REPO)/ThirdParty/Tinyxml \
            -I$(PORTLIBS)/include -I$(PORTLIBS)/include/SDL2 -I$(LIBNX)/include

DEFINES  := -D__SWITCH__ -DOPENCLAW_NATIVE=1

CXXFLAGS := -g -O2 -ffunction-sections -fdata-sections $(ARCH) $(DEFINES) $(INCLUDES) \
            -std=gnu++17 -fpermissive -fexceptions -Wno-narrowing -w -MMD -MP

LDFLAGS  := $(SPECS) -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map

LIBS     := $(shell $(PKGCONFIG) --libs SDL2_mixer SDL2_image SDL2_ttf SDL2_gfx sdl2 zlib 2>/dev/null) -lnx

#---------------------------------------------------------------------------------
# Bundled read-only data (romfs). CLAW.REZ is NOT bundled: players copy their
# own file to /switch/claw/CLAW.REZ on the SD card.
#---------------------------------------------------------------------------------
ROMFS_FILES := $(ROMFS)/config.xml $(ROMFS)/ASSETS.ZIP $(ROMFS)/clacon.ttf \
               $(ROMFS)/console02.tga $(ROMFS)/music/soundfont.sf3

ASSETS_SRC := $(shell cd $(REPO)/Build_Release && find ASSETS -type f 2>/dev/null)

.PHONY: all clean romfs
all: $(TARGET).nro

$(BUILD)/obj/%.o: $(REPO)/%.cpp
	@mkdir -p $(dir $@)
	@echo $*.cpp
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJECTS)
	@echo linking $(notdir $@)
	@$(CXX) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@

romfs: $(ROMFS_FILES)

$(ROMFS)/config.xml: config.xml
	@mkdir -p $(dir $@)
	@cp $< $@

$(ROMFS)/ASSETS.ZIP: $(addprefix $(REPO)/Build_Release/,$(ASSETS_SRC))
	@mkdir -p $(dir $@)
	@rm -f $@
	@echo packing ASSETS.ZIP
	@cd $(REPO)/Build_Release/ASSETS && \
	  if command -v zip >/dev/null 2>&1; then zip -q -r -X $(abspath $@) . ; \
	  else python3 -c "import shutil,sys; shutil.make_archive(sys.argv[1][:-4], 'zip', '.')" $(abspath $@) && \
	       mv $(abspath $(basename $@)).zip $(abspath $@); fi

$(ROMFS)/clacon.ttf $(ROMFS)/console02.tga: $(ROMFS)/%: $(REPO)/Build_Release/%
	@mkdir -p $(dir $@)
	@cp $< $@

$(ROMFS)/music/soundfont.sf3: $(REPO)/Build_Release/music/soundfont.sf3
	@mkdir -p $(dir $@)
	@cp $< $@

$(BUILD)/$(TARGET).nacp: Makefile
	@mkdir -p $(BUILD)
	@$(TOOLS)/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).nacp $(ROMFS_FILES) $(ICON)
	@$(TOOLS)/elf2nro $(BUILD)/$(TARGET).elf $@ --icon=$(ICON) --nacp=$(BUILD)/$(TARGET).nacp --romfsdir=$(ROMFS)
	@echo built ... $@

clean:
	@rm -rf $(BUILD) $(TARGET).nro

-include $(DEPENDS)
