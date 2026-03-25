cmake_minimum_required(VERSION 3.15)
project(VoiceChatApp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ------------------------------------------------------------------------------
# Compile Third-Party Libraries
# We turn off tests and shared libraries to keep your build fast and static.
# ------------------------------------------------------------------------------
set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "Disable RtAudio tests" FORCE)
set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "Build RtAudio statically" FORCE)
add_subdirectory(third_party/rtaudio)

set(OPUS_BUILD_SHARED OFF CACHE BOOL "Build Opus statically" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "Disable Opus tests" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "Disable Opus programs" FORCE)
add_subdirectory(third_party/opus)

# ------------------------------------------------------------------------------
# Core Application Setup
# ------------------------------------------------------------------------------
set(SOURCES
    src/Main.cpp
    src/Core/Application.cpp
    src/Core/ChannelState.cpp
    src/Network/NetworkManager.cpp
    src/Network/TailscaleNetwork.cpp
    src/Audio/AudioEngine.cpp
    src/UI/WindowManager.cpp
)

add_executable(VoiceChatApp ${SOURCES})

target_include_directories(VoiceChatApp PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/asio/include
    ${CMAKE_SOURCE_DIR}/third_party/rtaudio
    ${CMAKE_SOURCE_DIR}/third_party/opus/include
    ${CMAKE_SOURCE_DIR}/third_party/imgui
)

# ------------------------------------------------------------------------------
# Platform-Specific Linking
# ------------------------------------------------------------------------------

if(UNIX AND NOT APPLE)
    message(STATUS "Configuring for Linux with partial static linking...")
    
    target_link_options(VoiceChatApp PRIVATE "-static-libstdc++" "-static-libgcc")
    
    find_package(Threads REQUIRED)
    
    # Link the newly compiled static libraries (rtaudio, opus) 
    # and the system-level hardware interfaces
    target_link_libraries(VoiceChatApp PRIVATE 
        Threads::Threads 
        asound 
        pulse 
        pulse-simple
        rtaudio
        opus
    )

elseif(WIN32)
    message(STATUS "Configuring for Windows...")
    
    target_link_libraries(VoiceChatApp PRIVATE 
        ws2_32 
        wsock32
        rtaudio
        opus
    )
endif()