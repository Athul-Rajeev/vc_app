cmake_minimum_required(VERSION 3.22)
project(VoiceChatApp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# Qt ships as DLL-runtime libs on Windows, so the project must match.
# MultiThreadedDLL (Release) / MultiThreadedDebugDLL (Debug).
# Using the static runtime causes LNK4098 conflicts with Qt.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# ------------------------------------------------------------------------------
# Locate Qt6 — checks (in order):
#   1. CMAKE_PREFIX_PATH passed on the command line (always wins)
#   2. Qt installed locally into third_party/Qt by setup_deps
#   3. System-wide Qt installation (Linux apt, or Qt installer default paths)
# ------------------------------------------------------------------------------
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH
        "${CMAKE_SOURCE_DIR}/third_party/Qt/6.7.0/msvc2019_64"
        "C:/Qt/6.7.0/msvc2019_64"
        "C:/Qt/6.8.0/msvc2022_64"
        "$ENV{USERPROFILE}/Qt/6.7.0/msvc2019_64"
    )
elseif(UNIX AND NOT APPLE)
    list(APPEND CMAKE_PREFIX_PATH
        "${CMAKE_SOURCE_DIR}/third_party/Qt/6.7.0/gcc_64"
        "$ENV{HOME}/Qt/6.7.0/gcc_64"
        "/opt/Qt/6.7.0/gcc_64"
    )
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Qml
    Quick
    QuickControls2
)

message(STATUS "Found Qt6: ${Qt6_VERSION} at ${Qt6_DIR}")

qt_standard_project_setup()

# ------------------------------------------------------------------------------
# Third-Party Libraries
# ------------------------------------------------------------------------------
set(RTAUDIO_BUILD_TESTING     OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(WIN32)
    set(RTAUDIO_API_PULSE  OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_ALSA   OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_WASAPI ON  CACHE BOOL "" FORCE)
    set(RTAUDIO_API_DS     ON  CACHE BOOL "" FORCE)
elseif(UNIX AND NOT APPLE)
    set(RTAUDIO_API_PULSE  ON  CACHE BOOL "" FORCE)
    set(RTAUDIO_API_ALSA   OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_WASAPI OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_DS     OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(third_party/rtaudio)

set(OPUS_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING  OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/opus)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------
set(SOURCES
    src/Main.cpp
    src/Core/Application.cpp
    src/Core/ChannelState.cpp
    src/Network/NetworkManager.cpp
    src/Network/TailscaleNetwork.cpp
    src/Network/TcpSession.cpp
    src/Audio/AudioEngine.cpp
    src/Audio/PeerMixer.cpp
    src/UI/WindowManager.cpp
    src/UI/DiscordBackend.cpp
    src/Database/DatabaseManager.cpp
    src/Utils/LoggerSetup.cpp
    third_party/sqlite3/sqlite3.c
    # ── Headers with Q_OBJECT must be listed here so AUTOMOC finds them.
    # AUTOMOC scans sources it knows about; headers in include/ are invisible
    # to it unless explicitly added. Only Q_OBJECT headers need to be listed.
    include/UI/DiscordBackend.hpp
    include/UI/WindowManager.hpp
)

set(QML_RESOURCES
    src/UI/qml/discord.qrc
)

# ------------------------------------------------------------------------------
# Executable
# ------------------------------------------------------------------------------
add_executable(VoiceChatApp
    ${SOURCES}
    ${QML_RESOURCES}
)

qt_import_qml_plugins(VoiceChatApp)

# ------------------------------------------------------------------------------
# Include paths
# ------------------------------------------------------------------------------
target_include_directories(VoiceChatApp PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/asio/include
    ${CMAKE_SOURCE_DIR}/third_party/rtaudio
    ${CMAKE_SOURCE_DIR}/third_party/opus/include
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3
    ${CMAKE_SOURCE_DIR}/third_party/spdlog/include
)

# ------------------------------------------------------------------------------
# Platform linking
# ------------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    message(STATUS "Configuring for Linux...")

    target_link_options(VoiceChatApp PRIVATE
        "-static-libstdc++"
        "-static-libgcc"
    )

    find_package(Threads REQUIRED)

    target_link_libraries(VoiceChatApp PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Threads::Threads
        asound
        pulse
        pulse-simple
        rtaudio
        opus
        dl
    )

elseif(WIN32)
    message(STATUS "Configuring for Windows...")

    target_compile_definitions(VoiceChatApp PRIVATE
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00
    )

    set_target_properties(VoiceChatApp PROPERTIES
        WIN32_EXECUTABLE FALSE
    )

    target_link_libraries(VoiceChatApp PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        ws2_32
        wsock32
        rtaudio
        opus
    )
endif()

# ------------------------------------------------------------------------------
# Post-build
# ------------------------------------------------------------------------------
if(WIN32)
    add_custom_command(TARGET VoiceChatApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:opus>
            $<TARGET_FILE_DIR:VoiceChatApp>
        COMMENT "Copying opus.dll..."
    )

    find_program(WINDEPLOYQT windeployqt
        HINTS
            "${CMAKE_SOURCE_DIR}/third_party/Qt/6.7.0/msvc2019_64/bin"
            "C:/Qt/6.7.0/msvc2019_64/bin"
            "C:/Qt/6.8.0/msvc2022_64/bin"
    )
    if(WINDEPLOYQT)
        add_custom_command(TARGET VoiceChatApp POST_BUILD
            COMMAND ${WINDEPLOYQT}
                --qmldir "${CMAKE_SOURCE_DIR}/src/UI/qml"
                --no-translations
                "$<TARGET_FILE:VoiceChatApp>"
            COMMENT "Running windeployqt..."
        )
    else()
        message(WARNING "windeployqt not found — you may need to copy Qt DLLs manually.")
    endif()
endif()