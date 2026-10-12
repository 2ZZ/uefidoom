#!/usr/bin/env bash
# Builds doom.efi in Docker and writes it to ./out/doom.efi
# Usage: ./build.sh [RELEASE|DEBUG]
set -euo pipefail

cd "$(dirname "$0")"

IMAGE=uefidoom-build
TARGET="${1:-RELEASE}"

docker build -t "$IMAGE" .

mkdir -p out
docker run --rm \
	-e TARGET="$TARGET" \
	-v "$PWD:/src/uefidoom:ro" \
	-v "$PWD/out:/out" \
	"$IMAGE"

echo "Output: out/doom.efi"
