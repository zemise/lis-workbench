#!/usr/bin/env bash
set -euo pipefail

# Cross-package LIS Workbench Windows app from macOS.
# Output:
#   - build cache:               lis-workbench/build/windows-x64
#   - portable runtime stage:    lis-workbench/out/windows/portable/LISWorkbench
#   - installer:                 lis-workbench/out/windows/installer/LISWorkbench-Setup.exe

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/mingw-w64-x86_64.cmake"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/windows-x64}"
STAGE_DIR="${STAGE_DIR:-${ROOT_DIR}/out/windows/portable/LISWorkbench}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/out/windows/installer}"
PACKAGING_DIR="${ROOT_DIR}/packaging"
PACKAGE_WORK_DIR="${PACKAGE_WORK_DIR:-${ROOT_DIR}/out/windows/package-work}"
INSTALLER_OUT="${DIST_DIR}/LISWorkbench-Setup.exe"
APP_VERSION="$(awk -F'\"' '/kVersion/ { print $2; exit }' "${ROOT_DIR}/src/version.h")"

die() {
  echo "[ERROR] $*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing command: $1"
}

discover_nsisdir() {
  if ! command -v makensis >/dev/null 2>&1; then
    return 1
  fi
  makensis -HDRINFO 2>/dev/null | grep -o 'NSISDIR=[^,]*' | head -n 1 | cut -d= -f2-
}

echo "==> Check toolchain..."
require_cmd cmake
require_cmd x86_64-w64-mingw32-g++
require_cmd iconv
GENERATOR="Ninja"
if ! command -v ninja >/dev/null 2>&1; then
  GENERATOR="Unix Makefiles"
fi
[[ -f "${TOOLCHAIN_FILE}" ]] || die "toolchain file missing: ${TOOLCHAIN_FILE}"

echo "==> ROOT_DIR   : ${ROOT_DIR}"
echo "==> BUILD_DIR  : ${BUILD_DIR}"
echo "==> STAGE_DIR  : ${STAGE_DIR}"
echo "==> DIST_DIR   : ${DIST_DIR}"
echo "==> VERSION    : ${APP_VERSION:-unknown}"

mkdir -p "${BUILD_DIR}" "${STAGE_DIR}" "${DIST_DIR}" "${PACKAGE_WORK_DIR}"
rm -f "${BUILD_DIR}/CMakeCache.txt"

echo "==> Configure CMake (cross MinGW)..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "${GENERATOR}" \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
  -DCMAKE_BUILD_TYPE=Release

echo "==> Build result_search.exe..."
cmake --build "${BUILD_DIR}" --target result_search -j 8

GUI_EXE="${BUILD_DIR}/result_search.exe"
[[ -f "${GUI_EXE}" ]] || die "missing ${GUI_EXE}"

echo "==> Stage runtime files..."
rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"

cp -f "${GUI_EXE}" "${STAGE_DIR}/result_search.exe"
cp -f "${ROOT_DIR}/README.md" "${STAGE_DIR}/README.md"
cp -f "${ROOT_DIR}/QUERY_DESIGN.md" "${STAGE_DIR}/QUERY_DESIGN.md"

echo "==> Stage ready: ${STAGE_DIR}"
ls -1 "${STAGE_DIR}"

NSISDIR_VALUE="$(discover_nsisdir || true)"
if command -v makensis >/dev/null 2>&1; then
  echo "==> Try NSIS installer framework..."
  NSI_TMP="${PACKAGE_WORK_DIR}/LISWorkbench-ansi.nsi"
  iconv -f UTF-8 -t GBK "${PACKAGING_DIR}/LISWorkbench.nsi" > "${NSI_TMP}"
  if [[ -n "${NSISDIR_VALUE}" ]] && NSISDIR="${NSISDIR_VALUE}" makensis \
      -DAPP_VERSION="${APP_VERSION}" \
      -DBUILD_DIR="${BUILD_DIR}" \
      -DOUTPUT_DIR="${PACKAGE_WORK_DIR}" \
      -DOUTPUT_NAME="LISWorkbench-Setup.exe" \
      -INPUTCHARSET CP936 "${NSI_TMP}"; then
    cp -f "${PACKAGE_WORK_DIR}/LISWorkbench-Setup.exe" "${INSTALLER_OUT}"
    echo "==> Installer built with NSIS: ${INSTALLER_OUT}"
    echo "Done."
    exit 0
  fi
  echo "[WARN] NSIS failed on this macOS environment."
fi

die "NSIS is required to build the LIS Workbench installer."
