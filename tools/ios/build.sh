#!/bin/sh
# tools/ios/build.sh — build, install and launch the iOS app (docs/IOS.md).
#
# Runs on the Mac (Apple Silicon or Intel) with Xcode; no third-party tools.
# The compiled objects go to the ignored build/ios/<sdk>/. The app bundles go
# to Xcode's own DerivedData folder (EM_IOS_PRODUCTS below), outside the
# repository: codesign refuses a bundle inside a file-provider folder such as
# an iCloud-synced ~/Documents, which tags every .app it sees with attributes
# that cannot be removed. The app CONTAINS THE USER'S OWN DISC DATA (the
# local assets/ and data/ exports are copied into the bundle): never share,
# upload or commit it; `build.sh clean` deletes every copy on the Mac.
#
#   tools/ios/build.sh sim                     simulator app
#   tools/ios/build.sh sim-run [VAR=value ...] simulator app, booted, installed, launched
#   tools/ios/build.sh device                  device app, signed (Apple Development)
#   tools/ios/build.sh install                 install the device app on the phone
#   tools/ios/build.sh launch [--console] [VAR=value ...]
#                                              launch it on the phone (with env switches)
#   tools/ios/build.sh run [VAR=value ...]     device + install + launch
#   tools/ios/build.sh fetch FILE [DEST]       copy Documents/FILE off the phone
#   tools/ios/build.sh clean                   delete build/ios and the app bundles
#
# Settings (environment):
#   EM_IOS_TEAM       signing team ID (default: the OU of your first
#                     "Apple Development" certificate, e.g. a free personal team)
#   EM_IOS_BUNDLE_ID  bundle identifier (default com.exterminationport.game)
#   EM_IOS_DEVICE     device UDID or name (default: the first available paired iPhone)
#   EM_IOS_SIM        simulator name (default "iPhone 17 Pro")
#   EM_IOS_PRODUCTS   where Xcode builds the apps (default
#                     ~/Library/Developer/Xcode/DerivedData/<checkout name>-ios)
#
# The Xcode project's build phases call `xcode-lib` (make ios-lib for the
# SDK being built) and `xcode-assets` (copy assets/ and data/ into the app).
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT="$ROOT/build/ios"
XC=${EM_IOS_PRODUCTS:-$HOME/Library/Developer/Xcode/DerivedData/$(basename "$ROOT")-ios}
PROJECT="$HERE/Extermination.xcodeproj"
BUNDLE_ID=${EM_IOS_BUNDLE_ID:-com.exterminationport.game}
SIM=${EM_IOS_SIM:-iPhone 17 Pro}
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

die() { echo "ios: $*" >&2; exit 1; }

team_id() {
    if [ -n "${EM_IOS_TEAM:-}" ]; then echo "$EM_IOS_TEAM"; return; fi
    security find-certificate -c "Apple Development" -p 2>/dev/null |
        openssl x509 -noout -subject 2>/dev/null |
        sed -n 's/.*OU *= *\([A-Z0-9]*\).*/\1/p' | head -1
}

device_id() {
    if [ -n "${EM_IOS_DEVICE:-}" ]; then echo "$EM_IOS_DEVICE"; return; fi
    json="$OUT/devices.json"
    mkdir -p "$OUT"
    xcrun devicectl list devices --json-output "$json" >/dev/null 2>&1 || die "devicectl cannot list devices"
    python3 - "$json" <<'PY'
import json, sys
for d in json.load(open(sys.argv[1]))["result"]["devices"]:
    hw = d.get("hardwareProperties", {})
    if hw.get("deviceType") == "iPhone" and hw.get("reality") == "physical" \
            and d.get("connectionProperties", {}).get("pairingState") == "paired":
        print(hw.get("udid", d.get("identifier")))
        break
PY
}

check_assets() {
    [ -d "$ROOT/assets" ] || die "no assets/ in $ROOT: export the user's own disc first (docs/STARTUP.md)"
}

# The movie converter, a macOS tool (built for the Mac even inside an iOS
# Xcode build, whose environment names the iOS SDK).
transcoder() {
    tool="$OUT/movie_transcode"
    src="$HERE/movie_transcode.m"
    if [ ! -x "$tool" ] || [ "$src" -nt "$tool" ]; then
        mkdir -p "$OUT"
        env -u SDKROOT -u IPHONEOS_DEPLOYMENT_TARGET xcrun --sdk macosx clang -O2 -Wall -Wextra \
            -fobjc-arc -framework Foundation -framework AVFoundation -framework CoreMedia \
            -framework CoreVideo "$src" -o "$tool" >&2
    fi
    echo "$tool"
}

products() { echo "$XC/Products/Release-$1/Extermination.app"; }

