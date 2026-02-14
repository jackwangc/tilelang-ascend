#!/bin/bash
# Build script for AscendC kernel compilation using bisheng compiler

# ============================================================================
# Configuration
# ============================================================================

# Source file (input .cpp file)
# Usage: ./build.sh <source_file>
# Default: kernel.cpp
SRC_FILE="${1:-kernel.cpp}"

# Output library file
#LIB_FILE="${SRC_FILE%.cpp}.so"
LIB_FILE="ascendc.so"

# Environment variables (can be overridden)
ASCEND_HOME_PATH="${ASCEND_HOME_PATH:-/usr/local/Ascend/ascend-toolkit/latest}"
TL_ROOT="${TL_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
TILELANG_TEMPLATE_PATH="${TILELANG_TEMPLATE_PATH:-$TL_ROOT/src}"

# NPU architecture
NPU_ARCH="${NPU_ARCH:-dav-2201}"

# ============================================================================
# Compilation
# ============================================================================

echo "=========================================="
echo "AscendC Kernel Build Script"
echo "=========================================="
echo "Source file:    $SRC_FILE"
echo "Output library: $LIB_FILE"
echo "NPU Arch:       $NPU_ARCH"
echo "ASCEND_HOME:    $ASCEND_HOME_PATH"
echo "TL_ROOT:        $TL_ROOT"
echo "=========================================="

bisheng \
    --npu-arch="$NPU_ARCH" \
    -O2 \
    -std=c++17 \
    -xasc \
    -I"$ASCEND_HOME_PATH/include" \
    -I"$ASCEND_HOME_PATH/include/experiment/msprof" \
    -I"$ASCEND_HOME_PATH/include/experiment/runtime" \
    -I"$ASCEND_HOME_PATH/pkg_inc" \
    -I"$ASCEND_HOME_PATH/pkg_inc/runtime" \
    -I"$ASCEND_HOME_PATH/pkg_inc/profiling" \
    -I"$TL_ROOT/3rdparty/catlass/include" \
    -I"$TL_ROOT/3rdparty/shmem/include" \
    -I"$TL_ROOT/3rdparty/shmem/src/device" \
    -DBACKEND_HYBM \
    -I"$TILELANG_TEMPLATE_PATH" \
    -L"$ASCEND_HOME_PATH/lib64" \
    -Wno-macro-redefined \
    -Wno-ignored-attributes \
    -Wno-non-c-typedef-for-linkage \
    -lruntime \
    -lascendcl \
    -lm \
    -ltiling_api \
    -lplatform \
    -lc_sec \
    -ldl \
    -fPIC \
    --shared \
    "$SRC_FILE" \
    -o "$LIB_FILE"

# Check compilation result
if [ $? -eq 0 ]; then
    echo "=========================================="
    echo "Build successful! Output: $LIB_FILE"
    echo "=========================================="
    exit 0
else
    echo "=========================================="
    echo "Build failed!"
    echo "=========================================="
    exit 1
fi
