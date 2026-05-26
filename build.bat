@echo off
echo Starting build process for VoiceChatApp on Windows...

:: Remove MSYS2 paths from this session to prevent CMake from finding them
set PATH=%PATH:C:\msys64\ucrt64\bin;=%
set PATH=%PATH:C:\msys64\mingw64\bin;=%
set PATH=%PATH:C:\msys64\usr\bin;=%

:: Automatically locate and initialize the MSVC Developer Command Prompt (including Build Tools)
set "VswherePath=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VswherePath%" (
    for /f "usebackq tokens=*" %%i in (`"%VswherePath%" -latest -products * -property installationPath`) do (
        set "VsInstallPath=%%i"
    )
)

if defined VsInstallPath (
    echo Initializing MSVC environment...
    call "%VsInstallPath%\VC\Auxiliary\Build\vcvars64.bat" >nul
) else (
    echo Error: Visual Studio 2022 or Build Tools not found.
    pause
    exit /b 1
)

:: Clear existing build cache to force CMake to re-evaluate the isolated PATH
if exist build (
    rmdir /s /q build
)
mkdir build
cd build

echo Configuring CMake...
cmake ..

echo Compiling application...
cmake --build . --config Release

echo ---------------------------------------------------
echo Build successful! 
echo Run the app from the project root using: .\build\Release\VoiceChatApp.exe ^<TargetTailscaleIP^>
echo ---------------------------------------------------
pause