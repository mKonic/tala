#!/usr/bin/env bash
# The one place tala's version comes from. Both numbers come out of git;
# nothing is declared in a file.
#
#   code   10000 + N, N = commits on HEAD. Monotonic, says which build is newer.
#   name   git describe: "v1.0.0" at the tag, "v1.0.0-12-gabc1234" past it,
#          the bare hash before the first tag. Releasing is tagging.
#
#   scripts/version.sh [code|name|both]      default: both
#   scripts/version.sh header OUT            write a C++ header, only if changed
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"

# 0 / "unknown" when git cannot answer: never a plausible-looking number.
code=0
name="unknown"

if git -C "$HERE" rev-parse --git-dir >/dev/null 2>&1; then
    # A shallow clone counts wrong and yields a lower number than the last build.
    if [ -f "$(git -C "$HERE" rev-parse --git-dir)/shallow" ]; then
        echo "version.sh: shallow clone -- 'git fetch --unshallow' for a real build number" >&2
    else
        commits=$(git -C "$HERE" rev-list --count HEAD 2>/dev/null || echo 0)
        [ "$commits" -gt 0 ] && code=$((10000 + commits))
    fi
    name=$(git -C "$HERE" describe --tags --match 'v[0-9]*' --always --dirty 2>/dev/null || echo unknown)
fi

case "${1:-both}" in
    code) echo "$code" ;;
    name) echo "$name" ;;
    both) echo "$code $name" ;;
    # pacman's order: 1.2.r3.gabc1234 after a tag, r<commits>.g<hash> before one.
    pkgver)
        if desc=$(git -C "$HERE" describe --tags --long --match 'v[0-9]*' 2>/dev/null); then
            echo "$desc" | sed 's/^v//; s/-\([0-9]*\)-g/.r\1.g/'
        else
            printf 'r%s.g%s\n' "$(git -C "$HERE" rev-list --count HEAD)" "$(git -C "$HERE" rev-parse --short HEAD)"
        fi
        ;;
    header)
        out="${2:?header needs an output path}"
        tmp="$out.tmp"
        printf '#pragma once\n#define TALA_VERSION "%s"\n#define TALA_BUILD %s\n' "$name" "$code" > "$tmp"
        if cmp -s "$tmp" "$out"; then rm -f "$tmp"; else mv "$tmp" "$out"; fi
        ;;
    *) echo "usage: $0 [code|name|both|pkgver|header OUT]" >&2; exit 2 ;;
esac
