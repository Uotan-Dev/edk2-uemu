/** @file
  Helpers for reading uemu platform data from the device tree.

  Copyright (c) 2026 Nuo Shen, Nanjing University

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#pragma once

#include <Uefi.h>

EFI_STATUS
EFIAPI
UemuFdtGet (
  OUT CONST VOID  **Fdt
  );

EFI_STATUS
EFIAPI
UemuFdtFindCompatibleNode (
  IN  CONST VOID   *Fdt,
  IN  CONST CHAR8  *Compatible,
  OUT INT32        *Node
  );

EFI_STATUS
EFIAPI
UemuFdtGetReg (
  IN  CONST VOID           *Fdt,
  IN  INT32                Node,
  IN  UINTN                Index,
  OUT EFI_PHYSICAL_ADDRESS *Base,
  OUT UINT64               *Size
  );

EFI_STATUS
EFIAPI
UemuFdtGetU32 (
  IN  CONST VOID   *Fdt,
  IN  INT32        Node,
  IN  CONST CHAR8  *Property,
  OUT UINT32       *Value
  );

EFI_STATUS
EFIAPI
UemuFdtGetString (
  IN  CONST VOID   *Fdt,
  IN  INT32        Node,
  IN  CONST CHAR8  *Property,
  OUT CONST CHAR8  **Value,
  OUT UINTN        *Size OPTIONAL
  );

EFI_STATUS
EFIAPI
UemuFdtAlignRange (
  IN  EFI_PHYSICAL_ADDRESS  Base,
  IN  UINT64                Size,
  OUT EFI_PHYSICAL_ADDRESS  *AlignedBase,
  OUT UINT64                *AlignedSize
  );
