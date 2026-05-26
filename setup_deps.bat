@echo off
echo Setting up third-party dependencies for Windows...

if not exist third_party (
    mkdir third_party
)

:: --------------------------------------------------------------
:: Detect which Python to use.
:: Try 'python' first (could be MSYS2 or python.org depending on PATH).
:: If it resolves to the MSYS2 one we detect that by checking the path.
:: Then try 'py' (Windows Python Launcher, always python.org CPython).
:: Store the winner in PYEXE.
:: --------------------------------------------------------------
set PYEXE=

:: Try 'py' (Windows Python Launcher) first — most reliable on Windows
where py >nul 2>&1
if not errorlevel 1 (
    set PYEXE=py -3
    goto :python_found
)

:: Fall back to 'python', but reject it if it's the MSYS2 build
:: (MSYS2 Python lives under msys64 and can't build wheels correctly)
where python >nul 2>&1
if not errorlevel 1 (
    python -c "import sys; exit(0 if 'msys' not in sys.executable.lower() and 'mingw' not in sys.executable.lower() else 1)" >nul 2>&1
    if not errorlevel 1 (
        set PYEXE=python
        goto :python_found
    ) else (
        echo WARNING: 'python' resolved to MSYS2 Python, skipping it.
    )
)

echo.
echo ERROR: No suitable Python found.
echo Install Python 3.10-3.13 from https://python.org
echo ^(Python 3.14 may not yet have pre-built wheels for all dependencies^)
echo.
pause
exit /b 1

:python_found
echo Using Python: %PYEXE%

:: --------------------------------------------------------------
:: Qt6 via aqtinstall
:: --prefer-binary  = use wheels when available, allow pure-Python
::                    source dists, but never compile C extensions.
:: --------------------------------------------------------------
echo Checking for Qt6...
if not exist third_party\Qt\6.7.0\msvc2019_64\lib\cmake\Qt6 (
    echo Installing Qt6 via aqtinstall...

    %PYEXE% -m pip install "aqtinstall==3.1.9" --prefer-binary --quiet
    if errorlevel 1 (
        echo ERROR: Failed to install aqtinstall.
        echo.
        echo If you are on Python 3.14, it may not yet have wheels for all
        echo aqtinstall dependencies. Try installing Python 3.12 from python.org
        echo alongside your current install — 'py -3.12 -m pip install ...' will
        echo pick it up automatically.
        pause
        exit /b 1
    )

    %PYEXE% -m aqt install-qt windows desktop 6.7.0 win64_msvc2019_64 ^
        --outputdir third_party\Qt
    if errorlevel 1 (
        echo ERROR: Qt6 download failed. Check your internet connection.
        pause
        exit /b 1
    )
) else (
    echo Qt6 already exists. Skipping.
)

cd third_party

:: 1. Asio
echo Fetching Asio...
if not exist asio (
    git clone --depth 1 --branch asio-1-30-2 https://github.com/chriskohlhoff/asio.git asio_src
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

:: 4. SQLite3
echo Fetching SQLite3...
if not exist sqlite3 (
    mkdir sqlite3
    curl -L -o sqlite3.zip https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip
    tar -xf sqlite3.zip
    move sqlite-amalgamation-3450300\sqlite3.c sqlite3\ >nul
    move sqlite-amalgamation-3450300\sqlite3.h sqlite3\ >nul
    move sqlite-amalgamation-3450300\sqlite3ext.h sqlite3\ >nul
    rmdir /s /q sqlite-amalgamation-3450300
    del sqlite3.zip
) else (
    echo SQLite3 already exists. Skipping.
)

:: 5. spdlog
echo Fetching spdlog...
if not exist spdlog (
    git clone --depth 1 --branch v1.13.0 https://github.com/gabime/spdlog.git
) else (
    echo spdlog already exists. Skipping.
)

echo ---------------------------------------------------
echo All dependencies downloaded successfully!
echo ---------------------------------------------------
pause