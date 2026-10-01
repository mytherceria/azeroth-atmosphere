# Azeroth Atmosphere

Weather, light and a quieter screen for World of Warcraft 1.12 clients. Made on RavenCraft's client (TWMOA 1.18.1)
running VanillaFixes; it should suit any Turtle-based 1.12 client with the same DLL loader.

## What is in it

- **Indoor Weather** (`IndoorRain.dll` and the `IndoorRain` addon). Rain, snow and sandstorms sound muffled while you
  are under a roof and fade at the door. Storms outside get thunder at random distances, wind gusts and a lightning
  flash, and the rain loops lose the thunder the game had recorded into them on a fixed cycle. Every one of these
  sounds is built in memory when the game starts, from the game's own files on your machine: the pack contains no
  game audio at all. Details in [IndoorRain/README.md](IndoorRain/README.md).
- **The zone moods** (the addon shown in game as Azeroth Atmosphere, folder `AtmosphereDirector`). A written mood and a preset for every zone, city, dungeon and raid in the client,
  118 of them, using comfyatmosphere's fog, sun rays, night and clouds, plus a colour wash over the world. Where
  footage of WoW Forever exists, the preset is measured from it. `/atmos` opens one window with every switch and
  slider of the pack.
- **Clean Screen** (addon). The whole interface fades away after a minute of doing nothing; any activity brings it
  straight back. `/cleanscreen`.
- **Rain textures** (`Data\patch-R.MPQ`). A softer, brighter raindrop and a new splash, small, bright and quick, with
  ripples and a glint off wet ground. Both are drawn from scratch by the scripts in `RainTextures/`.

## What it needs

- **VanillaFixes**, which loads `IndoorRain.dll` from `dlls.txt`.
- **ClassicAPI or SuperWoW**: the Indoor Weather addon asks the game whether you are indoors through either one.
- For the Director's looks: **comfyatmosphere** (fog, sun rays, volumetric light) and, if you like, **comfygrass**
  (moving grass), both by aloofbit and both needing DXVK. They are not included; get them from
  https://github.com/aloofbit/comfyatmosphere and https://github.com/aloofbit/comfygrass. Without comfyatmosphere
  the Director still applies its colour wash, and Indoor Weather works on its own (only its storm fog needs comfy).

## Installing by hand

1. Close the game.
2. Unzip `AzerothAtmosphere-<version>.zip` from the latest release into the game folder (the one with `WoW.exe`).
   It adds `IndoorRain.dll`, three folders under `Interface\AddOns`, and `Data\patch-R.MPQ`. If your client
   already has a `patch-R.MPQ` of its own, rename ours to a letter nothing else uses.
3. Add a line `IndoorRain.dll` to `dlls.txt`. VanillaFixes shows a one-time reminder that its DLL list changed.
4. Start the game. `/indoorrain status`, `/atmos` and `/cleanscreen` show that everything is there.

To remove it: delete `IndoorRain.dll` and its line in `dlls.txt`, the folders `Interface\AddOns\IndoorRain`,
`AtmosphereDirector` and `CleanScreen`, and `Data\patch-R.MPQ`. Indoor Weather also leaves `IndoorRain.ini` next to
`WoW.exe` and `Logs\IndoorRain.log`.

## Building it

`tools/make_release.sh` builds everything into `dist/`: the DLL (with `i686-w64-mingw32-gcc`), the textures (Python
with numpy and Pillow), the archive (through StormLib: set `STORMLIB` to the path of `libstorm.so`), the zips and
`SHA256SUMS`. Each part has its own notes: [IndoorRain/README.md](IndoorRain/README.md) for the DLL, its Wine test
harness and its native self-tests; `AtmosphereDirector/tools/` for the zone presets (edit `zones_source.py`, then
run `build_zones.py`); `RainTextures/` for the textures.

## License

Everything in this repository is free software under the GNU General Public License, version 3 (`LICENSE`). The
compressor in `IndoorRain/dll/dsp.c` is a port of FFmpeg's (LGPL-2.1 or later), used here under GPL-3.0.

The game's own sounds and art are not part of this project and are not covered by its license. Indoor Weather
reads the sounds from the player's own client while the game runs and never writes them anywhere; the textures are
drawn from scratch.

comfyatmosphere and comfygrass are aloofbit's own projects, linked above, not included. The zone moods draw on public
WoW Forever gameplay footage and on turtlecraft.gg. World of Warcraft is a trademark of Blizzard Entertainment;
this project is not affiliated with Blizzard.
