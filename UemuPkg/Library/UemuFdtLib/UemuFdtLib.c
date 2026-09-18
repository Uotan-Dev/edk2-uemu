/** @file
  Helpers for reading uemu platform data from the device tree.

  Copyright (c) 2026 Nuo Shen, Nanjing University

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Uefi.h>

#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>

#include <Guid/FdtHob.h>
#include <Library/BaseLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/UemuFdtLib.h>

STATIC
EFI_STATUS
DecodeCells (
  IN  CONST UINT32  *Cells,
  IN  UINTN         Count,
  OUT UINT64        *Value
  )
{
  UINTN   Index;
  UINT64  Result;

  if ((Count == 0) || (Count > FDT_MAX_NCELLS)) {
    return EFI_COMPROMISED_DATA;
  }

  Result = 0;
  for (Index = 0; Index < Count; Index++) {
    if (Result > RShiftU64 (MAX_UINT64, 32)) {
      return EFI_BAD_BUFFER_SIZE;
    }

    Result = LShiftU64 (Result, 32) | Fdt32ToCpu (Cells[Index]);
  }

  *Value = Result;
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
NodeIsEnabled (
  IN CONST VOID  *Fdt,
  IN INT32       Node
  )
{
  CONST CHAR8  *Status;
  INT32        Length;

  Status = FdtGetProp (Fdt, Node, "status", &Length);
  if (Status == NULL) {
    return TRUE;
  }

  if ((Length <= 0) || (AsciiStrnLenS (Status, Length) != (UINTN)(Length - 1))) {
    return FALSE;
  }

  return (BOOLEAN)((AsciiStrCmp (Status, "okay") == 0) ||
                   (AsciiStrCmp (Status, "ok") == 0));
}

EFI_STATUS
EFIAPI
UemuFdtGet (
  OUT CONST VOID  **Fdt
  )
{
  VOID  *Hob;

  if (Fdt == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Hob = GetFirstGuidHob (&gFdtHobGuid);
  if ((Hob == NULL) || (GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (UINT64))) {
    return EFI_NOT_FOUND;
  }

  *Fdt = (CONST VOID *)(UINTN)*(CONST UINT64 *)GET_GUID_HOB_DATA (Hob);
  if ((*Fdt == NULL) || (FdtCheckHeader (*Fdt) != 0)) {
    return EFI_COMPROMISED_DATA;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UemuFdtFindCompatibleNode (
  IN  CONST VOID   *Fdt,
  IN  CONST CHAR8  *Compatible,
  OUT INT32        *Node
  )
{
  INT32  Current;

  if ((Fdt == NULL) || (Compatible == NULL) || (Node == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Current = -1;
  while (TRUE) {
    Current = FdtNodeOffsetByCompatible (Fdt, Current, Compatible);
    if (Current == -FDT_ERR_NOTFOUND) {
      return EFI_NOT_FOUND;
    }

    if (Current < 0) {
      return EFI_COMPROMISED_DATA;
    }

    if (NodeIsEnabled (Fdt, Current)) {
      *Node = Current;
      return EFI_SUCCESS;
    }
  }
}

EFI_STATUS
EFIAPI
UemuFdtGetReg (
  IN  CONST VOID            *Fdt,
  IN  INT32                 Node,
  IN  UINTN                 Index,
  OUT EFI_PHYSICAL_ADDRESS  *Base,
  OUT UINT64                *Size
  )
{
  EFI_STATUS    Status;
  INT32         Parent;
  INT32         AddressCells;
  INT32         SizeCells;
  INT32         Length;
  UINTN         TupleCells;
  UINTN         TupleSize;
  CONST UINT32  *Reg;
  UINT64        Address;
  UINT64        RangeSize;

  if ((Fdt == NULL) || (Base == NULL) || (Size == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Parent = FdtParentOffset (Fdt, Node);
  if (Parent < 0) {
    return EFI_COMPROMISED_DATA;
  }

  AddressCells = FdtAddressCells (Fdt, Parent);
  SizeCells    = FdtSizeCells (Fdt, Parent);
  if ((AddressCells <= 0) || (AddressCells > FDT_MAX_NCELLS) ||
      (SizeCells <= 0) || (SizeCells > FDT_MAX_NCELLS))
  {
    return EFI_COMPROMISED_DATA;
  }

  TupleCells = (UINTN)AddressCells + (UINTN)SizeCells;
  TupleSize  = TupleCells * sizeof (UINT32);
  Reg        = FdtGetProp (Fdt, Node, "reg", &Length);
  if (Reg == NULL) {
    return EFI_NOT_FOUND;
  }

  if ((Length <= 0) || (((UINTN)Length % TupleSize) != 0)) {
    return EFI_COMPROMISED_DATA;
  }

  if (Index >= (UINTN)Length / TupleSize) {
    return EFI_NOT_FOUND;
  }

  Reg += Index * TupleCells;
  Status = DecodeCells (Reg, AddressCells, &Address);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = DecodeCells (Reg + AddressCells, SizeCells, &RangeSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((RangeSize == 0) || (Address > MAX_UINT64 - RangeSize)) {
    return EFI_COMPROMISED_DATA;
  }

  *Base = Address;
  *Size = RangeSize;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UemuFdtGetU32 (
  IN  CONST VOID   *Fdt,
  IN  INT32        Node,
  IN  CONST CHAR8  *Property,
  OUT UINT32       *Value
  )
{
  CONST UINT32  *Data;
  INT32         Length;

  if ((Fdt == NULL) || (Property == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Data = FdtGetProp (Fdt, Node, Property, &Length);
  if (Data == NULL) {
    return EFI_NOT_FOUND;
  }

  if (Length != sizeof (*Data)) {
    return EFI_COMPROMISED_DATA;
  }

  *Value = Fdt32ToCpu (*Data);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UemuFdtGetString (
  IN  CONST VOID   *Fdt,
  IN  INT32        Node,
  IN  CONST CHAR8  *Property,
  OUT CONST CHAR8  **Value,
  OUT UINTN        *Size OPTIONAL
  )
{
  CONST CHAR8  *Data;
  INT32        Length;

  if ((Fdt == NULL) || (Property == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Data = FdtGetProp (Fdt, Node, Property, &Length);
  if (Data == NULL) {
    return EFI_NOT_FOUND;
  }

  if ((Length <= 0) || (AsciiStrnLenS (Data, Length) != (UINTN)(Length - 1))) {
    return EFI_COMPROMISED_DATA;
  }

  *Value = Data;
  if (Size != NULL) {
    *Size = Length;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UemuFdtAlignRange (
  IN  EFI_PHYSICAL_ADDRESS  Base,
  IN  UINT64                Size,
  OUT EFI_PHYSICAL_ADDRESS  *AlignedBase,
  OUT UINT64                *AlignedSize
  )
{
  UINT64  End;
  UINT64  AlignedEnd;

  if ((AlignedBase == NULL) || (AlignedSize == NULL) || (Size == 0) ||
      (Base > MAX_UINT64 - Size))
  {
    return EFI_INVALID_PARAMETER;
  }

  End = Base + Size;
  if (End > MAX_UINT64 - EFI_PAGE_MASK) {
    return EFI_BAD_BUFFER_SIZE;
  }

  *AlignedBase = Base & ~(EFI_PHYSICAL_ADDRESS)EFI_PAGE_MASK;
  AlignedEnd   = ALIGN_VALUE (End, EFI_PAGE_SIZE);
  *AlignedSize = AlignedEnd - *AlignedBase;
  return EFI_SUCCESS;
}
