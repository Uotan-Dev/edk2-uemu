/** @file

 Copyright (c) 2019, Linaro Ltd. All rights reserved
 Copyright (c) 2026 Nuo Shen, Nanjing University

 SPDX-License-Identifier: BSD-2-Clause-Patent

 **/

#include <Base.h>
#include <PiDxe.h>
#include <Library/VirtNorFlashPlatformLib.h>

#define QEMU_NOR_BLOCK_SIZE  SIZE_256KB

#define VARS_REGION_SIZE  (SIZE_256KB * 3)  // 768 KB: VarStore + FTW + Spare

EFI_STATUS
VirtNorFlashPlatformInitialization (
  VOID
  )
{
  return EFI_SUCCESS;
}

VIRT_NOR_FLASH_DESCRIPTION  mNorFlashDevice =
{
  FixedPcdGet32 (PcdOvmfFdBaseAddress),
  FixedPcdGet64 (PcdFlashNvStorageVariableBase),
  // Size covers from CODE base through the end of the VARS region.
  // VARS is at a non-contiguous offset (0x22000000 vs code at 0x20000000)
  // within the same PFlash CFI device.
  (FixedPcdGet64 (PcdFlashNvStorageVariableBase) - FixedPcdGet32 (PcdOvmfFdBaseAddress))
    + VARS_REGION_SIZE,
  QEMU_NOR_BLOCK_SIZE
};

EFI_STATUS
VirtNorFlashPlatformGetDevices (
  OUT VIRT_NOR_FLASH_DESCRIPTION  **NorFlashDescriptions,
  OUT UINT32                      *Count
  )
{
  *NorFlashDescriptions = &mNorFlashDevice;
  *Count                = 1;
  return EFI_SUCCESS;
}
