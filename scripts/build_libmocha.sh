#!/usr/bin/env bash
# Build wiiu-env's libmocha into ./portlibs/mocha. It isn't in devkitPro's package index.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PFX="$ROOT/portlibs/mocha"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

git clone --quiet https://github.com/wiiu-env/libmocha.git "$WORK/libmocha"
git -C "$WORK/libmocha" checkout --quiet 50fefdf8307a
make -C "$WORK/libmocha" --no-print-directory >/dev/null

mkdir -p "$PFX"
cp -r "$WORK/libmocha/lib" "$WORK/libmocha/include" "$PFX/"

echo "Built libmocha in $PFX"
