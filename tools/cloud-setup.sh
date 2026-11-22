#!/usr/bin/env bash
# Setup script of the cloud environment (Claude Code on the web): installs what
# the project needs beyond the image (Ubuntu 24.04, GCC, Clang, CMake, Ninja,
# Python already there). Paste it in the environment's "Setup script" box (or
# load it with "Choose file"); it runs as root once, then the machine is cached.
# Safe to run again; on a desktop, the README's apt line is enough.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

apt-get update -q

# SDL3 build dependencies (as in the README), so that the debug, release and
# asan presets can build SDL3 and SDL3_ttf from source.
packages=(
    build-essential cmake ninja-build git pkg-config
    libfreetype-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev
    libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev
    libgl1-mesa-dev libasound2-dev libpulse-dev libdecor-0-dev
)
# The formatter, the same version as on the desktop (the whole tree is kept
# clean with it; another version formats a few lines differently).
packages+=(clang-format-18)
# Visual checks: a virtual display, software OpenGL, input and screenshots,
# keyboard layouts.
packages+=(xvfb libgl1-mesa-dri mesa-utils xdotool imagemagick x11-xkb-utils
    x11-xserver-utils)
# The sprite tools (tools/*.py).
packages+=(python3-pil python3-numpy python3-tk)

apt-get install -y -q --no-install-recommends "${packages[@]}"

# `clang-format` must be version 18 whatever else the image carries.
ln -sf "$(command -v clang-format-18)" /usr/local/bin/clang-format

echo "cloud-setup: done"
cmake --version | head -n 1
clang-format --version
