# Keep / strip / rework audit

Base: Prism Launcher `develop` at `5a52fca` (2026-11-19). C++/Qt6, CMake + vcpkg, about 105k lines under `launcher/`.
This is a first pass from reading the tree and directory sizes, not from a build. Nothing is removed yet.

## Size by directory (lines of .cpp/.h/.ui)
ui 59k, minecraft 25k, modplatform 12k, net 3k, updater 2.7k, java 2.2k, launch 1.6k, meta 1.4k, settings 1.4k, archive 1k, tasks 1k.

## Keep (core of the new app)
| Path | Why |
|---|---|
| `net/`, `tasks/`, `settings/`, `archive/` | Downloads, task/progress system, settings, zip handling. |
| `meta/` | Version-metadata fetch. Reused for server version lists where it fits. |
| `modplatform/modrinth/`, `modplatform/flame/`, `modplatform/ResourceAPI.*`, `modplatform/helpers/` (hash utils) | Mod search, install and update checks. |
| `minecraft/mod/` (mods, data packs, resource folder models, `tasks/`) | Installed-mod listing, metadata, update checks and dependency resolution. |
| `minecraft/World.*`, `WorldList.*`, `libraries/libnbtplusplus` | World listing and size. |
| `minecraft/PackProfile.*`, `Component.*` | The "Minecraft version + loader" model; the version switcher builds on it. |
| `updater/` | Prism's self-update system, to be pointed at our releases. |
| `ui/` shell: `MainWindow`, `pagedialog`, `instanceview`, `widgets`, `themes`, `ui/pages/modplatform` (Modrinth/CurseForge browse pages, resource models) | The familiar MultiMC-style interface. |
| `InstanceList`, `BaseInstance`, `InstanceCopyTask`, `InstanceImportTask` | Instance model; becomes "server" model. |

## Strip
| Path | What it is |
|---|---|
| `minecraft/auth/`, `minecraft/skins/`, `ui/pages/global/AccountListPage`, `ui/dialogs/MSALoginDialog`, `ProfileSelectDialog`, `ProfileSetupDialog`, `ChooseOfflineNameDialog` | Microsoft/Mojang accounts and skins. Servers need none of it. |
| `minecraft/launch/` (natives, assets, libraries, client jar mod) and `launch/` client steps | Game launching. |
| `minecraft/update/`, `AssetsUtils`, `Library`, `Rule`, `LaunchProfile` | Client libraries and assets. |
| `modplatform/atlauncher`, `ftb`, `legacy_ftb`, `import_ftb`, `technic` and their pages | Client modpack providers. Candidate to drop; modpack servers may come back later. |
| `ui/pages/instance`: `ScreenshotsPage`, `ServersPage`, `GameOptionsPage`, `McClient`, `McResolver` | Client-only pages (`ServerPingTask` may be reused for status). |
| `ui/pages/global/APIPage` pieces for Microsoft/Flame keys | Rework: keep the CurseForge key field only. |
| `news/`, `screenshots/`, `macsandbox/`, `filelink/` | Client features and news feed. |

## Rework
| Area | Change |
|---|---|
| `MinecraftInstance` | Mod, world and data-pack lists hang off it. Split into a server instance that owns the same folder models and a `server.properties` model, with a process controller replacing `createLaunchTask`. |
| `java/` | Keep detection, selection and the Java downloader. Server version to Java version mapping becomes automatic. |
| `LaunchController`, `LoggedProcess` | Becomes the server process controller: start, stop, console input, crash detection. |
| `ui/pages/instance/LogPage` | Becomes the live server console with command input. |
| `VersionPage` | Version switcher for server software and version, with backup before change. |
| New | Server software providers (Vanilla, Paper, Purpur, Fabric, Quilt), EULA flow, `server.properties` editor, backups, size view, playit.gg agent manager, UPnP and port-clash checks. |

## Findings that shape the plan
- Mojang's version JSON parser (`MojangVersionFormat.cpp`) reads every entry under `downloads`, so the Vanilla `server` jar URL is already available through the existing parser; it is just never used. Paper, Purpur, Fabric and Quilt need their own small API clients.
- The mod browse and update code is parameterised on the instance and its PackProfile (Minecraft version plus loader filter), so it should work for server instances once those expose the same interface.
- Build is CMake with vcpkg and Qt6; upstream CI builds Linux, Windows and macOS. A `.deb` target is not in the repo today and will be added.
- Prism's updater is only enabled for specific build configurations (`Launcher_ENABLE_UPDATER`), so it will be wired to our own releases.

## Licence notes
GPL-3.0-only. `COPYING.md` carries Prism, PolyMC and MultiMC (Apache-2.0) notices; these stay. Add `NOTICE`/credits and an About-dialog credit during rebranding. Third-party libraries under `libraries/` keep their own licences; audit before first release.
