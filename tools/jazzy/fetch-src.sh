#!/usr/bin/env bash
# Baixa o fonte do ROS 2 Jazzy em /ws/ros2_src/src e fixa os repositórios nos
# commits do handoff (Refs/handoff-ros2-jazzy-aaos.md §3). Rodar no contêiner:
#   tools/jazzy/run.sh /bridge/tools/jazzy/fetch-src.sh
set -euo pipefail

SRC=/ws/ros2_src/src
mkdir -p "$SRC"
cd "$SRC"

if [ ! -f /ws/ros2_src/ros2.repos ]; then
  wget -q -O /ws/ros2_src/ros2.repos \
    https://raw.githubusercontent.com/ros2/ros2/jazzy/ros2.repos
fi
vcs import --skip-existing < /ws/ros2_src/ros2.repos

# repo:commit, do handoff §3.
PINS="
ros2/rcutils:2cd95ae
ros2/rclcpp:99b3803c
ros2/rcl:7927b76
ros2/rmw:c90c297
ros2/rmw_cyclonedds:7645474
eclipse-cyclonedds/cyclonedds:454df330
eclipse-iceoryx/iceoryx:ebdf38728
ros2/rcpputils:747b92f
ros2/rosidl:b56ff98
ros2/rosidl_typesupport:dc851e0
ros2/rosidl_typesupport_fastrtps:8a25101
eProsima/Fast-CDR:5cc8c55
eProsima/foonathan_memory_vendor:716cfd8
ros2/rcl_logging:da51595
ros2/common_interfaces:784c489
ros2/libyaml_vendor:dbba913
ros2/yaml_cpp_vendor:867dd6b
ros2/tinyxml2_vendor:85acf25
ros2/spdlog_vendor:dfd75d8
ros2/console_bridge_vendor:abb96a9
"
for pin in $PINS; do
  repo="${pin%%:*}"; commit="${pin##*:}"
  git -C "$repo" fetch -q origin "$commit" 2>/dev/null || git -C "$repo" fetch -q --all
  git -C "$repo" checkout -q "$commit"
  echo "$repo -> $(git -C "$repo" rev-parse --short HEAD)"
done
