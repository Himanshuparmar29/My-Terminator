#!/bin/bash
set -e

echo "=========================================================="
echo "          Running Terminator Automated Test Suite         "
echo "=========================================================="

echo "[1/3] Building Terminator and Test Targets..."
make clean
make
make test

echo ""
echo "[2/3] Verifying Zero Comments Rule..."
if grep -En '//|\/\*|\*\/' src/*.c src/*.h tests/test_suite.c tests/test_modes.c tests/test_harness.c; then
    echo "[FAIL] Comments detected in C source or header files!"
    exit 1
else
    echo "[PASS] Verified: Zero comments in all C source and header files."
fi

echo ""
echo "[3/3] Running X11 GUI Launch Verification..."
python3 tests/test_x11_gui.py

echo ""
echo "=========================================================="
echo "          ALL TESTS PASSED SUCCESSFULLY!                  "
echo "=========================================================="
