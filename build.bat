@echo off
echo Starting build process for VoiceChatApp on Windows...

:: Check if the build directory exists, create it if it does not
if not exist build (
    echo Creating build directory...
    mkdir build
)

:: Navigate into the build directory
cd build

:: Run CMake to generate the Visual Studio solution files
echo Configuring CMake...
cmake ..

:: Compile the C++ code in Release mode
echo Compiling application...
cmake --build . --config Release

echo ---------------------------------------------------
echo Build successful! 
echo Run the app from the project root using: .\build\Release\VoiceChatApp.exe ^<TargetTailscaleIP^>
echo ---------------------------------------------------
pause