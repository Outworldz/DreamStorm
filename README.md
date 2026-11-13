<img align="left" width="100" height="100" src="doc/dreamstorm_256.png" alt="DreamStorm logo"/>

**[Firestorm](https://www.firestormviewer.org) is a free client for 3D virtual worlds such as Second Life and various OpenSim worlds where users can create, connect and chat with others from around the world.**

> [!IMPORTANT]
> **This is the Outworldz fork, branch `Water`. It is NOT the official Firestorm viewer.**
> It is an unofficial build made by [Outworldz](https://www.outworldz.com) for [DreamGrid](https://www.outworldz.com/DreamGrid/) and other OpenSim grids. Please do not report problems with it to the Firestorm team; open an issue on [this fork](https://github.com/Outworldz/DreamStorm/issues) instead.

## DreamStorm (Firestorm with DreamGrid OceanMotion waves)

**DreamStorm** is the Outworldz name for this build. It draws moving sea waves on OpenSim regions that run the PhysX physics engine with waves turned on. The waves you see are the same waves the simulator uses, so boats and floating objects bob on the crests you see, not on flat water.

### Download

A signed Windows installer is on the [Releases page](https://github.com/Outworldz/DreamStorm/releases) of this fork.

- The installer is Authenticode-signed by Outworldz, so Windows shows Outworldz as the publisher.
- It installs as its own program, **Firestorm-DreamStorm**, next to any normal Firestorm. It does not replace or update the official viewer.
- It shares settings with the official 64-bit Firestorm (`%APPDATA%\Firestorm_x64`).
- This is an OpenSim build (open-source libraries only). It uses OpenAL for sound instead of FMOD, and OpenJPEG for textures instead of KDU, so textures may load a little slower than in the official viewer.

### How to see the waves

1. The region must run PhysX with waves on. In DreamGrid: region settings, Water tab, Sea Waves.
2. In the viewer, keep **Transparent Water** on (Preferences, Graphics). With it off, the water stays flat.
3. Keep your PC clock correct (Windows time sync on). The waves are timed by the clock, so a wrong clock moves the crests away from where the boats are.

The simulator tells the viewer about the waves through the region's simulator features (`OpenSimExtras.PhysXWaves`: Height, Style, Direction). Regions without it, and every Second Life region, show normal flat water.

### Known limits

- Waves are drawn on your own region and on empty water up to about 1 km away. Water further out stays flat, and the waves fade out smoothly between 384 and 768 m.
- A change of wave direction from the region's environment reaches the viewer when you next enter the region.

### What changed from Firestorm

All changes are marked `<FS:Outworldz>` in the code.

- `indra/newview/llvowater.h`, `llvowater.cpp`: reads the wave settings and builds a 2 m grid for the water surface.
- `indra/newview/lldrawpoolwater.cpp`: passes the wave settings to the water shader.
- `indra/newview/llviewerregion.cpp`: rebuilds the water when the wave settings arrive.
- `indra/newview/app_settings/shaders/class1/environment/waterV.glsl`: moves the water surface with the same wave formula the simulator uses.

### Licence

Firestorm, and so this fork, is under the [GNU Lesser General Public License v2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html) (LGPL).

- The full source for every installer published here is this repository, branch `Water`. Each release names the exact commit it was built from.
- You may use, change and pass on this build under the same licence. If you pass on a changed copy, you must offer its source code too.
- "Firestorm" is the name of the Phoenix Firestorm Project's viewer. This build uses the name only to say what it is based on. It is not made, checked or supported by the Firestorm team.
- Second Life's [Third Party Viewer Policy](https://secondlife.com/corporate/third-party-viewers) applies if you take this viewer to Second Life. The wave changes do nothing there.
- The Firestorm team asks that code used beyond personal use comes from an official release branch. This branch is based on Firestorm's development code (7.2.5 preview), so treat it as a test build.

### Patch for viewer developers

All the sea-wave code is in one patch file: [`patches/outworldz-sea-waves.patch`](patches/outworldz-sea-waves.patch) (5 files, about 300 lines). It is made against Firestorm `master` at `41c40409d9` (7.2.5 preview) and was checked to apply cleanly to Firestorm `master` on 2026-11-11. It touches only the viewer's water code, so it should also port to other viewers based on the Second Life viewer.

**Apply it with git** (from the top of your Firestorm source tree):

```
curl -LO https://raw.githubusercontent.com/Outworldz/DreamStorm/Water/patches/outworldz-sea-waves.patch
git apply --check outworldz-sea-waves.patch
git apply --3way outworldz-sea-waves.patch
```

1. `--check` only tests it and changes nothing. No output means it fits.
2. `--3way` applies it. If your tree has moved on, git merges the changes and marks any real conflicts in the files for you to fix by hand.
3. Rebuild the viewer as usual. No new libraries, settings or build switches are needed.

**Without git:** `patch -p1 < outworldz-sea-waves.patch`

**Or pull the commits** instead of the file, which keeps the history:

```
git remote add outworldz https://github.com/Outworldz/DreamStorm.git
git fetch outworldz Water
git cherry-pick c52e7e69d6 493fdcab6d
```

**What the simulator must send.** The viewer draws waves only when the region's simulator features contain `OpenSimExtras.PhysXWaves`, a map with:

- `Height`: wave height in metres, crest to trough (0 = flat water).
- `Style`: 0 to 1, from long gentle swell to short steep chop.
- `Direction`: the direction the waves travel, in compass degrees (0 = north, 90 = east).

With no `PhysXWaves` (Second Life, other physics engines, or waves turned off) the patch changes nothing and the water draws exactly as before. The wave formula (a sum of 4 Gerstner waves in grid-wide metres, timed by the Unix clock) is in `LLWaterWaves` in `llvowater.cpp`. It must match the simulator's wave code (`PhysXWaves.cs` in the [Outworldz PhysX physics module for OpenSim](https://github.com/Outworldz/Opensimulator_Physx)) exactly, so boats float on the crests the viewer draws. If you change one, change both.

### Building it yourself

The steps are the same as for normal Firestorm, with three differences: clone this fork, use branch `Water`, and use the `ReleaseFS_open` configuration (open-source libraries only, no FMOD or KDU licence needed). Every platform needs [autobuild](https://github.com/secondlife/autobuild) and the build variables:

```
git clone -b Water https://github.com/Outworldz/DreamStorm.git
git clone https://github.com/FirestormViewer/fs-build-variables.git
export AUTOBUILD_VARIABLES_FILE=$PWD/fs-build-variables/variables
cd phoenix-firestorm
```

#### Windows

Follow the [Windows build instructions](doc/building_windows.md). Outworldz builds with Visual Studio 2026:

- autobuild 3.10.2 needs a local patch so that it knows Visual Studio 18 ("Visual Studio 18 2026", toolset v145); set `AUTOBUILD_VSVER=180`. With Visual Studio 2022 no patch is needed.
- Visual Studio's bundled CMake 4 needs `CMAKE_POLICY_VERSION_MINIMUM=3.5`.
- `mt.exe` from Windows SDK 10.0.28000 cannot find `midlrtmd.dll` (link error LNK1327); put the 10.0.26100 SDK `bin\x64` folder on PATH.
- Configure: `autobuild configure -A 64 -c ReleaseFS_open -- --chan DreamStorm -DLL_TESTS:BOOL=FALSE -DPACKAGE:BOOL=ON`
- Build: `autobuild build -A 64 -c ReleaseFS_open --no-configure`
- The installer (`*Setup.exe` in `build-vc*-64/newview/Release`) needs [NSIS](https://nsis.sourceforge.io). Signing runs only when `SIGNTOOL_PATH` is set.

#### Linux

Follow the [Linux build instructions](doc/building_linux.md) (written for Ubuntu 22.04/24.04). In short:

```
sudo apt install libgl1-mesa-dev libglu1-mesa-dev libpulse-dev build-essential python3-pip git libssl-dev libxinerama-dev libxrandr-dev libfontconfig-dev libfreetype6-dev cmake
pip install autobuild llsd
autobuild configure -A 64 -c ReleaseFS_open -- --chan DreamStorm -DLL_TESTS:BOOL=FALSE --package
autobuild build -A 64 -c ReleaseFS_open --no-configure
```

- The first build takes about an hour and needs 16 GB of memory or more (add swap, or use fewer cores, if it runs out).
- The result is a `.tar.xz` in `build-linux-x86_64/newview`. Unpack it anywhere and run `./firestorm` from that folder. For a menu entry, run `etc/refresh_desktop_app_entry.sh` from that folder once.

#### macOS

Follow the [macOS build instructions](doc/building_macos.md). In short:

1. Install Xcode (open it once so it finishes setting up), then `brew install cmake` (or the CMake app from cmake.org).
2. `pip3 install autobuild llsd`
3. Configure and build:

```
autobuild configure -A 64 -c ReleaseFS_open -- --chan DreamStorm -DLL_TESTS:BOOL=FALSE --package
autobuild build -A 64 -c ReleaseFS_open --no-configure
```

- The result is a `.dmg` in `build-darwin-*/newview`.
- A self-built Mac viewer is not signed by Apple. The first time, right-click the app and choose **Open**, then **Open** again. If macOS still says it is damaged, run `xattr -dr com.apple.quarantine /Applications/Firestorm*.app` once in Terminal.
- The Mac and Linux builds have not been tested by Outworldz yet. Please report what you find on the [issues page](https://github.com/Outworldz/DreamStorm/issues).

---

This repository contains the official source code for the Firestorm viewer.

## Open Source

Firestorm is a third party viewer derived from the official [Second Life](https://github.com/secondlife/viewer) client. The client codebase has been open source since 2007 and is available under the LGPL license.

## Download

Pre-built versions of the viewer releases for Windows, Mac and Linux can be downloaded from the [official website](https://www.firestormviewer.org/choose-your-platform/).

## Build Instructions

Build instructions for each operating system can be found using the links below and in the official [wiki](https://wiki.firestormviewer.org).

- [Windows](doc/building_windows.md)
- [Mac](doc/building_macos.md)
- [Linux](doc/building_linux.md)

> [!NOTE]
> We do not provide support for compiling the viewer or issues resulting from using a self-compiled viewer. However, there is a self-compilers group within Second Life that can be joined to ask questions related to compiling the viewer: [Firestorm Self Compilers](https://tinyurl.com/firestorm-self-compilers)

## Contribute

Help make Firestorm better! You can get involved with improvements by filing bugs and suggesting enhancements via [JIRA](https://jira.firestormviewer.org) or [creating pull requests](CONTRIBUTING.md).

## Community Respect

This section is guided by the [TPV Policy](https://secondlife.com/corporate/third-party-viewers) and the [Second Life Code of Conduct](https://github.com/secondlife/viewer?tab=coc-ov-file).

Firestorm code is made available during ongoing development, with the **master** branch representing the current nightly build. Developers and self-compilers are encouraged to work on their own forks and contribute back via pull requests, as detailed in the [contributing guide](CONTRIBUTING.md).

If you intend to use our code for your own viewer beyond personal use, please only use code from official release branches (for example, `Firestorm_7.1.13`), rather than from pre-release/preview or nightly builds.
