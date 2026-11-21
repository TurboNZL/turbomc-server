<div align="center">

# TurboMC Server

**Run, update and manage your Minecraft servers from one familiar window.**

[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0--only-C4282D?style=for-the-badge&logo=gnu&logoColor=white)](LICENSE)
[![Qt 6](https://img.shields.io/badge/Qt-6.8-41CD52?style=for-the-badge&logo=qt&logoColor=white)](https://www.qt.io/)
[![C++](https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![Status](https://img.shields.io/badge/status-early%20development-orange?style=for-the-badge)](#-status)
[![Platforms](https://img.shields.io/badge/platforms-Linux%20%C2%B7%20Windows%20%C2%B7%20macOS-555?style=for-the-badge)](#-building)

<br />

TurboMC Server is a desktop app that keeps all your Minecraft **servers** in one list, the way
[Prism Launcher](https://prismlauncher.org) keeps your game installs: pick a version, install mods or plugins
from Modrinth, change versions with one click, and start the server with a live console.<br />
No Docker, no panel, no web server to babysit.

<sub>Part of <a href="https://turbosuite.prismalite.org">TurboSuite</a> by TurboNZL.</sub>

</div>

> [!IMPORTANT]
> TurboMC Server is a **fork** of [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher) (itself a fork of
> MultiMC) and is **not** affiliated with, endorsed by, or supported by the Prism Launcher project.
> It is also not affiliated with Mojang or Microsoft. Please do not ask Prism for help with it.

---

## ✨ What it does

| | |
|---|---|
| 🗂️ **One list for every server** | Each server is an *instance* with its own folder, settings, notes and world list, in the same MultiMC-style window you may already know. |
| ⬇️ **Install in two clicks** | Choose the software and a Minecraft version; the right jar is downloaded, and checked against the checksum when the provider publishes one. |
| 🔁 **Change versions easily** | Switch software or version from the **Server version** page. Nothing updates on its own. |
| 🖥️ **Live console** | Start, stop (graceful, with a save) or kill. Type commands, search the log, colour-coded warnings and errors. |
| 📜 **EULA handled** | The Minecraft EULA is shown once, and only you can accept it. |
| 🧩 **Mods and plugins** | Browse and install from Modrinth, filtered to what your server software can load. *Still being tested.* |
| ☕ **Java aware** | The Java version each Minecraft release needs is looked up and recorded. |

### Supported server software

| Software | Install | Mods / plugins | Notes |
|---|:---:|---|---|
| **Vanilla** | ✅ | data packs | Official Mojang server |
| **Paper** | ✅ | Paper / Spigot / Bukkit plugins | |
| **Purpur** | ✅ | Purpur / Paper / Spigot / Bukkit plugins | |
| **Fabric** | ✅ | Fabric mods (Modrinth) | Geyser and Floodgate are available on Modrinth for Bedrock crossplay |
| Quilt | 📋 | | Planned |
| Rust servers (e.g. Pumpkin) | 📋 | | Planned as *experimental*, pre-1.0 upstream |

## 🚦 Status

TurboMC Server is in **early development**. The core works end to end (install → EULA → start → console → graceful stop,
tested on Vanilla, Paper, Purpur and Fabric), but a lot is still on the way. There are no packaged releases yet.

| Area | State |
|---|:---:|
| Create / install servers (Vanilla, Paper, Purpur, Fabric) | ✅ |
| Start, stop, kill, live console with commands | ✅ |
| Switch software / version | ✅ |
| Modrinth mods and plugins for the installed software | 🚧 wired up, needs more testing |
| `server.properties` editor | ✅ |
| World backups (restore, delete), with an automatic one before version changes | ✅ |
| playit.gg tunnel manager (run only while the server runs) | 📋 |
| Port-forwarding helpers (UPnP, port-clash check) | 📋 |
| Automatic Java selection per server | 📋 |
| World / folder size view | 📋 |
| Optional, opt-in update checks | 📋 |
| Quilt, experimental Rust servers | 📋 |
| Linux `.deb` (Qt bundled), built by CI | 🚧 untested CI |
| AppImage, Windows and macOS builds | 📋 |

✅ done · 🚧 in progress · 📋 planned

**Nothing updates automatically.** Version changes, mod updates and tunnels only happen when you ask for them.

## 📦 Installing (early builds)

Linux (Debian / Ubuntu) builds are published as a rolling **nightly** release of the latest commit on `main`:
[Releases → Nightly](https://github.com/TurboNZL/turbomc-server/releases/tag/nightly).

```bash
sudo apt install ./turbomc-server_*.deb
turbomc-server
```

The package bundles Qt, so it does not depend on your distribution's Qt version. You need a **Java** runtime to run
servers (`default-jre-headless` is pulled in; newer Minecraft versions need a newer Java, which you can point the app at in Settings).
TurboMC Server keeps its data in `~/.local/share/TurboMCServer`, separate from any Prism Launcher install.

## 🔨 Building

You need a C++23 compiler, CMake ≥ 3.28, Ninja and **Qt 6.8 or newer**. Most distributions ship an older Qt, so use
the [Qt online installer](https://www.qt.io/download-qt-installer) or [`aqtinstall`](https://github.com/miurahr/aqtinstall).

<details>
<summary><b>Debian / Ubuntu example</b></summary>

```bash
sudo apt install build-essential cmake ninja-build pkg-config extra-cmake-modules \
    libcmark-dev cmark libarchive-dev libqrencode-dev libtomlplusplus-dev zlib1g-dev \
    libgl-dev libvulkan-dev openjdk-17-jdk-headless
pip install aqtinstall
aqt install-qt linux desktop 6.8.3 linux_gcc_64 -m qtnetworkauth -O ~/Qt
```

`openjdk-17-jdk-headless` is needed because the bundled Java helper jars target Java 7 and newer JDKs cannot compile that.

```bash
git clone https://github.com/TurboNZL/turbomc-server
cd turbomc-server
git submodule update --init libraries/libnbtplusplus

CMAKE_PREFIX_PATH=~/Qt/6.8.3/gcc_64 \
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DLauncher_QT_VERSION_MAJOR=6 -DBUILD_TESTING=OFF
cmake --build build
```

Run it with `LD_LIBRARY_PATH=~/Qt/6.8.3/gcc_64/lib ./build/turbomc-server`.

To make a `.deb` with Qt bundled: `scripts/package-linux.sh build ~/Qt/6.8.3/gcc_64 0.1.0 dist`.

</details>

### API keys

This build ships with **no third-party API keys**. Modrinth needs none, and Prism Launcher's keys are not reused here.
CurseForge is intentionally not supported.

## 🤝 Community & support

Found a bug or have an idea? [Open an issue](https://github.com/TurboNZL/turbomc-server/issues).
Please include your OS, the server software and version, and the console log.

## 🙏 Credits

TurboMC Server stands on the shoulders of:

- **[Prism Launcher](https://prismlauncher.org)** and its contributors, the code this project is forked from
- **[PolyMC](https://github.com/PolyMC/PolyMC)** and **[MultiMC](https://github.com/MultiMC/Launcher)**, where Prism came from
- **[PaperMC](https://papermc.io)**, **[Purpur](https://purpurmc.org)**, **[FabricMC](https://fabricmc.net)** and **Mojang**, whose servers it installs
- **[Modrinth](https://modrinth.com)** for the mod and plugin platform

Full copyright and licence notices are in [COPYING.md](COPYING.md).

## 🍴 Forking & redistributing

You are free to fork and redistribute under the terms of the [license](LICENSE) (this is a legal responsibility). In short:

- Keep it **GPL-3.0-only**, give recipients the source, and keep the copyright notices in [COPYING.md](COPYING.md).
- Make clear that your fork is not TurboMC Server, and is not endorsed by or affiliated with it.
- Use **your own API keys** (or none). Do not reuse keys from this repository's history.
- Do not use Mojang's or Microsoft's branding, or Prism Launcher's name and logo, to suggest an official connection.

## 📄 License

[![GPL-3.0](https://img.shields.io/badge/license-GPL--3.0--only-C4282D?logo=gnu&logoColor=white)](LICENSE)

All code is available under the **GPL-3.0-only** license, inherited from Prism Launcher. Parts derived from MultiMC
remain under their original Apache-2.0 notices (see [COPYING.md](COPYING.md)).

*Minecraft is a trademark of Mojang Synergies AB. This project is not an official Minecraft product.*
