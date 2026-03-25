@echo off
echo Setting up third-party dependencies for Windows...

:: Create the directory if it doesn't exist
if not exist third_party (
    mkdir third_party
)
cd third_party

:: 1. Asio (Header-only library)
echo Fetching Asio...
if not exist asio (
    git clone --depth 1 --branch asio-1-30-2 https://github.com/chriskohlhoff/asio.git asio_src
    :: Move the inner asio folder out and delete the rest
    move asio_src\asio .\asio >nul
    rmdir /s /q asio_src
) else (
    echo Asio already exists. Skipping.
)

:: 2. RtAudio
echo Fetching RtAudio...
if not exist rtaudio (
    git clone --depth 1 https://github.com/thestk/rtaudio.git
) else (
    echo RtAudio already exists. Skipping.
)

:: 3. Opus
echo Fetching Opus...
if not exist opus (
    git clone --depth 1 https://github.com/xiph/opus.git
) else (
    echo Opus already exists. Skipping.
)

:: 4. ImGui
echo Fetching ImGui...
if not exist imgui (
    git clone --depth 1 --branch v1.90.4 https://github.com/ocornut/imgui.git
) else (
    echo ImGui already exists. Skipping.
)

echo ---------------------------------------------------
echo All dependencies downloaded successfully!
echo ---------------------------------------------------
pause