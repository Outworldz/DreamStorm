<img align="left" width="100" height="100" src="doc/firestorm_256.png" alt="Logo of Firestorm viewer"/>

**[Firestorm](https://www.firestormviewer.org) is a free client for 3D virtual worlds such as Second Life and various OpenSim worlds where users can create, connect and chat with others from around the world.**

> [!IMPORTANT]
> **This is the Outworldz fork, branch `Water`. It is NOT the official Firestorm viewer.**
> It is an unofficial build made by [Outworldz](https://www.outworldz.com) for [DreamGrid](https://www.outworldz.com/DreamGrid/) and other OpenSim grids. Please do not report problems with it to the Firestorm team; open an issue on [this fork](https://github.com/Outworldz/phoenix-firestorm/issues) instead.

## Outworldz Sea Waves build

This build draws moving sea waves on OpenSim regions that run the PhysX physics engine with waves turned on. The waves you see are the same waves the simulator uses, so boats and floating objects bob on the crests you see, not on flat water.

### Download

A signed Windows installer is on the [Releases page](https://github.com/Outworldz/phoenix-firestorm/releases) of this fork.

- The installer is Authenticode-signed by Outworldz, so Windows shows Outworldz as the publisher.
- It installs as its own program, **Firestorm-Water**, next to any normal Firestorm. It does not replace or update the official viewer.
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

### Building it yourself

Follow the normal [Windows build instructions](doc/building_windows.md), using branch `Water` and the `ReleaseFS_open` configuration. Outworldz builds it with Visual Studio 2026:

- autobuild 3.10.2 needs a local patch so that it knows Visual Studio 18 ("Visual Studio 18 2026", toolset v145); set `AUTOBUILD_VSVER=180`.
- Visual Studio's bundled CMake 4 needs `CMAKE_POLICY_VERSION_MINIMUM=3.5`.
- `mt.exe` from Windows SDK 10.0.28000 cannot find `midlrtmd.dll` (link error LNK1327); put the 10.0.26100 SDK `bin\x64` folder on PATH.
- Configure: `autobuild configure -A 64 -c ReleaseFS_open -- --chan Water -DLL_TESTS:BOOL=FALSE -DPACKAGE:BOOL=ON`, then `autobuild build -A 64 -c ReleaseFS_open --no-configure`.
- The installer needs [NSIS](https://nsis.sourceforge.io). Signing runs only when `SIGNTOOL_PATH` is set.

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

## Community respect

This section is guided by the [TPV Policy](https://secondlife.com/corporate/third-party-viewers) and the [Second Life Code of Conduct](https://github.com/secondlife/viewer?tab=coc-ov-file).

Firestorm code is made available during ongoing development, with the **master** branch representing the current nightly build. Developers and self-compilers are encouraged to work on their own forks and contribute back via pull requests, as detailed in the [contributing guide](CONTRIBUTING.md).

If you intend to use our code for your own viewer beyond personal use, please only use code from official release branches (for example, `Firestorm_7.1.13`), rather than from pre-release/preview or nightly builds.