xcode_build() {   # $1 = iphoneos | iphonesimulator
    check_assets
    team=$(team_id)
    sign=""
    if [ "$1" = iphoneos ]; then
        [ -n "$team" ] || die "no Apple Development certificate; sign in to Xcode (Settings > Accounts) once, or set EM_IOS_TEAM"
        sign="DEVELOPMENT_TEAM=$team -allowProvisioningUpdates -allowProvisioningDeviceRegistration"
    fi
    # The library first, so a compile error reads plainly (the project's own
    # phase then finds it up to date).
    make -C "$ROOT" -j"$JOBS" ios-lib IOS_SDK="$1"
    # shellcheck disable=SC2086
    xcodebuild -project "$PROJECT" -target Extermination -configuration Release -sdk "$1" \
        SYMROOT="$XC/Products" OBJROOT="$XC/Intermediates" \
        PRODUCT_BUNDLE_IDENTIFIER="$BUNDLE_ID" $sign -quiet build
    codesign --verify "$(products "$1")" || die "the built app's signature does not verify"
    echo "ios: built $(products "$1")"
}

launch_args() {   # VAR=value ... -> a JSON object for devicectl
    python3 - "$@" <<'PY'
import json, sys
env = {}
for a in sys.argv[1:]:
    k, _, v = a.partition("=")
    env[k] = v
print(json.dumps(env))
PY
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
xcode-lib)
    : "${PLATFORM_NAME:?run from Xcode}"
    exec make -C "$ROOT" -j"$JOBS" ios-lib IOS_SDK="$PLATFORM_NAME"
    ;;
xcode-assets)
    : "${TARGET_BUILD_DIR:?run from Xcode}"
    check_assets
    dest="$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH"
    # -L: the worktree's assets/ may be a directory of symlinks.
    rsync -a -L --delete --exclude '*.bak' --exclude '.DS_Store' --exclude '*.mov' \
        "$ROOT/assets/" "$dest/assets/"
    # Movies: iOS has no MPEG-2 decoder; the bundle gets HEVC conversions
    # (tools/ios/movie_transcode.m), cached in build/ios/movies/.
    tool=$(transcoder)
    (cd "$ROOT/assets" && find -L . -name '*.mov' -type f) | while read -r rel; do
        rel=${rel#./}
        cache="$OUT/movies/$rel"
        if [ ! -f "$cache" ] || [ "$ROOT/assets/$rel" -nt "$cache" ]; then
            mkdir -p "$(dirname "$cache")"
            "$tool" "$ROOT/assets/$rel" "$cache.tmp" && mv "$cache.tmp" "$cache"
        fi
        mkdir -p "$(dirname "$dest/assets/$rel")"
        rsync -a "$cache" "$dest/assets/$rel"
    done
    mkdir -p "$dest/data/save"
    if [ -d "$ROOT/data" ]; then
        rsync -a -L --exclude '.DS_Store' "$ROOT/data/" "$dest/data/"
    fi
    # codesign refuses Finder info / file-provider attributes on resources.
    xattr -cr "$dest/assets" "$dest/data" 2>/dev/null || true
    # Xcode does not track what this phase changes: touch one of its
    # CodeSign inputs so every build seals the current resources.
    touch "$TARGET_BUILD_DIR/$INFOPLIST_PATH"
    ;;
sim)
    xcode_build iphonesimulator
    ;;
sim-run)
    xcode_build iphonesimulator
    xcrun simctl boot "$SIM" 2>/dev/null || true
    xcrun simctl install "$SIM" "$(products iphonesimulator)"
    for a in "$@"; do export "SIMCTL_CHILD_$a"; done
    xcrun simctl launch --terminate-running-process "$SIM" "$BUNDLE_ID"
    ;;
device)
    xcode_build iphoneos
    ;;
install)
    app=$(products iphoneos)
    [ -d "$app" ] || die "build the device app first: tools/ios/build.sh device"
    dev=$(device_id); [ -n "$dev" ] || die "no paired iPhone available (unlock it; check xcrun devicectl list devices)"
    xcrun devicectl device install app --device "$dev" "$app"
    ;;
launch)
    dev=$(device_id); [ -n "$dev" ] || die "no paired iPhone available"
    console=""
    if [ "${1:-}" = "--console" ]; then console="--console"; shift; fi
    if [ $# -gt 0 ]; then
        xcrun devicectl device process launch --device "$dev" --terminate-existing $console \
            --environment-variables "$(launch_args "$@")" "$BUNDLE_ID"
    else
        xcrun devicectl device process launch --device "$dev" --terminate-existing $console "$BUNDLE_ID"
    fi
    ;;
run)
    "$0" device
    "$0" install
    "$0" launch "$@"
    ;;
fetch)
    [ $# -ge 1 ] || die "usage: build.sh fetch FILE [DEST]"
    dev=$(device_id); [ -n "$dev" ] || die "no paired iPhone available"
    dest=${2:-$OUT/device-files/$(basename "$1")}
    mkdir -p "$(dirname "$dest")"
    xcrun devicectl device copy from --device "$dev" --domain-type appDataContainer \
        --domain-identifier "$BUNDLE_ID" --source "Documents/$1" --destination "$dest"
    echo "ios: $dest"
    ;;
clean)
    rm -rf "$OUT" "$XC"
    echo "ios: removed $OUT and $XC"
    ;;
*)
    sed -n '2,36p' "$0" | sed 's/^# \{0,1\}//'
    [ -z "$cmd" ] || exit 1
    ;;
esac
