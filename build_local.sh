#!/usr/bin/env bash
set -euo pipefail

# Configure and build the CG artifact locally.  CG_BUILD_DIR can point to
# build-portable/build-2pc so local and remote builds stay separate.

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"

cmake -S "$ROOT" -B "$BUILD_DIR"
if [[ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]]; then
    cmake --build "$BUILD_DIR" -j "$CMAKE_BUILD_PARALLEL_LEVEL"
else
    cmake --build "$BUILD_DIR"
fi
