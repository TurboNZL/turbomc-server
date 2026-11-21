#!/usr/bin/env bash
# Builds a .deb with Qt bundled (so it does not depend on the distro's Qt version).
#
#   scripts/package-linux.sh <build-dir> <qt-dir> <version> <out-dir>
#
# <qt-dir> is the Qt prefix used for the build, e.g. ~/Qt/6.8.3/gcc_64.
# Needs: dpkg-deb, patchelf is NOT required (a wrapper script sets the library path).
set -euo pipefail

BUILD=$(realpath "${1:?build dir}")
QT=$(realpath "${2:?qt dir}")
VERSION="${3:?version}"
OUT=$(mkdir -p "${4:?out dir}" && realpath "$4")

NAME=turbomc-server
APP=opt/$NAME
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ROOT=$STAGE/pkg
mkdir -p "$ROOT/$APP" "$ROOT/DEBIAN" "$ROOT/usr/bin" "$ROOT/usr/share"

echo "== install"
cmake --install "$BUILD" --prefix "$ROOT/$APP" >/dev/null
rm -f "$ROOT/$APP"/share/TurboMCServer/NewLaunch*.jar   # client-launch helpers, unused by a server manager

echo "== bundle Qt and private libraries"
LIB=$ROOT/$APP/lib
PLUG=$ROOT/$APP/plugins
mkdir -p "$LIB" "$PLUG"
for p in platforms platforminputcontexts imageformats iconengines xcbglintegrations tls networkinformation platformthemes; do
    [ -d "$QT/plugins/$p" ] || continue
    mkdir -p "$PLUG/$p"
    cp -a "$QT"/plugins/"$p"/*.so "$PLUG/$p/" 2>/dev/null || true
done
rm -f "$PLUG"/platformthemes/libqgtk3.so

# Libraries we ship: everything from the Qt prefix, plus the few third-party libs that Ubuntu/Debian versions differ on.
bundle_deps() {
    LD_LIBRARY_PATH="$QT/lib" ldd "$1" 2>/dev/null | awk '/=> \// {print $3}' | while read -r lib; do
        case "$lib" in
            "$QT"/*|*/libtomlplusplus.so*|*/libcmark.so*|*/libarchive.so*|*/liblz4.so*) cp -naL "$lib" "$LIB/" ;;
        esac
    done
}
bundle_deps "$ROOT/$APP/bin/$NAME"
while IFS= read -r so; do bundle_deps "$so"; done < <(find "$PLUG" -name '*.so')
# Qt libraries loaded lazily by plugins
bundle_deps "$LIB/libQt6Gui.so.6"
for extra in libQt6XcbQpa.so.6 libQt6WaylandClient.so.6 libQt6DBus.so.6 libQt6OpenGL.so.6 libQt6Svg.so.6; do
    [ -e "$QT/lib/$extra" ] && cp -naL "$QT/lib/$extra" "$LIB/" || true
done
strip --strip-unneeded "$LIB"/*.so* "$ROOT/$APP/bin/$NAME" 2>/dev/null || true

echo "== wrapper, desktop integration"
cat > "$ROOT/$APP/$NAME.sh" <<'WRAP'
#!/bin/sh
DIR=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$DIR/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$DIR/plugins/platforms"
exec "$DIR/bin/turbomc-server" "$@"
WRAP
chmod 755 "$ROOT/$APP/$NAME.sh"
ln -s "/$APP/$NAME.sh" "$ROOT/usr/bin/$NAME"
for d in applications icons metainfo mime; do
    [ -d "$ROOT/$APP/share/$d" ] && mkdir -p "$ROOT/usr/share/$d" && cp -a "$ROOT/$APP/share/$d/." "$ROOT/usr/share/$d/"
done

echo "== control file"
SIZE=$(du -sk "$ROOT" | cut -f1)
cat > "$ROOT/DEBIAN/control" <<CTRL
Package: $NAME
Version: $VERSION
Section: games
Priority: optional
Architecture: amd64
Installed-Size: $SIZE
Maintainer: TurboNZL <noreply@users.noreply.github.com>
Homepage: https://github.com/TurboNZL/turbomc-server
Depends: libc6, libstdc++6, libgcc-s1, libgl1, libegl1, libxkbcommon0, libxkbcommon-x11-0, libxcb-cursor0, libxcb-icccm4, libxcb-keysyms1, libxcb-xkb1, libxcb-image0, libxcb-render-util0, libxcb-shape0, libxcb-xinerama0, libfontconfig1, libfreetype6, libdbus-1-3, libglib2.0-0, default-jre-headless | java-runtime-headless
Description: Run, update and manage Minecraft servers from one window
 TurboMC Server keeps all your Minecraft servers in one list. Install Vanilla,
 Paper, Purpur or Fabric, change versions, install Modrinth mods and plugins,
 edit server.properties, back up worlds and watch a live console.
 .
 Fork of Prism Launcher; not affiliated with Prism Launcher, Mojang or Microsoft.
CTRL

echo "== build deb"
DEB="$OUT/${NAME}_${VERSION}_amd64.deb"
dpkg-deb --build --root-owner-group -Zxz "$ROOT" "$DEB" >/dev/null
echo "$DEB"
