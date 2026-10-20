#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
HERE=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BIN=${1:?path to rebuilt usb_hid}
package list /boot/system/packages/haiku-*.hpkg | grep -F 'version: r1~beta6_hrev60072_269_dirty-1' >/dev/null
[ "$(uname -m)" = x86_64 ]
STAGE=$(mktemp -d /tmp/macbook-touchpad.XXXXXX)
trap 'rm -rf "$STAGE"' EXIT HUP INT TERM
mkdir -p "$STAGE/add-ons/kernel/drivers/bin" "$STAGE/documentation/packages/macbook-touchpad"
cp -p /boot/system/add-ons/kernel/drivers/bin/usb_hid "$STAGE/add-ons/kernel/drivers/bin/usb_hid"
cat "$BIN" > "$STAGE/add-ons/kernel/drivers/bin/usb_hid"
chmod 755 "$STAGE/add-ons/kernel/drivers/bin/usb_hid"
touch "$STAGE/add-ons/kernel/drivers/bin/usb_hid"
cp "$HERE/"*.md "$HERE/macbook-touchpad.patch" "$STAGE/documentation/packages/macbook-touchpad/"
cp -R "$HERE/src" "$HERE/tests" "$HERE/package" "$STAGE/documentation/packages/macbook-touchpad/"
cat > "$STAGE/.PackageInfo" <<INFO
name macbook_touchpad
version 0.2.6-1
architecture x86_64
summary "MacBook7,1 raw touchpad movement and scrolling"
description "Experimental native Wellspring pointer support for MacBook7,1 USB 05ac:0236. Bypasses firmware mouse emulation and retains small movements on both axes. Single-finger movement, tap to click, physical click, dragging with a second contact and two-finger vertical/horizontal scrolling. Retains macbook_fnkey fixes. Restart after installing or removing."
vendor rainygirl
packager "rainygirl <rainygirl@gmail.com>"
licenses { "MIT" }
copyrights { "2008-2011 Michael Lotz" "2026 rainygirl" }
provides { macbook_touchpad = 0.2.6 }
requires { haiku == r1~beta6_hrev60072_269_dirty
 macbook_fnkey == 1.0.0 }
flags { system_package }
INFO
mkdir -p "$HERE/dist"
package create -C "$STAGE" "$HERE/dist/macbook_touchpad-0.2.6-1-x86_64.hpkg"
sha256sum "$HERE/dist/macbook_touchpad-0.2.6-1-x86_64.hpkg"
