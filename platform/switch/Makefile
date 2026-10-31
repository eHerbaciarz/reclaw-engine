# Nintendo Switch (homebrew, libnx) build of the merged engine.
#
#   export DEVKITPRO=/opt/devkitpro
#   (dkp-)pacman -S switch-dev switch-sdl2 switch-sdl2_mixer switch-sdl2_image \
#                   switch-sdl2_ttf switch-sdl2_gfx switch-zlib
#   make -C platform/switch -j$(nproc)
#
# Output: platform/switch/reclaw.nro
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
TARGET      := reclaw
APP_TITLE   := ReClaw
APP_AUTHOR  := OpenClaw, claw-web and ClawSwitch contributors
APP_VERSION := 0.1
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
C_SOURCES := ThirdParty/libsmacker/smacker.c
OBJECTS := $(addprefix $(BUILD)/obj/,$(SOURCES:.cpp=.o)) $(addprefix $(BUILD)/obj/,$(C_SOURCES:.c=.o))
DEPENDS := $(OBJECTS:.o=.d)

#---------------------------------------------------------------------------------
# Flags
#---------------------------------------------------------------------------------
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# ThirdParty/ also bundles SDL 2.0.4 headers for the old Windows build. It is
# searched last (-idirafter) so <SDL2/...> always resolves to the SDL that is
# actually linked (portlibs), while FastDelegate/Tinyxml are still found.
INCLUDES := -I$(PORTLIBS)/include -I$(PORTLIBS)/include/SDL2 -I$(LIBNX)/include \
            -I$(REPO)/OpenClaw -I$(REPO)/Box2D -I$(REPO)/libwap \
            -I$(REPO)/ThirdParty/Tinyxml -idirafter $(REPO)/ThirdParty

DEFINES  := -D__SWITCH__ -DOPENCLAW_NATIVE=1
ifeq ($(TRACE),1)
# Diagnostic build: timestamped startup trace in /switch/reclaw/save/reclaw.log
DEFINES  += -DOPENCLAW_STARTUP_TRACE
endif

ifneq ($(RECLAW_FFMPEG),)
DEFINES  += -DOPENCLAW_HAVE_FFMPEG=1
INCLUDES += -I$(RECLAW_FFMPEG)/include
endif

CXXFLAGS := -g -O2 -ffunction-sections -fdata-sections $(ARCH) $(DEFINES) $(INCLUDES) \
            -std=gnu++17 -fpermissive -fexceptions -Wno-narrowing -w -MMD -MP

LDFLAGS  := $(SPECS) -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map

LIBS     := $(shell $(PKGCONFIG) --libs SDL2_mixer SDL2_image SDL2_ttf SDL2_gfx sdl2 zlib 2>/dev/null) -lnx

# Remastered movies: movie_XX.mp4 needs a minimal FFmpeg
# (scripts/build_ffmpeg_min.sh switch <prefix>; make RECLAW_FFMPEG=<prefix>).
# Without it only movie_XX.mpg (MPEG-1) plays.
ifneq ($(RECLAW_FFMPEG),)
LIBS     := $(RECLAW_FFMPEG)/lib/libavformat.a $(RECLAW_FFMPEG)/lib/libavcodec.a $(RECLAW_FFMPEG)/lib/libavutil.a $(LIBS)
endif

#---------------------------------------------------------------------------------
# Bundled read-only data (romfs). CLAW.REZ is NOT bundled: players copy their
# own file to /switch/reclaw/data/CLAW.REZ on the SD card.
#---------------------------------------------------------------------------------
ROMFS_FILES := $(ROMFS)/config.xml $(ROMFS)/ASSETS.ZIP $(ROMFS)/clacon.ttf \
               $(ROMFS)/console02.tga $(ROMFS)/music/soundfont.sf3 $(ROMFS)/splash.ttf $(ROMFS)/subtitles.ttf

ASSETS_SRC := $(shell cd $(REPO)/Build_Release && find ASSETS -type f 2>/dev/null)

.PHONY: all clean romfs
all: $(TARGET).nro

$(BUILD)/obj/%.o: $(REPO)/%.cpp
	@mkdir -p $(dir $@)
	@echo $*.cpp
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: $(REPO)/%.c
	@mkdir -p $(dir $@)
	@echo $*.c
	@$(CC) -g -O2 -ffunction-sections -fdata-sections $(ARCH) $(DEFINES) -w -MMD -MP -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJECTS)
	@echo linking $(notdir $@)
	@$(CXX) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@

romfs: $(ROMFS_FILES)

# Optional dedication screen while loading: make SPLASH="Text-Title"
# Only StartupSplash.cpp sees the text; a stamp file rebuilds it when it changes.
SPLASH_OBJ   := $(BUILD)/obj/OpenClaw/Engine/GameApp/StartupSplash.o
SPLASH_STAMP := $(BUILD)/splash.stamp
ifneq ($(strip $(SPLASH)),)
$(SPLASH_OBJ): CXXFLAGS += -DOPENCLAW_SPLASH_TEXT='"$(SPLASH)"'
endif
$(SPLASH_OBJ): $(SPLASH_STAMP)
$(SPLASH_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(SPLASH)' | cmp -s - $@ || echo '$(SPLASH)' > $@
.PHONY: FORCE
FORCE:

$(ROMFS)/splash.ttf: $(REPO)/ThirdParty/Fonts/CinzelDecorative/CinzelDecorative-Bold.ttf
	@mkdir -p $(dir $@)
	@cp $< $@

$(ROMFS)/subtitles.ttf: $(REPO)/Box2D/Data/DroidSans.ttf
	@mkdir -p $(dir $@)
	@cp $< $@

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
