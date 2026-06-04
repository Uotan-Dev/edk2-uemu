#!/bin/bash
set -ex
cd "$(dirname "$0")"
git submodule update --init --recursive --depth 1
EDK2_TARGET="${EDK2_TARGET:-RELEASE}"
export EDK_TOOLS_PATH="${PWD}/edk2/BaseTools"
export PACKAGES_PATH="${PWD}/edk2:${PWD}"
export WORKSPACE="${PWD}/build"
if ! gcc -dumpmachine 2>/dev/null | grep -q '^riscv64-'; then
    if which riscv64-unknown-elf-gcc &>/dev/null; then
        export GCC_RISCV64_PREFIX=riscv64-unknown-elf-
    else
        export GCC_RISCV64_PREFIX=riscv64-linux-gnu-
    fi
fi
VERSION="$(bash -$- scripts/version.sh)"
set +x
SAVED_ARGS=("$@")
set --
source "${PWD}/edk2/edksetup.sh"
set -- "${SAVED_ARGS[@]}"
set -x
mkdir -pv "${WORKSPACE}"
make -C "${EDK_TOOLS_PATH}" -j"$(nproc)"
build \
    -t GCC \
    -a RISCV64 \
    -b "${EDK2_TARGET}" \
    -D DISABLE_NEW_DEPRECATED_INTERFACES=TRUE \
    -D FIRMWARE_VER="${VERSION}" \
    -p UemuPkg/UemuPkg.dsc \
    "$@"
rm -fv RISCV_VIRT_CODE.fd RISCV_VIRT_VARS.fd
cp -v "${WORKSPACE}/Build/UemuPkg/${EDK2_TARGET}_GCC/FV/UEMU_CODE.fd" RISCV_VIRT_CODE.fd
cp -v "${WORKSPACE}/Build/UemuPkg/${EDK2_TARGET}_GCC/FV/UEMU_VARS.fd" RISCV_VIRT_VARS.fd
ls -lh RISCV_VIRT_CODE.fd RISCV_VIRT_VARS.fd
exit 0
