#!/usr/bin/env bash
# Cross-compila o núcleo do ROS 2 Jazzy (até rclcpp + mensagens) para Android
# x86_64, seguindo Refs/handoff-ros2-jazzy-aaos.md §4.2. Rodar no contêiner:
#   tools/jazzy/run.sh /bridge/tools/jazzy/build-android.sh
# Pré-requisito: fetch-src.sh. O patch P1 do libyaml_vendor é aplicado aqui.
set -euo pipefail

ROS2_SRC=/ws/ros2_src
TC="--toolchain /bridge/tools/jazzy/android_x86_64.toolchain.cmake"
EH="--event-handlers console_cohesion+"

# P1 (handoff §4.4): libyaml 0.2.5 pede CMake 3.0 e quebra o toolchain do NDK.
if [ ! -f "$ROS2_SRC/src/ros2/libyaml_vendor/patches/android_cmake_min_version.patch" ]; then
  git -C "$ROS2_SRC/src/ros2/libyaml_vendor" apply /bridge/tools/jazzy/libyaml_vendor-android.patch
fi

# 0) Ferramentas ament/rosidl nativas, cada pacote no seu prefixo (o handoff
#    usa um <ROS2_SRC>/install nativo; aqui só o necessário).
PKGS="ament_package ament_cmake_core ament_cmake ament_cmake_export_definitions \
ament_cmake_export_include_directories ament_cmake_export_libraries ament_cmake_export_link_flags \
ament_cmake_include_directories ament_cmake_libraries ament_cmake_python ament_cmake_version \
ament_cmake_export_dependencies ament_cmake_export_interfaces ament_cmake_export_targets \
ament_cmake_target_dependencies ament_cmake_test ament_cmake_vendor_package ament_cmake_gen_version_h \
ament_cmake_ros ros_environment rosidl_adapter rosidl_parser rosidl_pycommon rosidl_cli \
python_cmake_module ament_index_python rpyutils ament_cmake_gmock ament_cmake_gtest ament_cmake_pytest"
cd "$ROS2_SRC"
colcon build --base-paths src --build-base build_host --install-base install \
  --packages-up-to $PKGS --cmake-args -DBUILD_TESTING=OFF $EH

P=""; for pkg in $PKGS; do P="$P:$ROS2_SRC/install/$pkg"; done
export CMAKE_PREFIX_PATH="${P#:}"
export AMENT_PREFIX_PATH="$CMAKE_PREFIX_PATH"
# Módulos Python das ferramentas (ament_package, rosidl_*), só do install nativo.
PYTHONPATH="$(ls -d "$ROS2_SRC"/install/*/lib/python3*/site-packages | paste -sd:)"
export PYTHONPATH

SKIP="ament_lint ament_pycodestyle ament_flake8 ament_mypy ament_pep257 ament_cppcheck ament_cpplint \
ament_copyright ament_xmllint ament_uncrustify uncrustify_vendor ament_cmake_uncrustify ament_cmake_cppcheck \
ament_cmake_cpplint ament_cmake_flake8 ament_cmake_pep257 ament_cmake_xmllint ament_cmake_copyright \
ament_cmake_lint_cmake ament_lint_cmake ament_cmake_google_benchmark gtest_vendor gmock_vendor \
google_benchmark_vendor osrf_testing_tools_cpp performance_test_fixture domain_coordinator launch launch_xml \
launch_yaml launch_testing launch_testing_ament_cmake rosidl_generator_py rpyutils rosidl_generator_rs fastrtps \
rmw_connextdds rmw_connextddsmicro rti_connext_dds_cmake_module rmw_fastrtps_cpp rmw_fastrtps_dynamic_cpp \
rmw_fastrtps_shared_cpp rosidl_dynamic_typesupport_fastrtps rmw_connextdds_common spdlog_vendor \
rcl_logging_spdlog test_msgs test_interface_files iceoryx_hoofs iceoryx_posh iceoryx_binding_c osrf_pycommon \
ament_lint_common mimick_vendor ament_index_python"
# Além do handoff: Release (sem debug info) e páginas de 16 KB (AAOS 15+, §9 item 1).
CORE_ARGS="-DBUILD_TESTING=OFF -DENABLE_SSL=OFF -DENABLE_SECURITY=OFF -DENABLE_SHM=OFF -DBUILD_IDLC=OFF \
-DBUILD_DDSPERF=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF -DRCL_LOGGING_IMPLEMENTATION=rcl_logging_noop \
-DCMAKE_BUILD_TYPE=Release -DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384 \
-DCMAKE_EXE_LINKER_FLAGS=-Wl,-z,max-page-size=16384"
OUT="--base-paths src --build-base build_android_x86_64 --install-base install_android_x86_64"

# --packages-ignore em vez do --packages-skip do handoff: com skip, o colcon exige
# as dependências de teste (performance_test_fixture etc.) já instaladas.
# 1) libyaml_vendor com P1.
colcon build $OUT --packages-up-to libyaml_vendor --packages-ignore $SKIP \
  --cmake-args $TC $CORE_ARGS -DCMAKE_DISABLE_FIND_PACKAGE_yaml=ON $EH
# 2) núcleo até rclcpp + mensagens do MVP.
colcon build $OUT --packages-up-to rclcpp std_msgs geometry_msgs --packages-ignore $SKIP \
  --cmake-args $TC $CORE_ARGS $EH
