/** @file
  Read machine configuration from the device tree the emulator built.

  uemu-ng describes every guest-visible device it creates in the FDT it
  hands to the firmware, so that tree is the single source of truth for
  where a device lives, how large it is, and whether it exists at all.
  These helpers are the only place in UemuPkg that parses that data;
  drivers keep their own device register offsets and other ABI constants.

  Absent node and malformed node are deliberately different results: a
  missing compatible node is normal hardware absence, while a node that
  is present but carries a broken property is a bad machine description.

  Copyright (c) 2026 Nuo Shen, Nanjing University

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#pragma once

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>

#include <Guid/FdtHob.h>

//
// The root node always describes two address cells and two size cells for
// this platform, so one (address, size) pair in a "reg" property is 16 bytes.
//
#define UEMU_FDT_CELL_PAIR_SIZE  (2 * sizeof (UINT32) * 2)

/**
  Locate the device tree this platform booted with.

  @param[out] Fdt  Receives the device tree blob the emulator handed over.

  @retval EFI_SUCCESS        *Fdt holds a checked device tree blob.
  @retval EFI_NOT_FOUND      No FDT HOB, or no usable blob behind it.
  @retval EFI_DEVICE_ERROR   The blob exists but is not a valid FDT.
**/
STATIC
inline
EFI_STATUS
UemuFdtDeviceTreeBase (
  OUT CONST VOID  **Fdt
  )
{
  VOID  *Hob;

  Hob = GetFirstGuidHob (&gFdtHobGuid);
  if ((Hob == NULL) || (GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (UINT64))) {
    return EFI_NOT_FOUND;
  }

  *Fdt = (CONST VOID *)(UINTN)*(UINT64 *)GET_GUID_HOB_DATA (Hob);
  if (FdtCheckHeader (*Fdt) != 0) {
    DEBUG ((DEBUG_ERROR, "%a: invalid device tree\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

/**
  Find the first enabled node with a compatible string.

  @param[out] Node  Receives the node offset.

  @retval EFI_SUCCESS        *Node is an enabled node.
  @retval EFI_NOT_FOUND      No enabled node with that compatible string.
  @retval EFI_DEVICE_ERROR   The device tree itself is unusable.
**/
STATIC
inline
EFI_STATUS
UemuFdtFindNode (
  IN  CONST CHAR8  *Compatible,
  OUT INT32        *Node
  )
{
  EFI_STATUS   Status;
  CONST VOID   *Fdt;
  CONST CHAR8  *NodeStatus;
  INT32        Candidate;
  INT32        Len;

  Status = UemuFdtDeviceTreeBase (&Fdt);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Candidate = FdtNodeOffsetByCompatible (Fdt, 0, Compatible);
  if (Candidate < 0) {
    return EFI_NOT_FOUND;
  }

  //
  // A missing status property implies 'ok'; anything other than 'ok' or
  // 'okay' means the emulator did not wire the device up.  This mirrors
  // what FdtClientDxe's own node lookup treats as enabled.
  //
  NodeStatus = FdtGetProp (Fdt, Candidate, "status", &Len);
  if ((NodeStatus != NULL) &&
      (AsciiStrCmp (NodeStatus, "ok") != 0) &&
      (AsciiStrCmp (NodeStatus, "okay") != 0))
  {
    return EFI_NOT_FOUND;
  }

  *Node = Candidate;
  return EFI_SUCCESS;
}

/**
  Read the first (address, size) pair of a node's "reg" property.

  @param[in]  Node  Node offset to read from.
  @param[out] Base  Receives the device's MMIO base address.
  @param[out] Size  Receives the device's MMIO region size. May be NULL.

  @retval EFI_SUCCESS        *Base (and *Size) describe the device.
  @retval EFI_NOT_FOUND      The node has no "reg" property.
  @retval EFI_DEVICE_ERROR   The device tree is unusable, or "reg" is
                             malformed, or it describes a zero-sized region.
**/
STATIC
inline
EFI_STATUS
UemuFdtNodeReg (
  IN  INT32                Node,
  OUT EFI_PHYSICAL_ADDRESS *Base,
  OUT UINT64               *Size OPTIONAL
  )
{
  EFI_STATUS    Status;
  CONST VOID    *Fdt;
  CONST UINT32  *Reg;
  INT32         Len;
  UINT64        Address;
  UINT64        Length;

  Status = UemuFdtDeviceTreeBase (&Fdt);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Reg = FdtGetProp (Fdt, Node, "reg", &Len);
  if (Reg == NULL) {
    return EFI_NOT_FOUND;
  }

  if (Len < (INT32)UEMU_FDT_CELL_PAIR_SIZE) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: node %d has a malformed 'reg' property (size 0x%x)\n",
      __func__,
      Node,
      Len
      ));
    return EFI_DEVICE_ERROR;
  }

  Address = LShiftU64 (SwapBytes32 (Reg[0]), 32) | SwapBytes32 (Reg[1]);
  Length  = LShiftU64 (SwapBytes32 (Reg[2]), 32) | SwapBytes32 (Reg[3]);

  if (Length == 0) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: node %d describes a zero-sized region\n",
      __func__,
      Node
      ));
    return EFI_DEVICE_ERROR;
  }

  *Base = Address;
  if (Size != NULL) {
    *Size = Length;
  }

  return EFI_SUCCESS;
}

