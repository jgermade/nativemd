#!/usr/bin/env bash
# Install what nativemd and its development setup ('make up') need: a C/C++ compiler,
# CMake, Ninja, Python 3, Lua (the disassembly's bit-perfect check), unzip, git, SDL2
# (desktop host). Detects apt, dnf, pacman, zypper or Homebrew; asks for sudo when needed.
set -euo pipefail
sudo_if_needed() { if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi; }
if command -v apt-get > /dev/null; then
    sudo_if_needed apt-get update
    sudo_if_needed apt-get install -y build-essential cmake ninja-build python3 lua5.4 unzip git \
        pkg-config libsdl2-dev
elif command -v dnf > /dev/null; then
    sudo_if_needed dnf install -y gcc gcc-c++ make cmake ninja-build python3 lua unzip git SDL2-devel
elif command -v pacman > /dev/null; then
    sudo_if_needed pacman -S --needed --noconfirm base-devel cmake ninja python lua unzip git sdl2
elif command -v zypper > /dev/null; then
    sudo_if_needed zypper install -y gcc gcc-c++ make cmake ninja python3 lua54 unzip git SDL2-devel
elif command -v brew > /dev/null; then
    brew install cmake ninja python lua sdl2
else
    echo "install_deps.sh: no known package manager (apt, dnf, pacman, zypper, brew)." >&2
    echo "Install by hand: a C/C++ compiler, CMake >= 3.16, Ninja, Python 3, Lua, unzip, git, SDL2 (dev)." >&2
    exit 1
fi
echo "dependencies installed"
