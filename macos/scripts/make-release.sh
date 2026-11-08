#!/bin/bash
# Builds the macOS release: SplatThumb.app (with its Quick Look extension), signed with
# Developer ID, inside a notarized and stapled SplatThumb-<version>-macos.dmg, plus a
# .sha256 file next to it.
#
#   macos/scripts/make-release.sh [--team TEAMID] [--identity NAME] [--profile NAME]
#   macos/scripts/make-release.sh --adhoc     # local test: ad hoc signature, no notarizing
#
#   --team      Apple team ID used for the build (default: from the identity)
#   --identity  codesign identity (default: "Developer ID Application")
#   --profile   notarytool keychain profile (default: splatthumb), created once with
#               xcrun notarytool store-credentials splatthumb --apple-id <id> --team-id <team>
#   --no-notarize  sign with Developer ID but skip notarizing (for checking the signature)
#   --adhoc     ad hoc signature, no notarizing; only runs on this Mac
#
# Output goes to build/macos-release/. Run from anywhere inside the repository.
set -euo pipefail

identity="Developer ID Application"
profile="splatthumb"
team=""
notarize=1
adhoc=0
while [[ $# -gt 0 ]]; do
    case "$1" in
    --team) team="$2"; shift 2 ;;
    --identity) identity="$2"; shift 2 ;;
    --profile) profile="$2"; shift 2 ;;
    --no-notarize) notarize=0; shift ;;
    --adhoc) adhoc=1; notarize=0; identity="-"; shift ;;
    -h | --help) sed -n '2,19p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

fail() { echo "error: $*" >&2; exit 1; }
step() { echo; echo "== $*"; }

root="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$root"
version="$(sed -nE 's/^project\(SplatThumb VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)"
[[ -n "$version" ]] || fail "could not read the version from CMakeLists.txt"
out="build/macos-release"
build="$out/xcode"
app="$out/SplatThumb.app"
dmg="$out/SplatThumb-$version-macos.dmg"

# --- checks before the long build ------------------------------------------------
if [[ $adhoc -eq 0 ]]; then
    match="$(security find-identity -v -p codesigning | grep -F "\"$identity" | head -1 || true)"
    [[ -n "$match" ]] || fail "no codesigning identity matching \"$identity\" in the keychain.
       Create a Developer ID Application certificate in Xcode → Settings → Accounts →
       Manage Certificates (the paid team), or test with --adhoc."
    identity="$(sed -E 's/^.*"(.*)"$/\1/' <<<"$match")"
    if [[ -z "$team" ]]; then
        team="$(sed -nE 's/.*\(([A-Z0-9]{10})\)$/\1/p' <<<"$identity")"
    fi
    [[ -n "$team" ]] || fail "could not tell the team ID from \"$identity\"; pass --team"
    if [[ $notarize -eq 1 ]]; then
        xcrun notarytool history --keychain-profile "$profile" >/dev/null 2>&1 ||
            fail "no notarytool keychain profile \"$profile\". Create it once with
       xcrun notarytool store-credentials $profile --apple-id <Apple ID> --team-id $team
       (it asks for an app-specific password), or use --no-notarize."
    fi
fi
echo "SplatThumb $version for macOS"
echo "identity: $identity${team:+ (team $team)}; notarize: $([[ $notarize -eq 1 ]] && echo yes || echo no)"

# --- build -------------------------------------------------------------------------
step "Build (universal, Release)"
rm -rf "$out"
cmake -S macos -B "$build" -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" >/dev/null
# Built ad hoc; the release signature is applied below, so the build never needs the
# team's certificates or provisioning.
cmake --build "$build" --config Release --target SplatThumb -- -quiet
ditto "$build/Release/SplatThumb.app" "$app"
for bin in "$app/Contents/MacOS/SplatThumb" \
           "$app/Contents/PlugIns/SplatThumbThumbnail.appex/Contents/MacOS/SplatThumbThumbnail"; do
    archs="$(lipo -archs "$bin")"
    [[ "$archs" == *arm64* && "$archs" == *x86_64* ]] || fail "$bin is not universal ($archs)"
    echo "$(basename "$bin"): $archs"
done

# --- sign, inside out ----------------------------------------------------------------
step "Sign"
sign_flags=(--force --options runtime)
[[ $adhoc -eq 1 ]] || sign_flags+=(--timestamp)
codesign "${sign_flags[@]}" --sign "$identity" \
    --entitlements macos/thumbnail/SplatThumbThumbnail.entitlements \
    "$app/Contents/PlugIns/SplatThumbThumbnail.appex"
codesign "${sign_flags[@]}" --sign "$identity" \
    --entitlements macos/app/SplatThumb.entitlements \
    "$app"
codesign --verify --strict --deep --verbose=2 "$app"
codesign -dv "$app" 2>&1 | grep -E "^(Identifier|TeamIdentifier|Timestamp|CodeDirectory)" || true

# --- disk image ----------------------------------------------------------------------
step "Disk image"
stage="$out/dmg"
mkdir -p "$stage"
ditto "$app" "$stage/SplatThumb.app"
ln -s /Applications "$stage/Applications"
hdiutil create -quiet -volname "SplatThumb $version" -srcfolder "$stage" -fs HFS+ \
    -format UDZO -ov "$dmg"
rm -rf "$stage"
if [[ $adhoc -eq 0 ]]; then
    codesign --force --timestamp --sign "$identity" "$dmg"
fi

# --- notarize ------------------------------------------------------------------------
if [[ $notarize -eq 1 ]]; then
    step "Notarize (this can take a few minutes)"
    xcrun notarytool submit "$dmg" --keychain-profile "$profile" --wait
    xcrun stapler staple "$dmg"
    xcrun stapler validate "$dmg"
fi

# --- check as a downloaded file would be ---------------------------------------------
step "Gatekeeper check"
if [[ $notarize -eq 1 ]]; then
    spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
    mnt="$(mktemp -d)"
    hdiutil attach -quiet -nobrowse -readonly -mountpoint "$mnt" "$dmg"
    spctl --assess --type execute --verbose=2 "$mnt/SplatThumb.app" || status=$?
    hdiutil detach -quiet "$mnt"
    [[ ${status:-0} -eq 0 ]] || fail "Gatekeeper rejected the app"
else
    echo "skipped: not notarized, so Gatekeeper would reject it on other Macs"
fi

(cd "$out" && shasum -a 256 "$(basename "$dmg")" >"$(basename "$dmg").sha256")
step "Done"
ls -l "$dmg" "$dmg.sha256"