/**
  Find a node by compatible string and decode its first "reg" entry.

  @param[in]  Compatible  Compatible string to look for.
  @param[out] Base        Receives the device's MMIO base address.
  @param[out] Size        Receives the device's MMIO region size. May be NULL.

  @retval EFI_SUCCESS        *Base (and *Size) describe the device.
  @retval EFI_NOT_FOUND      No enabled node with that compatible string.
  @retval EFI_DEVICE_ERROR   The node exists but its "reg" is malformed.
**/
STATIC
inline
EFI_STATUS
UemuFdtFindNodeBaseAndSize (
  IN  CONST CHAR8          *Compatible,
  OUT EFI_PHYSICAL_ADDRESS *Base,
  OUT UINT64               *Size OPTIONAL
  )
{
  EFI_STATUS  Status;
  INT32       Node;

  Status = UemuFdtFindNode (Compatible, &Node);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return UemuFdtNodeReg (Node, Base, Size);
}

/**
  Round a device's MMIO region out to the page window that contains it.

  The GCD describes memory space in 4 KB pages and rejects a runtime
  visible region whose base or length is not page aligned, while a device
  tree "reg" is only as large as the device's register file: the RTC
  declares 0x100 bytes and the RNG 0x10.  Device access must keep using
  the exact "reg" base, so this only widens a copy of the region for the
  page-granular APIs; it never reports a smaller range than it was given.

  @param[in,out] Base    Region base, rounded down to a page boundary.
  @param[in,out] Length  Region length, grown to cover whole pages.
**/
STATIC
inline
VOID
UemuFdtRegionToPageWindow (
  IN OUT EFI_PHYSICAL_ADDRESS  *Base,
  IN OUT UINT64                *Length
  )
{
  EFI_PHYSICAL_ADDRESS  Start;
  EFI_PHYSICAL_ADDRESS  End;

  Start = *Base & ~(EFI_PHYSICAL_ADDRESS)EFI_PAGE_MASK;
  End   = ALIGN_VALUE (*Base + *Length, SIZE_4KB);

  *Base   = Start;
  *Length = End - Start;
}

/**
  Read a 32-bit property from a node.

  @param[in]  Node   Node offset to read from.
  @param[in]  Name   Property name.
  @param[out] Value  Receives the property value.

  @retval EFI_SUCCESS        *Value holds the decoded property.
  @retval EFI_NOT_FOUND      The node has no such property.
  @retval EFI_DEVICE_ERROR   The property exists but is not one 32-bit cell.
**/
STATIC
inline
EFI_STATUS
UemuFdtGetUint32 (
  IN  INT32        Node,
  IN  CONST CHAR8  *Name,
  OUT UINT32       *Value
  )
{
  EFI_STATUS    Status;
  CONST VOID    *Fdt;
  CONST UINT32  *Prop;
  INT32         Len;

  Status = UemuFdtDeviceTreeBase (&Fdt);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Prop = FdtGetProp (Fdt, Node, Name, &Len);
  if (Prop == NULL) {
    return EFI_NOT_FOUND;
  }

  if (Len != (INT32)sizeof (UINT32)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: '%a' is not a 32-bit property (size 0x%x)\n",
      __func__,
      Name,
      Len
      ));
    return EFI_DEVICE_ERROR;
  }

  *Value = SwapBytes32 (ReadUnaligned32 (Prop));
  return EFI_SUCCESS;
}

/**
  Read a non-empty string property from a node.

  @param[in]  Node   Node offset to read from.
  @param[in]  Name   Property name.
  @param[out] Value  Receives a pointer into the device tree blob, valid for
                     the lifetime of the blob. Never NULL.

  @retval EFI_SUCCESS        *Value points at a non-empty string.
  @retval EFI_NOT_FOUND      The node has no such property.
  @retval EFI_DEVICE_ERROR   The property is not a string.
**/
STATIC
inline
EFI_STATUS
UemuFdtGetString (
  IN  INT32        Node,
  IN  CONST CHAR8  *Name,
  OUT CONST CHAR8  **Value
  )
{
  EFI_STATUS   Status;
  CONST VOID   *Fdt;
  CONST CHAR8  *Prop;
  INT32        Len;

  Status = UemuFdtDeviceTreeBase (&Fdt);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Prop = FdtGetProp (Fdt, Node, Name, &Len);
  if (Prop == NULL) {
    return EFI_NOT_FOUND;
  }

  if ((Len <= 0) || (Prop[Len - 1] != '\0') || (AsciiStrLen (Prop) == 0)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: '%a' is not a usable string (size 0x%x)\n",
      __func__,
      Name,
      Len
      ));
    return EFI_DEVICE_ERROR;
  }

  *Value = Prop;
  return EFI_SUCCESS;
}
