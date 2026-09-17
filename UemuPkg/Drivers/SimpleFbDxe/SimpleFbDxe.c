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
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Library/UemuFdtLib.h>

#include <Protocol/GraphicsOutput.h>
#include <Protocol/Cpu.h>

#define POS_TO_FB(posX, posY)                                                  \
  ((UINT8                                                                      \
        *)((UINTN)This->Mode->FrameBufferBase + (posY)*This->Mode->Info->PixelsPerScanLine * FB_BYTES_PER_PIXEL + (posX)*FB_BYTES_PER_PIXEL))

#define FB_BITS_PER_PIXEL (32)
#define FB_BYTES_PER_PIXEL (FB_BITS_PER_PIXEL / 8)

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
    IN UINTN FrameBufferSize)
{
  EFI_CPU_ARCH_PROTOCOL *CpuArch = NULL;
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  WindowBase;
  UINT64                WindowSize;

  Status = gBS->LocateProtocol(&gEfiCpuArchProtocolGuid, NULL, (VOID **)&CpuArch);
  if (EFI_ERROR(Status) || CpuArch == NULL) {
    DEBUG((DEBUG_WARN, "%a: gEfiCpuArchProtocolGuid not available\n", __func__));
    return Status;
  }

  /*
   * The stride comes from the device tree, so the framebuffer length need
   * not be a whole number of pages; the CPU attributes only apply to whole
   * pages, so widen a copy of the range to cover the framebuffer.
   */
  WindowBase = FrameBufferBase;
  WindowSize = FrameBufferSize;
  UemuFdtRegionToPageWindow(&WindowBase, &WindowSize);

  Status = CpuArch->SetMemoryAttributes(
      CpuArch,
      WindowBase,
      (UINTN)WindowSize,
      EFI_MEMORY_WT | EFI_MEMORY_XP);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN,
           "%a: Failed to set framebuffer memory attributes (WT | XP): %r\n",
           __func__,
           Status));
  }

  return Status;
}

EFI_STATUS
EFIAPI
SimpleFbDxeInitialize(
    IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS            Status;
  EFI_HANDLE            hUEFIDisplayHandle = NULL;
  CONST CHAR8           *Format;
  INT32                 Node;
  UINT32                Width;
  UINT32                Height;
  UINT32                Stride;
  UINT32                PixelsPerScanLine;
  UINT64                RegionSize;
  UINT64                FrameBufferSize;
  EFI_PHYSICAL_ADDRESS  FrameBufferBase;

  /*
   * The framebuffer is one of the devices uemu-ng creates, so its address
   * and geometry come from the device tree; nothing here guesses a layout.
   */
  Status = UemuFdtFindNode("simple-framebuffer", &Node);
  if (Status == EFI_NOT_FOUND) {
    DEBUG((DEBUG_INFO, "SimpleFbDxe: no simple-framebuffer device\n"));
    return EFI_NOT_FOUND;
  }

  if (EFI_ERROR(Status))
    return Status;

  Status = UemuFdtNodeReg(Node, &FrameBufferBase, &RegionSize);
  if (EFI_ERROR(Status))
    return Status;

  Status = UemuFdtGetUint32(Node, "width", &Width);
  if (EFI_ERROR(Status))
    return Status;

  Status = UemuFdtGetUint32(Node, "height", &Height);
  if (EFI_ERROR(Status))
    return Status;

  Status = UemuFdtGetUint32(Node, "stride", &Stride);
  if (EFI_ERROR(Status))
    return Status;

  Status = UemuFdtGetString(Node, "format", &Format);
  if (EFI_ERROR(Status))
    return Status;

  /* uemu-ng SimpleFB runs on XRGB 8:8:8:8 */
  if (AsciiStrCmp(Format, "x8r8g8b8") != 0) {
    DEBUG((DEBUG_ERROR, "SimpleFbDxe: unsupported format '%a'\n", Format));
    return EFI_UNSUPPORTED;
  }

  /*
   * The stride is the line pitch in bytes, so it has to cover the visible
   * pixels; it may be wider than they need.
   */
  if (Width == 0 || Height == 0 || Stride == 0 ||
      (Stride % FB_BYTES_PER_PIXEL) != 0) {
    DEBUG((DEBUG_ERROR,
           "SimpleFbDxe: bad geometry %ux%u stride %u\n",
           Width, Height, Stride));
    return EFI_DEVICE_ERROR;
  }

  PixelsPerScanLine = Stride / FB_BYTES_PER_PIXEL;
  if (PixelsPerScanLine < Width) {
    DEBUG((DEBUG_ERROR,
           "SimpleFbDxe: stride %u is narrower than %u pixels\n",
           Stride, Width));
    return EFI_DEVICE_ERROR;
  }

  FrameBufferSize = (UINT64)Stride * Height;
  if (FrameBufferSize > RegionSize || FrameBufferSize > MAX_UINT32) {
    DEBUG((DEBUG_ERROR,
           "SimpleFbDxe: framebuffer needs 0x%llx bytes, region gives 0x%llx\n",
           FrameBufferSize, RegionSize));
    return EFI_DEVICE_ERROR;
  }

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

  mDisplay.Mode->Info->HorizontalResolution = Width;
  mDisplay.Mode->Info->VerticalResolution   = Height;

  mDisplay.Mode->Info->PixelsPerScanLine = PixelsPerScanLine;
  mDisplay.Mode->Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  mDisplay.Mode->SizeOfInfo      = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  mDisplay.Mode->FrameBufferBase = FrameBufferBase;
  mDisplay.Mode->FrameBufferSize = (UINTN)FrameBufferSize;

  /* Memory property configuration */
  SetSimpleFrameBufferMemoryAttributes(
      FrameBufferBase, (UINTN)FrameBufferSize);

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

  DEBUG((DEBUG_INFO, "SimpleFbDxe: FB at 0x%08llX, %ux%u stride %u\n",
         FrameBufferBase, Width, Height, Stride));
  ZeroMem((VOID *)(UINTN)FrameBufferBase, (UINTN)FrameBufferSize);

  /* Register handle */
  Status = gBS->InstallMultipleProtocolInterfaces(
      &hUEFIDisplayHandle, &gEfiDevicePathProtocolGuid, &mDisplayDevicePath,
      &gEfiGraphicsOutputProtocolGuid, &mDisplay, NULL);

  ASSERT_EFI_ERROR(Status);

  return Status;
}
