#!/usr/bin/env bash
# Builds and runs the native unit tests, the parts of the core that need no Flutter, no
# adapter and no phone: the adapter protocol's encoder and decoder, and the stream
# parser. Prints OK and exits 0 when every test passes.
#
#   tools/native-test.sh
#
# Needs only a C++17 compiler and CMake. The build goes to native-test-build/ at the
# repository root, which is ignored by git.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$REPO/native-test-build"

GENERATOR=()
if command -v ninja >/dev/null; then
  GENERATOR=(-G Ninja)
fi

cmake -S "$REPO/packages/carplay_native/native_test" -B "$BUILD" "${GENERATOR[@]}" >/dev/null
cmake --build "$BUILD" >/dev/null
ctest --test-dir "$BUILD" --output-on-failure
echo OK
