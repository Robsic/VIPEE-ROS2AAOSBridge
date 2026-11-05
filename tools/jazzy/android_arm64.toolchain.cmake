# Toolchain do ROS 2 para Android arm64-v8a (RPi4/RPi5). Igual ao do handoff
# (Refs/handoff-ros2-jazzy-aaos.md §4.1), trocando o ANDROID_ABI e linkando com páginas de 16 KB.
set(ANDROID_ABI arm64-v8a CACHE STRING "" FORCE)
set(ANDROID_PLATFORM android-34 CACHE STRING "" FORCE)
set(ANDROID_STL c++_shared CACHE STRING "" FORCE)
if(NOT DEFINED ENV{ANDROID_NDK_HOME})
  message(FATAL_ERROR "ANDROID_NDK_HOME is not set")
endif()
include("$ENV{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake")
# O toolchain do NDK restringe find_* ao sysroot (ONLY); as ferramentas ament_cmake
# do host ficam fora do sysroot -> BOTH.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
# Páginas de 16 KB (AAOS 16 no RPi4 rejeita LOAD desalinhado).
string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,-z,max-page-size=16384")
string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,-z,max-page-size=16384")
