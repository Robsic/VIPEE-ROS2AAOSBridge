# Toolchain do ROS 2 para Android x86_64 (emulador). Igual ao do handoff
# (Refs/handoff-ros2-jazzy-aaos.md §4.1), trocando só o ANDROID_ABI.
set(ANDROID_ABI x86_64 CACHE STRING "" FORCE)
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
