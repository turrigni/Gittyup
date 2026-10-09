#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${SOURCE_DIR}/build/rhel9-release}"
JOBS="${JOBS:-$(nproc)}"
INSTALL_PREFIX="${INSTALL_PREFIX:-${HOME}/.local}"

if [[ ! -r /etc/os-release ]]; then
    printf 'Cannot identify the operating system.\n' >&2
    exit 1
fi

source /etc/os-release
if [[ "${ID:-}" != rhel || "${VERSION_ID%%.*}" != 9 ]]; then
    printf 'This installer supports RHEL 9 only (detected: %s %s).\n' \
        "${ID:-unknown}" "${VERSION_ID:-unknown}" >&2
    exit 1
fi

if ! command -v dnf >/dev/null 2>&1; then
    printf 'dnf is required to install build dependencies.\n' >&2
    exit 1
fi

if [[ ${EUID} -eq 0 ]]; then
    SUDO=()
else
    if ! command -v sudo >/dev/null 2>&1; then
        printf 'Run as root or install sudo to install dependencies.\n' >&2
        exit 1
    fi
    SUDO=(sudo)
fi

# PACKAGES=(
#     cmake
#     gcc
#     gcc-c++
#     git
#     libxkbcommon-devel
#     make
#     mesa-libGL-devel
#     openssl-devel
#     pcre-devel
#     pkgconf-pkg-config
#     qt6-qt5compat
#     qt6-qt5compat-devel
#     qt6-qtbase-devel
#     qt6-qttools-devel
#     zlib-devel
# )

# printf 'Installing RHEL 9 build and runtime dependencies...\n'
# "${SUDO[@]}" dnf install -y "${PACKAGES[@]}"

printf 'Initializing Git submodules...\n'
git -C "${SOURCE_DIR}" submodule update --init --depth 1

mkdir -p "${BUILD_DIR}"
BUILD_DIR="$(cd -- "${BUILD_DIR}" && pwd)"

printf 'Configuring Release build...\n'
cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DQt6_DIR=/usr/lib64/cmake/Qt6 \
    -DUSE_SYSTEM_LIBSSH2=OFF \
    -DUSE_SYSTEM_OPENSSL=ON \
    -DUSE_SYSTEM_QT=ON \
    -DENABLE_TESTS=OFF

printf 'Building Gittyup with %s jobs...\n' "${JOBS}"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

printf 'Installing Gittyup under %s...\n' "${INSTALL_PREFIX}"
cmake --install "${BUILD_DIR}"

printf 'Installation complete. Launch Gittyup with: %s/bin/gittyup\n' \
    "${INSTALL_PREFIX}"