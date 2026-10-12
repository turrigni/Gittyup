#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${SOURCE_DIR}/build/release}"
JOBS="${JOBS:-$(nproc)}"
INSTALL_PREFIX="${INSTALL_PREFIX:-${HOME}/.local}"

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

# ask for installation
read -p "Do you want to install Gittyup under ${INSTALL_PREFIX}? [y/N] " response
if [[ ! "${response,,}" =~ ^(yes|y)$ ]]; then
    printf 'Installation aborted.\n'
    exit 0
fi

printf 'Installing Gittyup under %s...\n' "${INSTALL_PREFIX}"
cmake --install "${BUILD_DIR}"

printf 'Installation complete. Launch Gittyup with: %s/bin/gittyup\n' \
    "${INSTALL_PREFIX}"