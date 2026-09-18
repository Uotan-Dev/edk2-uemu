/** @file
 * SimpleFbDxe: Simple FrameBuffer for uemu-ng
 *
 * Copyright (c) 2026 Nuo Shen, Nanjing University
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
*/

#include <PiDxe.h>
#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UemuFdtLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/GraphicsOutput.h>
#include <Protocol/Cpu.h>

#define FB_BYTES_PER_PIXEL  4

typedef struct {
  VENDOR_DEVICE_PATH DisplayDevicePath;
  EFI_DEVICE_PATH    EndDevicePath;
} DISPLAY_DEVICE_PATH;

DISPLAY_DEVICE_PATH mDisplayDevicePath = {
    {{HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
          (UINT8)(sizeof(VENDOR_DEVICE_PATH)),
          (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8),
      }},
     EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID},
    {END_DEVICE_PATH_TYPE,
     END_ENTIRE_DEVICE_PATH_SUBTYPE,
     {sizeof(EFI_DEVICE_PATH_PROTOCOL), 0}}};

/// Declares

STATIC FRAME_BUFFER_CONFIGURE *mFrameBufferBltLibConfigure;
STATIC UINTN mFrameBufferBltLibConfigureSize;

STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber,
    OUT UINTN *SizeOfInfo, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);

STATIC
EFI_STATUS
EFIAPI
DisplaySetMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber);

STATIC
EFI_STATUS
EFIAPI
DisplayBlt(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer,
    OPTIONAL IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
    IN UINTN SourceX, IN UINTN SourceY, IN UINTN DestinationX,
    IN UINTN DestinationY, IN UINTN Width, IN UINTN Height,
    IN UINTN Delta OPTIONAL);

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL mDisplay = {
    DisplayQueryMode, DisplaySetMode, DisplayBlt, NULL};

STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber,
    OUT UINTN *SizeOfInfo, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info)
{
  EFI_STATUS Status;
  Status = gBS->AllocatePool(
      EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
      (VOID **)Info);

  ASSERT_EFI_ERROR(Status);

  *SizeOfInfo                   = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  (*Info)->Version              = This->Mode->Info->Version;
  (*Info)->HorizontalResolution = This->Mode->Info->HorizontalResolution;
  (*Info)->VerticalResolution   = This->Mode->Info->VerticalResolution;
  (*Info)->PixelFormat          = This->Mode->Info->PixelFormat;
  (*Info)->PixelsPerScanLine    = This->Mode->Info->PixelsPerScanLine;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DisplaySetMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber)
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DisplayBlt(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer,
    OPTIONAL IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
    IN UINTN SourceX, IN UINTN SourceY, IN UINTN DestinationX,
    IN UINTN DestinationY, IN UINTN Width, IN UINTN Height,
    IN UINTN Delta OPTIONAL)
{
  RETURN_STATUS Status;
  EFI_TPL       Tpl;
  //
  // We have to raise to TPL_NOTIFY, so we make an atomic write to the frame
  // buffer. We would not want a timer based event (Cursor, ...) to come in
  // while we are doing this operation.
  //
  Tpl    = gBS->RaiseTPL(TPL_NOTIFY);
  Status = FrameBufferBlt(
      mFrameBufferBltLibConfigure, BltBuffer, BltOperation, SourceX, SourceY,
      DestinationX, DestinationY, Width, Height, Delta);
  gBS->RestoreTPL(Tpl);

  return RETURN_ERROR(Status) ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}

STATIC
EFI_STATUS
SetSimpleFrameBufferMemoryAttributes(
    IN EFI_PHYSICAL_ADDRESS FrameBufferBase,
    IN UINT64 FrameBufferSize)
{
  EFI_CPU_ARCH_PROTOCOL *CpuArch = NULL;
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  AlignedBase;
  UINT64                AlignedSize;

  Status = UemuFdtAlignRange (
             FrameBufferBase,
             FrameBufferSize,
             &AlignedBase,
             &AlignedSize
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->LocateProtocol(&gEfiCpuArchProtocolGuid, NULL, (VOID **)&CpuArch);
  if (EFI_ERROR(Status) || CpuArch == NULL) {
    DEBUG((DEBUG_WARN, "%a: gEfiCpuArchProtocolGuid not available\n", __func__));
    return Status;
  }

  Status = CpuArch->SetMemoryAttributes(
      CpuArch,
      AlignedBase,
      AlignedSize,
      EFI_MEMORY_WT | EFI_MEMORY_XP);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN,
           "%a: Failed to set framebuffer memory attributes (WT | XP): %r\n",
           __func__,
           Status));
  }

  return Status;
}

