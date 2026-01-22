#!/bin/bash
set -e

# Check for cmake
if ! command -v cmake &> /dev/null; then
    echo "cmake could not be found"
    exit 1
fi

echo "Building..."
rm -rf build
mkdir build
cd build
cmake ..
make
cd ..

echo "Cleaning old tests..."
rm -rf "manual test"
mkdir -p "manual test"
mkdir -p "manual test/subdir"

echo "Creating test files..."

# Valid FLAC (Stereo)
# 1 second of silence
dd if=/dev/zero bs=44100 count=4 2>/dev/null | flac --silent --force-raw-format --endian=little --sign=signed --channels=2 --bps=16 --sample-rate=44100 -o "manual test/valid.flac" -

# Mono FLAC
# 1 second of silence
dd if=/dev/zero bs=44100 count=2 2>/dev/null | flac --silent --force-raw-format --endian=little --sign=signed --channels=1 --bps=16 --sample-rate=44100 -o "manual test/mono.flac" -

# Unicode filename
cp "manual test/valid.flac" "manual test/Unicode 🎵.flac"

# Garbage file
echo "Not a flac" > "manual test/bad_header.flac"

echo "Running scan..."
./build/MQA_identifier -v "manual test"

echo "Checking log..."
if [ -f mqa_identifier.log ]; then
    cat mqa_identifier.log
else
    echo "Log file not found!"
    exit 1
fi

echo "Running with --dry-run..."
./build/MQA_identifier -v --dry-run "manual test"
