#!/bin/bash
set -e
echo "Setting up third-party dependencies..."

# --------------------------------------------------------------
# Qt6 via aqtinstall (Matches Windows path)
# --------------------------------------------------------------
echo "Checking for Qt6..."
if [ ! -d "third_party/Qt/6.7.0/gcc_64/lib/cmake/Qt6" ]; then
    echo "Downloading Qt6 via aqtinstall..."
    pip3 install aqtinstall --quiet
    python3 -m aqt install-qt linux desktop 6.7.0 linux_gcc_64 \
        --outputdir third_party/Qt
else
    echo "Qt6 (aqt) already exists. Skipping."
fi

mkdir -p third_party
cd third_party

# 1. Asio (Header-only library)
echo "Fetching Asio..."
if [ ! -d "asio" ]; then
    git clone --depth 1 --branch asio-1-30-2 https://github.com/chriskohlhoff/asio.git asio_src
    mv asio_src/asio ./asio
    rm -rf asio_src
else
    echo "Asio already exists. Skipping."
fi

# 2. RtAudio
echo "Fetching RtAudio..."
if [ ! -d "rtaudio" ]; then
    git clone --depth 1 https://github.com/thestk/rtaudio.git
else
    echo "RtAudio already exists. Skipping."
fi

# 3. Opus
echo "Fetching Opus..."
if [ ! -d "opus" ]; then
    git clone --depth 1 https://github.com/xiph/opus.git
else
    echo "Opus already exists. Skipping."
fi

# 4. SQLite3
echo "Fetching SQLite3..."
if [ ! -d "sqlite3" ]; then
    mkdir -p sqlite3
    curl -L -o sqlite3.zip https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip
    unzip -q sqlite3.zip
    mv sqlite-amalgamation-3450300/sqlite3.c sqlite3/
    mv sqlite-amalgamation-3450300/sqlite3.h sqlite3/
    mv sqlite-amalgamation-3450300/sqlite3ext.h sqlite3/
    rm -rf sqlite-amalgamation-3450300 sqlite3.zip
else
    echo "SQLite3 already exists. Skipping."
fi

# 5. spdlog
echo "Fetching spdlog..."
if [ ! -d "spdlog" ]; then
    git clone --depth 1 --branch v1.13.0 https://github.com/gabime/spdlog.git
else
    echo "spdlog already exists. Skipping."
fi

echo "---------------------------------------------------"
echo "All dependencies downloaded successfully!"
echo "---------------------------------------------------"