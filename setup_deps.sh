#!/bin/bash
set -e
echo "Setting up third-party dependencies..."

# --------------------------------------------------------------
# Qt6 via apt (covers Ubuntu 22.04+ and Debian 12+)
# On older distros that don't have qt6 packages, the aqtinstall
# fallback block below will run instead.
# --------------------------------------------------------------
echo "Checking for Qt6..."

QT_APT_PACKAGES=(
    qt6-base-dev
    qt6-declarative-dev
    libqt6qml6
    qml6-module-qtquick
    qml6-module-qtquick-controls
    qml6-module-qtquick-layouts
    qml6-module-qtquick-templates
    qt6-qmake
)

# Check if qt6-base-dev is available in the package manager
if apt-cache show qt6-base-dev &>/dev/null; then
    echo "Installing Qt6 via apt..."
    sudo apt-get update -qq
    sudo apt-get install -y "${QT_APT_PACKAGES[@]}"
else
    # Fallback: aqtinstall into third_party/Qt (same as Windows path)
    echo "qt6-base-dev not found in apt — falling back to aqtinstall..."
    if [ ! -d "third_party/Qt/6.7.0/gcc_64/lib/cmake/Qt6" ]; then
        pip3 install aqtinstall --quiet
        python3 -m aqt install-qt linux desktop 6.7.0 linux_gcc_64 \
            --outputdir third_party/Qt \
            --modules qtquickcontrols2
    else
        echo "Qt6 (aqt) already exists. Skipping."
    fi
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