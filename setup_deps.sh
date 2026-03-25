#!/bin/bash

# Exit immediately if a command fails
set -e

echo "Setting up third-party dependencies..."

# Create the directory if it doesn't exist
mkdir -p third_party
cd third_party

# 1. Asio (Header-only library)
echo "Fetching Asio..."
git clone --depth 1 --branch asio-1-30-2 https://github.com/chriskohlhoff/asio.git asio_src
mv asio_src/asio ./asio
rm -rf asio_src

# 2. RtAudio
echo "Fetching RtAudio..."
git clone --depth 1 https://github.com/thestk/rtaudio.git

# 3. Opus
echo "Fetching Opus..."
git clone --depth 1 https://github.com/xiph/opus.git

# 4. ImGui
echo "Fetching ImGui..."
git clone --depth 1 --branch v1.90.4 https://github.com/ocornut/imgui.git

echo "---------------------------------------------------"
echo "All dependencies downloaded successfully!"
echo "---------------------------------------------------"
