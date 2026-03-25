#!/bin/bash

# Exit the script immediately if any command fails
set -e

echo "Starting build process for VoiceChatApp..."

# Check if the build directory exists, create it if it does not
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

# Navigate into the build directory
cd build

# Run CMake to generate the required Makefiles based on your CMakeLists.txt
echo "Configuring CMake..."
cmake ..

# Compile the actual C++ code using all available CPU cores for speed
echo "Compiling application..."
make -j$(nproc)

echo "---------------------------------------------------"
echo "Build successful! "
echo "Run the app from the project root using: ./build/VoiceChatApp <TargetTailscaleIP>"
echo "---------------------------------------------------"
