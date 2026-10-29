# ROS 2 Jazzy (Ubuntu 24.04) para cross-compilar o ROS para Android sem tocar
# no Humble do host. Receita: Refs/handoff-ros2-jazzy-aaos.md §4 e §10.
FROM ros:jazzy-ros-base-noble

ARG NDK_VERSION=r27
# SHA-1 de dl.google.com/android/repository/repository2-3.xml (ndk;27.0.12077973).
ARG NDK_SHA1=5e5cd517bdb98d7e0faf2c494a3041291e71bdcc

RUN apt-get update && apt-get install -y --no-install-recommends \
      ros-jazzy-rmw-cyclonedds-cpp unzip wget llvm \
    && rm -rf /var/lib/apt/lists/*

# NDK Linux (o do Windows SDK não serve). 27.0.12077973 = r27.
RUN wget -q -O /tmp/ndk.zip \
      https://dl.google.com/android/repository/android-ndk-${NDK_VERSION}-linux.zip \
    && echo "${NDK_SHA1}  /tmp/ndk.zip" | sha1sum -c - \
    && unzip -q /tmp/ndk.zip -d /opt \
    && rm /tmp/ndk.zip

ENV ANDROID_NDK_HOME=/opt/android-ndk-${NDK_VERSION} \
    RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
    ROS2CLI_START_DAEMON=false

# A imagem não faz source de /opt/ros/jazzy: o build cruzado usa só o prefixo
# curado do handoff §4.2. Para usar o ros2 CLI: source /opt/ros/jazzy/setup.bash.
WORKDIR /ws
