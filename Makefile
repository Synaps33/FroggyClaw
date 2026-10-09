# Captain Claw libretro core - Data Frog SF2000 / GB300.
#
#   make SF2000_PLATFORM=native   -> openclaw_libretro.so       (x86, test_runner)
#   make SF2000_PLATFORM=sf2000   -> openclaw_libretro_sf2000.a (MIPS32r2)
#
# The engine normally links against real SDL2. This port links against
# libretro/sdl_compat instead, a software implementation that draws straight
# into the host's 320x240 RGB565 framebuffer. The engine includes SDL as
# <SDL2/SDL.h>, so -Ilibretro/sdl_compat is what resolves that here.

SF2000_PLATFORM ?= native

TARGET_NAME  := openclaw
ENGINE_DIR   := OpenClaw
LIBRETRO_DIR := libretro

# main.cpp defines main() and calls RunGameEngine(), which owns an unbounded
# loop. The libretro core drives StepLoop() itself instead, so it is excluded.
# Miniz.cpp in Engine/Resource duplicates libwap/Miniz.cpp, so it is also excluded.
ENGINE_SOURCES  := $(shell find $(ENGINE_DIR) -name '*.cpp' ! -name 'main.cpp' ! -name 'Miniz.cpp')
SDL_SOURCES     := $(wildcard $(LIBRETRO_DIR)/sdl_compat/*.cpp)
CORE_SOURCES    := $(LIBRETRO_DIR)/libretro_core.cpp
BOX2D_SOURCES   := $(shell find Box2D/Box2D -name '*.cpp')
LIBWAP_SOURCES  := $(wildcard libwap/*.cpp)
TINYXML_SOURCES := ThirdParty/Tinyxml/tinystr.cpp \
                   ThirdParty/Tinyxml/tinyxml.cpp \
                   ThirdParty/Tinyxml/tinyxmlerror.cpp \
                   ThirdParty/Tinyxml/tinyxmlparser.cpp

ALL_SOURCES := $(ENGINE_SOURCES) $(SDL_SOURCES) $(CORE_SOURCES) \
               $(BOX2D_SOURCES) $(LIBWAP_SOURCES) $(TINYXML_SOURCES)

ifeq ($(SF2000_PLATFORM),sf2000)
	# Data Frog SF2000 / GB300: HiChip MIPS32r2, little endian, no FPU.
	MIPS          ?= /opt/frog-toolchain/mipsel-mti-elf_stable/bin/mipsel-mti-elf-
	MIPS_BINUTILS ?= /opt/mips32-mti-elf/2019.09-03-2/bin/mips-mti-elf-
	CC      := $(MIPS)gcc
	CXX     := $(MIPS)g++
	AR      := $(MIPS_BINUTILS)ar
	TARGET  := $(TARGET_NAME)_libretro_sf2000.a
	BUILD_DIR := build/sf2000
	STATIC_LINKING := 1

	ARCH_FLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float -G0 \
	              -mno-abicalls -fno-pic -ffreestanding -fno-strict-aliasing
	OPT_FLAGS  := -O2
else
	CC     := gcc
	CXX    := g++
	AR     := ar
	TARGET := $(TARGET_NAME)_libretro.so
	BUILD_DIR := build/native
	fpic    := -fPIC
	SHARED  := -shared
	STATIC_LINKING := 0
	ARCH_FLAGS :=
	OPT_FLAGS := -O2 -DSF2000
endif

WARN_FLAGS := -Wall -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable

INCLUDES := -I. -I$(ENGINE_DIR) -I$(LIBRETRO_DIR) -I$(LIBRETRO_DIR)/sdl_compat \
            -IBox2D -Ilibwap -IThirdParty -IThirdParty/Tinyxml

CXXFLAGS := $(ARCH_FLAGS) $(OPT_FLAGS) -std=c++11 $(WARN_FLAGS) $(fpic) \
            -ffunction-sections -fdata-sections $(INCLUDES) -include Engine/Util/StdCompat.h $(EXTRA_CXXFLAGS)

# newlib has neither <sys/utime.h> nor unprototyped ftello/fseeko under the
# default feature set. Miniz only uses those to preserve zip timestamps, which
# is irrelevant for read-only game assets.
MINIZ_FLAGS := -DMINIZ_NO_TIME -D_LARGEFILE_SOURCE

OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(ALL_SOURCES))
DEPS    := $(OBJECTS:.o=.d)

.PHONY: all clean

all: $(TARGET)

ifeq ($(STATIC_LINKING),1)
$(TARGET): $(OBJECTS)
	@echo "Creating static archive $@"
	$(AR) rcs $@ $(OBJECTS)
else
$(TARGET): $(OBJECTS)
	@echo "Linking shared library $@"
	$(CXX) $(SHARED) $(CXXFLAGS) -o $@ $(OBJECTS) -lm
endif

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/libwap/Miniz.o: libwap/Miniz.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(MINIZ_FLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf build $(TARGET)
