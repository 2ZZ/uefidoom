#!/usr/bin/env bash
# Runs inside the container: builds doom.efi and copies it to /out.
set -euo pipefail

TARGET="${TARGET:-RELEASE}"
ARCH="${ARCH:-X64}"
TOOLCHAIN=GCC5

if [ "$(uname -m)" != "x86_64" ]; then
	export GCC5_BIN=x86_64-linux-gnu-
fi

mkdir -p "$WORKSPACE"
cd "$WORKSPACE"
set +u
source /opt/edk2/edksetup.sh BaseTools
set -u

build -p uefidoom/uefidoom.dsc -a "$ARCH" -t "$TOOLCHAIN" -b "$TARGET" -n "$(nproc)"

cp "$WORKSPACE/Build/DoomPkg/${TARGET}_${TOOLCHAIN}/${ARCH}/doom.efi" /out/doom.efi
echo "Built /out/doom.efi"