STATIC
EFI_STATUS
GetSimpleFrameBuffer (
  OUT EFI_PHYSICAL_ADDRESS  *Base,
  OUT UINT64                *Size,
  OUT UINT32                *Width,
  OUT UINT32                *Height,
  OUT UINT32                *Stride
  )
{
  EFI_STATUS   Status;
  CONST VOID   *Fdt;
  INT32        Node;
  CONST CHAR8  *Format;
  UINT64       RequiredSize;

  Status = UemuFdtGet (&Fdt);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = UemuFdtFindCompatibleNode (Fdt, "simple-framebuffer", &Node);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = UemuFdtGetReg (Fdt, Node, 0, Base, Size);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = UemuFdtGetU32 (Fdt, Node, "width", Width);
  if (!EFI_ERROR (Status)) {
    Status = UemuFdtGetU32 (Fdt, Node, "height", Height);
  }

  if (!EFI_ERROR (Status)) {
    Status = UemuFdtGetU32 (Fdt, Node, "stride", Stride);
  }

  if (!EFI_ERROR (Status)) {
    Status = UemuFdtGetString (Fdt, Node, "format", &Format, NULL);
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((*Width == 0) || (*Height == 0) ||
      ((*Stride % FB_BYTES_PER_PIXEL) != 0) ||
      (*Width > MAX_UINT32 / FB_BYTES_PER_PIXEL) ||
      (*Stride < *Width * FB_BYTES_PER_PIXEL) ||
      (*Height > MAX_UINT64 / *Stride) ||
      (AsciiStrCmp (Format, "x8r8g8b8") != 0))
  {
    return EFI_COMPROMISED_DATA;
  }

  RequiredSize = MultU64x32 (*Stride, *Height);
  if ((RequiredSize > *Size) || (RequiredSize > MAX_UINTN) || (*Base > MAX_UINTN)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SimpleFbDxeInitialize(
    IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{

  EFI_STATUS Status             = EFI_SUCCESS;
  EFI_HANDLE hUEFIDisplayHandle = NULL;

  EFI_PHYSICAL_ADDRESS FrameBufferAddress;
  UINT64               FrameBufferRangeSize;
  UINT64               FrameBufferSize;
  UINT32               FrameBufferWidth;
  UINT32               FrameBufferHeight;
  UINT32               FrameBufferStride;

  Status = GetSimpleFrameBuffer (
             &FrameBufferAddress,
             &FrameBufferRangeSize,
             &FrameBufferWidth,
             &FrameBufferHeight,
             &FrameBufferStride
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "%a: simple-framebuffer unavailable: %r\n", __func__, Status));
    return Status;
  }

  FrameBufferSize = MultU64x32 (FrameBufferStride, FrameBufferHeight);

  /* Prepare struct */
  if (mDisplay.Mode == NULL) {
    Status = gBS->AllocatePool(
        EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE),
        (VOID **)&mDisplay.Mode);

    ASSERT_EFI_ERROR(Status);
    if (EFI_ERROR(Status))
      return Status;

    ZeroMem(mDisplay.Mode, sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE));
  }

  if (mDisplay.Mode->Info == NULL) {
    Status = gBS->AllocatePool(
        EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
        (VOID **)&mDisplay.Mode->Info);

    ASSERT_EFI_ERROR(Status);
    if (EFI_ERROR(Status))
      return Status;

    ZeroMem(mDisplay.Mode->Info, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION));
  }

  /* Set information */
  mDisplay.Mode->MaxMode       = 1;
  mDisplay.Mode->Mode          = 0;
  mDisplay.Mode->Info->Version = 0;

  mDisplay.Mode->Info->HorizontalResolution = FrameBufferWidth;
  mDisplay.Mode->Info->VerticalResolution   = FrameBufferHeight;

  mDisplay.Mode->Info->PixelsPerScanLine = FrameBufferStride / FB_BYTES_PER_PIXEL;
  mDisplay.Mode->Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  mDisplay.Mode->SizeOfInfo      = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  mDisplay.Mode->FrameBufferBase = FrameBufferAddress;
  mDisplay.Mode->FrameBufferSize = FrameBufferSize;

  /* Memory property configuration */
  SetSimpleFrameBufferMemoryAttributes(
      (EFI_PHYSICAL_ADDRESS)FrameBufferAddress,
      FrameBufferRangeSize);

  /* Create the FrameBufferBltLib configuration. */
  Status = FrameBufferBltConfigure(
      (VOID *)(UINTN)mDisplay.Mode->FrameBufferBase, mDisplay.Mode->Info,
      mFrameBufferBltLibConfigure, &mFrameBufferBltLibConfigureSize);

  if (Status == RETURN_BUFFER_TOO_SMALL) {
    mFrameBufferBltLibConfigure = AllocatePool(mFrameBufferBltLibConfigureSize);
    if (mFrameBufferBltLibConfigure != NULL) {
      Status = FrameBufferBltConfigure(
          (VOID *)(UINTN)mDisplay.Mode->FrameBufferBase, mDisplay.Mode->Info,
          mFrameBufferBltLibConfigure, &mFrameBufferBltLibConfigureSize);
    }
  }
  ASSERT_EFI_ERROR(Status);

  DEBUG((EFI_D_INFO, "SimpleFbDxe: FB at 0x%08llX, %dx%d, stride %u\n",
         FrameBufferAddress, FrameBufferWidth, FrameBufferHeight,
         FrameBufferStride));
  ZeroMem((VOID *)(UINTN)FrameBufferAddress, FrameBufferSize);

  /* Register handle */
  Status = gBS->InstallMultipleProtocolInterfaces(
      &hUEFIDisplayHandle, &gEfiDevicePathProtocolGuid, &mDisplayDevicePath,
      &gEfiGraphicsOutputProtocolGuid, &mDisplay, NULL);

  ASSERT_EFI_ERROR(Status);

  return Status;
}
