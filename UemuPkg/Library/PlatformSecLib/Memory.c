/** @file
  Memory Detection for RiscVVirt Machines.

  Copyright (c) 2021, Hewlett Packard Enterprise Development LP. All rights reserved.<BR>
  Copyright (c) 2006 - 2014, Intel Corporation. All rights reserved.<BR>
  Copyright (c) 2025, Ventana Micro Systems Inc. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "PlatformSecLib.h"

#include <Library/UemuFdtLib.h>

VOID
BuildMemoryTypeHob (
  VOID
  )
{
  EFI_MEMORY_TYPE_INFORMATION  Info[6];

  Info[0].Type          = EfiACPIReclaimMemory;
  Info[0].NumberOfPages = PcdGet32 (PcdMemoryTypeEfiACPIReclaimMemory);
  Info[1].Type          = EfiACPIMemoryNVS;
  Info[1].NumberOfPages = PcdGet32 (PcdMemoryTypeEfiACPIMemoryNVS);
  Info[2].Type          = EfiReservedMemoryType;
  Info[2].NumberOfPages = PcdGet32 (PcdMemoryTypeEfiReservedMemoryType);
  Info[3].Type          = EfiRuntimeServicesData;
  Info[3].NumberOfPages = PcdGet32 (PcdMemoryTypeEfiRuntimeServicesData);
  Info[4].Type          = EfiRuntimeServicesCode;
  Info[4].NumberOfPages = PcdGet32 (PcdMemoryTypeEfiRuntimeServicesCode);
  // Terminator for the list
  Info[5].Type          = EfiMaxMemoryType;
  Info[5].NumberOfPages = 0;

  BuildGuidDataHob (&gEfiMemoryTypeInformationGuid, &Info, sizeof (Info));
}

/**
  Create memory range resource HOB using the memory base
  address and size.

  @param  MemoryBase     Memory range base address.
  @param  MemorySize     Memory range size.

**/
STATIC
VOID
AddMemoryBaseSizeHob (
  IN EFI_PHYSICAL_ADDRESS  MemoryBase,
  IN UINT64                MemorySize
  )
{
  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_TESTED,
    MemoryBase,
    MemorySize
    );
}

/**
  Create memory range resource HOB using memory base
  address and top address of the memory range.

  @param  MemoryBase     Memory range base address.
  @param  MemoryLimit    Memory range size.

**/
STATIC
VOID
AddMemoryRangeHob (
  IN EFI_PHYSICAL_ADDRESS  MemoryBase,
  IN EFI_PHYSICAL_ADDRESS  MemoryLimit
  )
{
  AddMemoryBaseSizeHob (MemoryBase, (UINT64)(MemoryLimit - MemoryBase));
}

/**
  Publish system RAM and reserve memory regions.

**/
STATIC
VOID
InitializeRamRegions (
  IN EFI_PHYSICAL_ADDRESS  SystemMemoryBase,
  IN UINT64                SystemMemorySize
  )
{
  AddMemoryRangeHob (
    SystemMemoryBase,
    SystemMemoryBase + SystemMemorySize
    );
}

/** Mark reserved memory ranges in the EFI memory map

 * As per DT spec v0.4 Section 3.5.4,
 * "Reserved regions with the no-map property must be listed in the
 * memory map with type EfiReservedMemoryType. All other reserved
 * regions must be listed with type EfiBootServicesData."

  @param FdtPointer Pointer to FDT

**/
STATIC
VOID
AddReservedMemoryMap (
  IN VOID  *FdtPointer
  )
{
  EFI_STATUS            Status;
  INT32                 Node;
  INT32                 SubNode;
  INT32                 Len;
  EFI_PHYSICAL_ADDRESS  Addr;
  UINT64                Size;
  INTN                  NumRsv, i;
  UINTN                 Index;

  NumRsv = FdtGetNumberOfReserveMapEntries (FdtPointer);

  /* Look for an existing entry and add it to the efi mem map. */
  for (i = 0; i < NumRsv; i++) {
    if (FdtGetReserveMapEntry (FdtPointer, i, &Addr, &Size) != 0) {
      continue;
    }

    if ((Size != 0) && (Addr <= MAX_UINT64 - Size)) {
      BuildMemoryAllocationHob (
        Addr,
        Size,
        EfiReservedMemoryType
        );
    }
  }

  /* process reserved-memory */
  Node = FdtSubnodeOffset (FdtPointer, 0, "reserved-memory");
  if (Node >= 0) {
    FdtForEachSubnode (SubNode, FdtPointer, Node) {
      for (Index = 0; ; Index++) {
        Status = UemuFdtGetReg (
                   FdtPointer,
                   SubNode,
                   Index,
                   &Addr,
                   &Size
                   );
        if (Status == EFI_NOT_FOUND) {
          break;
        }

        if (EFI_ERROR (Status)) {
          DEBUG ((DEBUG_ERROR, "%a: invalid reserved-memory reg: %r\n",
                  __func__, Status));
          break;
        }

        DEBUG ((
          DEBUG_INFO,
          "%a: Adding Reserved Memory Addr = 0x%llx, Size = 0x%llx\n",
          __func__,
          Addr,
          Size
          ));

        if (FdtGetProp (FdtPointer, SubNode, "no-map", &Len)) {
          BuildMemoryAllocationHob (
            Addr,
            Size,
            EfiReservedMemoryType
            );
        } else {
          BuildMemoryAllocationHob (
            Addr,
            Size,
            EfiBootServicesData
            );
        }
      }
    }
  }
}

/**
  Perform Memory initialization.

  @param  FdtPointer      The pointer to the device tree.

  @return EFI_SUCCESS     The platform initialized successfully.
  @retval  Others        - As the error code indicates

**/
EFI_STATUS
EFIAPI
MemoryInitialization (
  VOID  *FdtPointer
  )
{
  EFI_STATUS    Status;
  CONST CHAR8   *Type;
  EFI_PHYSICAL_ADDRESS  CurBase;
  UINT64        CurSize;
  INT32         Node, Prev;
  INT32         Len;
  UINTN         Index;

  // Look for the lowest memory node
  for (Prev = 0; ; Prev = Node) {
    Node = FdtNextNode (FdtPointer, Prev, NULL);
    if (Node < 0) {
      break;
    }

    // Check for memory node
    Type = FdtGetProp (FdtPointer, Node, "device_type", &Len);
    if ((Type != NULL) && (Len == sizeof ("memory")) &&
        (AsciiStrCmp (Type, "memory") == 0))
    {
      for (Index = 0; ; Index++) {
        Status = UemuFdtGetReg (
                   FdtPointer,
                   Node,
                   Index,
                   &CurBase,
                   &CurSize
                   );
        if (Status == EFI_NOT_FOUND) {
          break;
        }

        if (EFI_ERROR (Status)) {
          DEBUG ((DEBUG_ERROR, "%a: Failed to parse FDT memory node: %r\n",
                  __func__, Status));
          break;
        }

        DEBUG ((
          DEBUG_INFO,
          "%a: System RAM @ 0x%lx - 0x%lx\n",
          __func__,
          CurBase,
          CurBase + CurSize - 1
          ));

        InitializeRamRegions (
          CurBase,
          CurSize
          );
      }
    }
  }

  AddReservedMemoryMap (FdtPointer);

  /* Make sure SEC is booting with bare mode */
  ASSERT ((RiscVGetSupervisorAddressTranslationRegister () & SATP64_MODE) == (SATP_MODE_OFF << SATP64_MODE_SHIFT));

  BuildMemoryTypeHob ();

  return EFI_SUCCESS;
}
