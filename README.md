# FroggyClaw

Captain Claw (1997) as a libretro core for the **Data Frog SF2000** and **GB300** handhelds, running on the Multicore / FrogUI firmware.

The original 1997 game needs a CD drive and a Pentium. This port renders it straight into the console's 320x240 RGB565 framebuffer, replaces SDL2 with a small software backend, and packs everything the engine needs into one static binary.

Prebuilt binaries are on the [releases page](https://github.com/Synaps33/FroggyClaw/releases) — it contains both console cores, the engine assets and a default config, plus an `INSTALL.txt` with the exact card layout. No game data is included.

---

## What this is

* A full engine port of OpenClaw (based on [pjasicek/OpenClaw](https://github.com/pjasicek/OpenClaw)) plus Box2D, libwap and TinyXML.
* A `libretro/sdl_compat` layer that implements the parts of SDL2 the engine actually uses — video into the console framebuffer, audio via the frontend sample callback, input from the joypad.
* A build that targets **MIPS32r2 with no FPU**, links into the Multicore bootloader, and is deployed as a single `core_87000000` binary.
* A test runner so the whole thing can be driven and profiled on a desktop without a console attached.

## What is playable

* All 13 normal levels plus the boss levels.
* Menu, single player, level select, pause menu, options.
* Save / load of level progress through the engine's own `SAVES.XML`.
* Cutscenes and replay movies are not implemented.

---

## Console-specific features

| Feature | Detail |
| --- | --- |
| **Frame limiter** | `MaxFps` option in the pause menu, 30 FPS (default) or unlimited. Uses the RTOS tick from the firmware when limiting/delaying |
| **Render scale** | `Game Play Area` slider from 50% to 100%, default 80% (256x192) |
| **Display** | Fixed 320x240 RGB565 output, aspect ratio correct for the panel |
| **Audio** | Mono mixdown to the console speaker, 22050 Hz |
| **Persistence** | Options are written back to `config.xml` next to the game data |

---

## Building

Requires the MIPS32 toolchain used by the Multicore project:

```
MIPS32-mti-elf (mipsel, 2019.09-03-02)
```

Clone the Multicore firmware as well — the core is linked into its bootloader:

```
git clone https://github.com/synaps33/froggyclaw
cd froggyclaw

git clone <multicore firmware>   # e.g. sf2000_multicore, expected at ~/gb300/sf2000_multicore
```

Build everything:

```
./scripts/build.sh all
```

| Target | Output |
| --- | --- |
| `native` | `openclaw_libretro.so` + `test_runner` (x86-64, for development) |
| `sf2000` | `openclaw_libretro_sf2000.a` (MIPS32r2 static library) |
| `link` | `core_87000000` for GB300V2 and SF2000 |
| `all` | library plus both linked cores |

`scripts/build.sh` builds into the multicore repo; point `MULTICORE` at your checkout if it is elsewhere:

```
MULTICORE=/path/to/sf2000_multicore ./scripts/build.sh all
```

If the multicore tree is not available, the static library and the native build still work — only the final link step needs it.

---

## Installing on a console

```
./scripts/deploy_sdcard.sh /media/<user>/GB300
```

The script picks the matching build from the mount point (override with `CONSOLE=SF2000` / `CONSOLE=GB300V2`) and installs:

```
/cores/claw/core_87000000                       core binary
/cores/claw.sf2k                                core for the stock launcher
/system/Deimos/cores/claw.sf2k                  core for the Multicore frontend
/ROMS/claw/claw.gba                             launch stub (contains claw;claw;CLAW.REZ)
/ROMS/claw/CLAW.REZ                             game data
/ROMS/claw/ASSETS.ZIP                           engine assets
/ROMS/claw/config.xml                           options
```

`CLAW.REZ` is not distributed here — copy it in from your own copy of the game. The other assets are rebuilt from `Build_Release/ASSETS/`.

Launch from the FrogUI `ROMS/claw` folder.

---

## Game data

The core loads three files from the same directory:

* `CLAW.REZ` — levels, sprites, sound, music
* `ASSETS.ZIP` — engine resources, menus, fonts, and the `MENU.xml` / `INGAME_MENU.XML` definitions
* `config.xml` — options, created automatically on first run

To regenerate `ASSETS.ZIP` after editing `Build_Release/ASSETS/`:

```
cd Build_Release && zip -0 -r ../ASSETS.ZIP ASSETS
```

---

## Controls

| Button | Game | Menu |
| --- | --- | --- |
| D-Pad | move | navigate |
| A | jump | confirm |
| X | melee | |
| B | shoot | back |
| Y | cycle ammo | |
| START | | confirm |
| SELECT | | open menu / back |

---

## Testing without a console

```
make SF2000_PLATFORM=native
./scripts/build.sh native
./test_runner ./openclaw_libretro.so /path/to/CLAW.REZ 300
```

The runner dlopens the native core, replays a fixed walkthrough (menu, level 1, walking, jumping, melee, shooting), dumps every 30th frame into `frames/`, and reports per-frame cost and heap use.

Reported timing on the core path:

```
retro_run steady ~32 ms   (30 FPS target with the frame limiter off)
```

The number that matters is the best figure — a shared box makes the mean noisy.

---

## Layout

```
libretro/                libretro entry points and the SDL compatibility layer
  libretro_core.cpp      core API, frame pacing, asset resolution
  sdl_compat/            SDL2 subset: video, audio, input, fonts, image
OpenClaw/                engine (ported)
Box2D/                   physics
libwap/                  resource archive and file format support
ThirdParty/Tinyxml/      XML
runner/test_runner.c     desktop harness
scripts/build.sh         build
scripts/deploy_sdcard.sh SD card install
Build_Release/ASSETS/    menu definitions and images, zipped into ASSETS.ZIP
```

---

## Credits

* Captain Claw is a trademark of Monolit Productions. This port is a fan project and ships no game data.
* OpenClaw — pjasicek and contributors.
* Multicore firmware for SF2000 / GB300 — FrogUI community.

## License

The OpenClaw engine code it is based on carries its own license. Check the upstream repository for details.
