#!/bin/bash
# Build OpenClaw for Data Frog SF2000 / GB300 (Multicore/FrogUI).
#
#   ./scripts/build.sh native   -> openclaw_libretro.so  (x86_64, for test_runner)
#   ./scripts/build.sh sf2000   -> openclaw_libretro_sf2000.a (MIPS32r2 static lib)
#   ./scripts/build.sh link     -> core_87000000 (GB300 & SF2000 binaries)
#   ./scripts/build.sh all      -> everything
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MULTICORE="${MULTICORE:-/home/Sajnaps/gb300/sf2000_multicore}"
MIPS_PREFIX="${MIPS_PREFIX:-/opt/mips32-mti-elf/2019.09-03-2/bin/mips-mti-elf-}"

CORE_NAME="claw"

step() { printf '\n\033[1;33m==> %s\033[0m\n' "$1"; }

build_native() {
	step "Building native x86_64 core (for test_runner)"
	make -C "$PROJECT_DIR" SF2000_PLATFORM=native -j$(nproc)
	gcc -O2 -Wall -I"$PROJECT_DIR/libretro" \
		-o "$PROJECT_DIR/test_runner" "$PROJECT_DIR/runner/test_runner.c" -ldl
	echo "Built: openclaw_libretro.so, test_runner"
}

build_sf2000() {
	step "Building MIPS32r2 static library for SF2000/GB300"
	make -C "$PROJECT_DIR" SF2000_PLATFORM=sf2000 -j$(nproc)
	${MIPS_PREFIX}size "$PROJECT_DIR/openclaw_libretro_sf2000.a" | tail -1
	echo "Built: openclaw_libretro_sf2000.a"
}

link_cores() {
	if [ ! -d "$MULTICORE" ]; then
		echo "ERROR: Multicore repo not found at $MULTICORE" >&2
		exit 1
	fi

	mkdir -p "$MULTICORE/cores/openclaw"
	cat > "$MULTICORE/cores/openclaw/Makefile" <<EOF
TARGET_NAME := openclaw
OPENCLAW_SRC ?= $PROJECT_DIR

ifeq (\$(platform), sf2000)
	TARGET := \$(TARGET_NAME)_libretro_\$(platform).a
	STATIC_LINKING = 1
endif

all:
	\$(MAKE) -C "\$(OPENCLAW_SRC)" SF2000_PLATFORM=sf2000
	cp -f "\$(OPENCLAW_SRC)/\$(TARGET)" ./\$(TARGET)

clean:
	rm -f *.a

.PHONY: all clean
EOF
	ln -sfn openclaw "$MULTICORE/cores/claw" 2>/dev/null || true
	mkdir -p "$MULTICORE/dist"

	for FROGGY in GB300V2 SF2000; do
		step "Linking core for $FROGGY"
		rm -rf "$MULTICORE/build"
		make -C "$MULTICORE" FROGGY_TYPE="$FROGGY" \
			CORE="cores/openclaw" CONSOLE="$CORE_NAME" \
			MIPS="$MIPS_PREFIX" >/dev/null

		# Copy core_87000000 binaries
		cp "$MULTICORE/build/core_87000000" "$PROJECT_DIR/core_87000000_$FROGGY"
		LOWER=$(echo "$FROGGY" | tr '[:upper:]' '[:lower:]')
		cp "$MULTICORE/build/core_87000000" "$PROJECT_DIR/core_87000000_$LOWER"
		cp "$MULTICORE/build/core_87000000" "$MULTICORE/dist/core_87000000_$LOWER"

		echo "Built: core_87000000_$FROGGY ($(stat -c %s "$PROJECT_DIR/core_87000000_$FROGGY") bytes)"
	done

	# Default core_87000000 (GB300V2) in project root & multicore root
	cp "$PROJECT_DIR/core_87000000_GB300V2" "$PROJECT_DIR/core_87000000"
	cp "$PROJECT_DIR/core_87000000_GB300V2" "$PROJECT_DIR/core_87"
	cp "$PROJECT_DIR/core_87000000_GB300V2" "$MULTICORE/core_87000000"
}

case "${1:-all}" in
	native) build_native ;;
	sf2000) build_sf2000 ;;
	link)   link_cores ;;
	all)    build_sf2000; link_cores ;;
	*)      echo "usage: $0 {native|sf2000|link|all}" >&2; exit 1 ;;
esac

echo
echo "Done."
