#!/bin/bash
# Quick ARM64 JIT test using Docker + QEMU
# Usage: ./test_arm64_jit.sh [demo.bin]

set -e

DEMO_FILE="${1:-build_qt/gradient_demo.bin}"

if [ ! -f "$DEMO_FILE" ]; then
    echo "Error: Demo file not found: $DEMO_FILE"
    echo "Usage: $0 [demo.bin]"
    exit 1
fi

echo "=== Testing ARM64 JIT with QEMU ==="
echo "Demo file: $DEMO_FILE"
echo ""

# Enable Docker multi-platform support (only needed once, but safe to re-run)
echo "Enabling Docker multi-platform support..."
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

echo ""
echo "Building and testing ARM64 binary..."
echo ""

# Build and run ARM64 version
docker run --rm --platform linux/arm64 \
  -v $(pwd):/work -w /work \
  ubuntu:22.04 bash -c "
# Install dependencies
export DEBIAN_FRONTEND=noninteractive
apt update -qq && apt install -y -qq build-essential cmake libsdl2-dev git pkg-config > /dev/null 2>&1

echo '=== Building for ARM64 ==='
mkdir -p build_arm64 && cd build_arm64
cmake -DCMAKE_BUILD_TYPE=Release .. 2>&1 | grep -E 'Configuring done|Generating done|Build files'
cmake --build . -j 2>&1 | tail -20

echo ''
echo '=== Running JIT Test (ARM64) ==='
echo 'Architecture:'
uname -m
echo ''

# Run test_jit (non-graphical JIT test)
if [ -f bin/test_jit ]; then
    echo 'Running test_jit with JIT enabled...'
    ./bin/test_jit /work/$DEMO_FILE --jit

    echo ''
    echo 'Running test_jit with interpreter for comparison...'
    ./bin/test_jit /work/$DEMO_FILE
else
    echo 'test_jit not found in bin/'
    ls -la bin/ | head -20
fi
"

echo ""
echo "=== ARM64 JIT Test Complete ==="
