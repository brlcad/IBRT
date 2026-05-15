#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/.build}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"
RENDER_WORKER="${IBRT_ENABLE_RENDER_WORKER:-ON}"

: "${BEXT_INSTALL_DIR:?set BEXT_INSTALL_DIR to the bext/install tree}"
: "${BRLCAD_PREFIX:?set BRLCAD_PREFIX to the BRL-CAD install prefix}"

cmake -S "$ROOT" \
  -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DBEXT_INSTALL_DIR="$BEXT_INSTALL_DIR" \
  -DBRLCAD_PREFIX="$BRLCAD_PREFIX" \
  -DIBRT_ENABLE_RENDER_WORKER="$RENDER_WORKER"

cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure
