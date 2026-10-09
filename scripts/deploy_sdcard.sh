#!/bin/bash
# Install OpenClaw onto an SD card (FrogUI layout).
#
#   ./scripts/deploy_sdcard.sh [/media/Sajnaps/GB300]
#
# Layout expected by the FrogUI firmware:
#     /mnt/sda1/cores/<core>/core_87000000   <- core binary
#     /mnt/sda1/ROMS/<core>/<file>.gba       <- launch stub shown in the browser
#     /mnt/sda1/ROMS/<core>/CLAW.REZ         <- game assets
#     /mnt/sda1/ROMS/<core>/ASSETS.ZIP       <- OpenClaw assets
#     /mnt/sda1/ROMS/<core>/config.xml       <- game config
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SDCARD="${1:-/media/Sajnaps/GB300}"
CORE_NAME="claw"

# Which console build to install. Override with CONSOLE=SF2000|GB300V2
if [ -z "$CONSOLE" ]; then
	case "$SDCARD" in
		*GB300*|*gb300*) CONSOLE="GB300V2" ;;
		*)             CONSOLE="SF2000" ;;
	esac
fi

if [ ! -d "$SDCARD" ]; then
	echo "ERROR: SD card mount point '$SDCARD' not found." >&2
	echo "Pass the mount point as the first argument." >&2
	exit 1
fi

CORE_BIN="$PROJECT_DIR/core_87000000_$CONSOLE"
if [ ! -f "$CORE_BIN" ]; then
	echo "ERROR: $CORE_BIN missing. Run scripts/build.sh link first." >&2
	exit 1
fi

echo "==> Installing OpenClaw ($CONSOLE build) into $SDCARD"

# --- core binary ------------------------------------------------------------
mkdir -p "$SDCARD/cores/$CORE_NAME"
mkdir -p "$SDCARD/cores"
mkdir -p "$SDCARD/system/Deimos/cores"
cp "$CORE_BIN" "$SDCARD/cores/$CORE_NAME/core_87000000"
cp "$CORE_BIN" "$SDCARD/cores/$CORE_NAME.sf2k"
cp "$CORE_BIN" "$SDCARD/system/Deimos/cores/$CORE_NAME.sf2k"

# --- launch stub + game data ------------------------------------------------
mkdir -p "$SDCARD/ROMS/$CORE_NAME"
rm -f "$SDCARD/ROMS/$CORE_NAME"/*.gba
printf '%s' "$CORE_NAME;$CORE_NAME;CLAW.REZ" > "$SDCARD/ROMS/$CORE_NAME/$CORE_NAME.gba"
printf '%s' "$CORE_NAME;$CORE_NAME;CLAW.REZ" > "$SDCARD/ROMS/$CORE_NAME/$CORE_NAME"

# Root level stubs so the game is also visible / launchable from ROMS root
printf '%s' "$CORE_NAME;$CORE_NAME;CLAW.REZ" > "$SDCARD/ROMS/$CORE_NAME.gba"
printf '%s' "$CORE_NAME;$CORE_NAME;CLAW.REZ" > "$SDCARD/ROMS/$CORE_NAME;$CORE_NAME.gba"
printf '%s' "$CORE_NAME;$CORE_NAME;CLAW.REZ" > "$SDCARD/ROMS/$CORE_NAME;$CORE_NAME;CLAW.REZ.gba"

# Game assets from /home/Sajnaps/claw if present
CLAW_SRC="/home/Sajnaps/claw"
if [ -d "$CLAW_SRC" ]; then
	if [ -f "$CLAW_SRC/CLAW.REZ" ] && [ ! -f "$SDCARD/ROMS/$CORE_NAME/CLAW.REZ" ]; then
		echo "Copying CLAW.REZ..."
		cp "$CLAW_SRC/CLAW.REZ" "$SDCARD/ROMS/$CORE_NAME/CLAW.REZ"
	fi
	if [ -f "$CLAW_SRC/ASSETS.ZIP" ]; then
		cp -u "$CLAW_SRC/ASSETS.ZIP" "$SDCARD/ROMS/$CORE_NAME/ASSETS.ZIP" 2>/dev/null || cp "$CLAW_SRC/ASSETS.ZIP" "$SDCARD/ROMS/$CORE_NAME/ASSETS.ZIP"
	fi
	if [ -f "$CLAW_SRC/config.xml" ]; then
		cp -u "$CLAW_SRC/config.xml" "$SDCARD/ROMS/$CORE_NAME/config.xml" 2>/dev/null || cp "$CLAW_SRC/config.xml" "$SDCARD/ROMS/$CORE_NAME/config.xml"
	fi
	if [ -f "$CLAW_SRC/SAVES.XML" ]; then
		cp -u "$CLAW_SRC/SAVES.XML" "$SDCARD/ROMS/$CORE_NAME/SAVES.XML" 2>/dev/null || cp "$CLAW_SRC/SAVES.XML" "$SDCARD/ROMS/$CORE_NAME/SAVES.XML"
	fi
fi

echo
echo "Installed:"
echo "  $SDCARD/cores/$CORE_NAME/core_87000000"
echo "  $SDCARD/ROMS/$CORE_NAME/$CORE_NAME.gba"
echo "  $SDCARD/ROMS/$CORE_NAME/$CORE_NAME"
echo "  $SDCARD/ROMS/$CORE_NAME/CLAW.REZ"
echo "  $SDCARD/ROMS/$CORE_NAME/ASSETS.ZIP"
echo "  $SDCARD/ROMS/$CORE_NAME/config.xml"
echo
echo "Launch from the FrogUI ROMs/$CORE_NAME folder."
