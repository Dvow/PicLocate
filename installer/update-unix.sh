#!/bin/sh
# Called with separate arguments after a verified download. Never touches the library.
set -eu
test "$#" -eq 5
platform=$1 archive=$2 target=$3 pid=$4 library=$5
case "$platform" in linux-x64|linux-arm64|macos-x64|macos-arm64) ;; *) exit 1 ;; esac
case "$pid" in ''|*[!0-9]*) exit 1 ;; esac
test "$pid" -gt 1
test -f "$archive" && test -d "$target" && test ! -L "$target"
case "$library/" in "$target/"*) exit 1 ;; esac
case "$platform" in
    linux-*) case "$(basename -- "$target")" in PicLocate*) ;; *) exit 1 ;; esac ;;
    macos-*) case "$target" in *.app) ;; *) exit 1 ;; esac ;;
esac
parent=$(dirname -- "$target")
stage=$(mktemp -d "$parent/.piclocate-update.XXXXXX")
backup="$stage/previous"
mounted=''
replaced=0
cleanup() {
    if test -n "$mounted"; then hdiutil detach "$mounted" -quiet || true; fi
    if test -d "$backup" && test "$replaced" -eq 0; then mv -- "$backup" "$target"; fi
    # Keep the previous application for recovery; remove only the staging data we created.
    if test "$replaced" -eq 1; then
        case "$candidate" in "$stage/"*) rm -rf -- "$candidate" ;; *) exit 1 ;; esac
        rm -f -- "$stage/entries"
        rmdir "$stage/mount" 2>/dev/null || true
    elif test ! -d "$backup" && test -z "$mounted"; then
        rm -rf -- "$stage"
    fi
}
trap cleanup EXIT HUP INT TERM
case "$platform" in
    linux-*)
        # CPack wraps every entry in a single versioned directory.
        tar -tzf "$archive" > "$stage/entries"
        if ! awk '/^\// || /(^|\/)\.\.(\/|$)/ {bad=1} END {exit bad}' "$stage/entries"; then exit 1; fi
        tar -xzf "$archive" -C "$stage"
        set -- "$stage"/PicLocate-*-linux-*
        test "$#" -eq 1
        candidate=$1
        test -x "$candidate/bin/PicLocate"
        test "$(cat "$candidate/share/PicLocate/.piclocate-package")" = "$platform"
        ;;
    macos-*)
        mkdir "$stage/mount"
        hdiutil attach "$archive" -nobrowse -readonly -mountpoint "$stage/mount" -quiet
        mounted="$stage/mount"
        candidate="$stage/PicLocate.app"
        ditto "$mounted/PicLocate.app" "$candidate"
        test -x "$candidate/Contents/MacOS/PicLocate"
        test "$(cat "$candidate/Contents/Resources/.piclocate-package")" = "$platform"
        hdiutil detach "$mounted" -quiet
        mounted=''
        ;;
esac
# Copy extra files as well, so a portable installation never discards user-created files.
# The new payload overwrites the old app inside this separate staging tree.
mkdir "$stage/merged"
if command -v ditto >/dev/null 2>&1; then
    ditto "$target" "$stage/merged"
    ditto "$candidate" "$stage/merged"
else
    cp -a "$target/." "$stage/merged/"
    cp -a "$candidate/." "$stage/merged/"
fi
attempt=0
while kill -0 "$pid" 2>/dev/null; do
    test "$attempt" -lt 300
    sleep 1
    attempt=$((attempt + 1))
done
mv -- "$target" "$backup"
if ! mv -- "$stage/merged" "$target"; then exit 1; fi
replaced=1
case "$platform" in
    linux-*) "$target/bin/PicLocate" --data-dir "$library" </dev/null >/dev/null 2>&1 & ;;
    macos-*) open -n "$target" --args --data-dir "$library" ;;
esac
