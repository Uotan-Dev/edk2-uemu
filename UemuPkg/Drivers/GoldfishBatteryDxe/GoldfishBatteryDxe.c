/** @file
  Goldfish Battery DXE Driver

  Discovers the Goldfish Battery virtual device from FDT (compatible
  "google,goldfish-battery"), reads battery status registers via MMIO,
  installs SMBIOS Type 22 (Portable Battery), and produces a lightweight
  Goldfish Battery protocol for runtime status queries.

  The emulator implements a subset of the Goldfish Battery register set:

    Offset  Name              Description
    ------  ----              -----------
    0x00    INT_STATUS        Pending IRQ status (read to clear)
    0x04    INT_ENABLE        IRQ enable mask (R/W)
    0x08    AC_ONLINE         AC power online (0/1)
    0x0C    STATUS            Battery charging status
    0x10    HEALTH            Battery health
    0x14    PRESENT           Battery present (0/1)
    0x18    CAPACITY          Remaining capacity (percent)

  Additional registers (0x1C - 0x40) are defined by the Linux kernel
  driver but the emulator currently returns 0 for these.

  References:
  - src/device/goldfish_battery.cpp (emulator implementation)
  - drivers/power/supply/goldfish_battery.c (Linux kernel driver)
  - UemuPkg/Drivers/GoldfishEventsDxe/ (DXE driver pattern)

  Copyright (c) 2026 Nuo Shen, Nanjing University

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Protocol/FdtClient.h>
#include <Protocol/Smbios.h>

// ---------------------------------------------------------------------------
// Goldfish Battery MMIO Register Offsets
//
// Matching the register layout from:
//   include/device/goldfish_battery.hpp (emulator)
//   drivers/power/supply/goldfish_battery.c (Linux kernel)
// ---------------------------------------------------------------------------
#define REG_INT_STATUS         0x00
#define REG_INT_ENABLE         0x04
#define REG_AC_ONLINE          0x08
#define REG_STATUS             0x0C
#define REG_HEALTH             0x10
#define REG_PRESENT            0x14
#define REG_CAPACITY           0x18
#define REG_VOLTAGE            0x1C
#define REG_TEMP               0x20
#define REG_CHARGE_COUNTER     0x24
#define REG_VOLTAGE_MAX        0x28
#define REG_CURRENT_MAX        0x2C
#define REG_CURRENT_NOW        0x30
#define REG_CURRENT_AVG        0x34
#define REG_CHARGE_FULL_UAH    0x38
#define REG_CYCLE_COUNT        0x40

// Interrupt source bits
#define BATTERY_STATUS_CHANGED  (1U << 0)
#define AC_STATUS_CHANGED       (1U << 1)
#define BATTERY_INT_MASK        (BATTERY_STATUS_CHANGED | AC_STATUS_CHANGED)

// ---------------------------------------------------------------------------
// Power Supply Status Values (from Linux include/linux/power_supply.h)
// ---------------------------------------------------------------------------
#define PWR_STATUS_UNKNOWN      0
#define PWR_STATUS_CHARGING     1
#define PWR_STATUS_DISCHARGING  2
#define PWR_STATUS_NOT_CHARGING 3
#define PWR_STATUS_FULL         4

#define PWR_HEALTH_UNKNOWN             0
#define PWR_HEALTH_GOOD                1
#define PWR_HEALTH_OVERHEAT            2
#define PWR_HEALTH_DEAD                3
#define PWR_HEALTH_OVERVOLTAGE         4
#define PWR_HEALTH_UNDERVOLTAGE        5
#define PWR_HEALTH_UNSPEC_FAILURE      6
#define PWR_HEALTH_COLD                7
#define PWR_HEALTH_WATCHDOG_EXPIRE     8
#define PWR_HEALTH_SAFETY_TIMER_EXPIRE 9
#define PWR_HEALTH_OVERCURRENT        10
#define PWR_HEALTH_CALIBRATION_REQD   11
#define PWR_HEALTH_WARM               12
#define PWR_HEALTH_COOL               13
#define PWR_HEALTH_HOT                14
#define PWR_HEALTH_NO_BATTERY         15
#define PWR_HEALTH_BLOWN_FUSE         16
#define PWR_HEALTH_CELL_IMBALANCE     17

// ---------------------------------------------------------------------------
// Device constants
// ---------------------------------------------------------------------------
#define GOLDFISH_BATTERY_DEFAULT_BASE  0x10003000ULL
#define GOLDFISH_BATTERY_MMIO_SIZE     0x1000ULL
#define GOLDFISH_BATTERY_COMPATIBLE    "google,goldfish-battery"

// ---------------------------------------------------------------------------
// Goldfish Battery Protocol
//
// A lightweight protocol for querying battery and AC status at runtime.
// EDK2 does not define a standard battery protocol, so we provide our
// own minimal one. Consumers locate it via gGoldfishBatteryProtocolGuid.
// ---------------------------------------------------------------------------
typedef struct {
  UINT32    AcOnline;        // AC power state (1 = online)
  UINT32    Status;          // Charging status (PWR_STATUS_*)
  UINT32    Health;          // Battery health (PWR_HEALTH_*)
  UINT32    Present;         // Battery presence (1 = present)
  UINT32    Capacity;        // Remaining capacity (percent, 0-100)
  UINT32    VoltageNow;      // Present voltage (mV), 0 if unknown
  UINT32    CurrentNow;      // Present current (mA), 0 if unknown
  UINT32    VoltageMax;      // Max design voltage (mV)
  UINT32    CurrentMax;      // Max design current (mA)
  UINT32    Temp;            // Temperature (0.1 °C)
  UINT32    CycleCount;      // Cycle count
  UINT32    ChargeFullUah;   // Full charge capacity (uAh)
  UINT32    ChargeCounter;   // Charge counter
  UINT32    CurrentAvg;      // Average current (mA)
} GOLDFISH_BATTERY_INFO;

typedef
EFI_STATUS
(EFIAPI *GOLDFISH_BATTERY_GET_INFO)(
  OUT GOLDFISH_BATTERY_INFO  *Info
  );

typedef struct {
  GOLDFISH_BATTERY_GET_INFO    GetBatteryInfo;
} GOLDFISH_BATTERY_PROTOCOL;

// Protocol GUID: { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF,
//   0x12, 0x34, 0x56, 0x78, 0x9A } }
#define GOLDFISH_BATTERY_PROTOCOL_GUID \
  { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, \
    0x78, 0x9A } }

EFI_GUID  gGoldfishBatteryProtocolGuid = GOLDFISH_BATTERY_PROTOCOL_GUID;

// ---------------------------------------------------------------------------
// Private Device Structure
// ---------------------------------------------------------------------------
typedef struct {
  UINTN                         Signature;
  EFI_HANDLE                    Handle;
  EFI_PHYSICAL_ADDRESS          MmioBase;
  GOLDFISH_BATTERY_PROTOCOL     Protocol;
} GOLDFISH_BATTERY_DEV;

#define GOLDFISH_BATTERY_SIGNATURE  SIGNATURE_32 ('G', 'f', 'B', 'a')
#define GOLDFISH_BATTERY_FROM_PROTOCOL(a) \
  CR (a, GOLDFISH_BATTERY_DEV, Protocol, GOLDFISH_BATTERY_SIGNATURE)

// ---------------------------------------------------------------------------
// Read a 32-bit MMIO register
// ---------------------------------------------------------------------------
STATIC
UINT32
BatteryRegRead32 (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINT32                Offset
  )
{
  return MmioRead32 (Base + Offset);
}

// ---------------------------------------------------------------------------
// Read all battery registers into a GOLDFISH_BATTERY_INFO struct.
// Reads INT_STATUS first to acknowledge any pending IRQ.
// ---------------------------------------------------------------------------
STATIC
VOID
ReadBatteryInfo (
  IN  EFI_PHYSICAL_ADDRESS     Base,
  OUT GOLDFISH_BATTERY_INFO    *Info
  )
{
  // Acknowledge and clear any pending interrupt
  BatteryRegRead32 (Base, REG_INT_STATUS);

  Info->AcOnline      = BatteryRegRead32 (Base, REG_AC_ONLINE);
  Info->Status        = BatteryRegRead32 (Base, REG_STATUS);
  Info->Health        = BatteryRegRead32 (Base, REG_HEALTH);
  Info->Present       = BatteryRegRead32 (Base, REG_PRESENT);
  Info->Capacity      = BatteryRegRead32 (Base, REG_CAPACITY);
  Info->VoltageNow    = BatteryRegRead32 (Base, REG_VOLTAGE);
  Info->Temp          = BatteryRegRead32 (Base, REG_TEMP);
  Info->ChargeCounter = BatteryRegRead32 (Base, REG_CHARGE_COUNTER);
  Info->VoltageMax    = BatteryRegRead32 (Base, REG_VOLTAGE_MAX);
  Info->CurrentMax    = BatteryRegRead32 (Base, REG_CURRENT_MAX);
  Info->CurrentNow    = BatteryRegRead32 (Base, REG_CURRENT_NOW);
  Info->CurrentAvg    = BatteryRegRead32 (Base, REG_CURRENT_AVG);
  Info->ChargeFullUah = BatteryRegRead32 (Base, REG_CHARGE_FULL_UAH);
  Info->CycleCount    = BatteryRegRead32 (Base, REG_CYCLE_COUNT);
}

// ---------------------------------------------------------------------------
// Goldfish Battery Protocol: GetBatteryInfo
// ---------------------------------------------------------------------------
STATIC
EFI_STATUS
EFIAPI
GetBatteryInfo (
  OUT GOLDFISH_BATTERY_INFO  *Info
  )
{
  GOLDFISH_BATTERY_DEV  *Dev;
  GOLDFISH_BATTERY_PROTOCOL  *Proto;

  //
  // Locate our own protocol to get back to the device context.
  // This is a slight indirection but avoids needing a "This" pointer
  // in the protocol method signature.
  //
  Proto = NULL;
  gBS->LocateProtocol (&gGoldfishBatteryProtocolGuid, NULL,
                       (VOID **)&Proto);

  if (Proto == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: protocol not found\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  Dev = GOLDFISH_BATTERY_FROM_PROTOCOL (Proto);
  if ((Dev == NULL) || (Info == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ReadBatteryInfo (Dev->MmioBase, Info);
  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// SMBIOS Type 22 (Portable Battery) Template
//
// We set DeviceChemistry to LithiumIon (0x06) as the most common virtual
// battery type. DeviceCapacity and DesignVoltage are 0 (unknown) because
// the emulator reports capacity as a percentage, not a mWh/mAh value.
// ---------------------------------------------------------------------------
STATIC SMBIOS_TABLE_TYPE22  mBatteryType22 = {
  {
    EFI_SMBIOS_TYPE_PORTABLE_BATTERY,      // Type
    sizeof (SMBIOS_TABLE_TYPE22),          // Length
    0                                      // Handle (filled by Smbios->Add)
  },
  1,   // Location             (string 1 = "Internal")
  2,   // Manufacturer         (string 2 = "Uotan")
  0,   // ManufactureDate      (0 = not specified)
  0,   // SerialNumber         (0 = not specified)
  3,   // DeviceName           (string 3 = "Virtual Battery")
  0x06,// DeviceChemistry      (LithiumIon)
  0,   // DeviceCapacity       (unknown)
  0,   // DesignVoltage        (unknown)
  0,   // SBDSVersionNumber    (0 = not specified)
  0,   // MaximumErrorInBatteryData
  0,   // SBDSSerialNumber
  0,   // SBDSManufactureDate
  0,   // SBDSDeviceChemistry  (0 = not specified)
  0,   // DesignCapacityMultiplier
  0    // OEMSpecific
};

// ---------------------------------------------------------------------------
// Helper: Log an SMBIOS record via gEfiSmbiosProtocolGuid
//
// Same pattern as PlatformSmbiosDxe's LogSmbiosData().
// ---------------------------------------------------------------------------
STATIC
EFI_STATUS
LogSmbiosRecord (
  IN  EFI_SMBIOS_TABLE_HEADER  *Template,
  IN  CHAR8                    **StringPack
  )
{
  EFI_STATUS              Status;
  EFI_SMBIOS_PROTOCOL     *Smbios;
  EFI_SMBIOS_HANDLE       SmbiosHandle;
  EFI_SMBIOS_TABLE_HEADER *Record;
  UINTN                   Index;
  UINTN                   StringSize;
  UINTN                   Size;
  CHAR8                   *Str;

  Status = gBS->LocateProtocol (
                  &gEfiSmbiosProtocolGuid,
                  NULL,
                  (VOID **)&Smbios
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "%a: Smbios protocol not found: %r\n", __func__,
            Status));
    return Status;
  }

  //
  // Calculate total size: fixed record + string pack + double null
  //
  Size = Template->Length;
  if (StringPack == NULL) {
    Size += 2;
  } else {
    for (Index = 0; StringPack[Index] != NULL; Index++) {
      StringSize = AsciiStrSize (StringPack[Index]);
      Size += StringSize;
    }
    if (StringPack[0] == NULL) {
      Size += 1;
    }
    Size += 1;  // terminating null
  }

  Record = (EFI_SMBIOS_TABLE_HEADER *)AllocateZeroPool (Size);
  if (Record == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (Record, Template, Template->Length);

  //
  // Append string pack after the fixed record
  //
  Str = ((CHAR8 *)Record) + Record->Length;
  if (StringPack != NULL) {
    for (Index = 0; StringPack[Index] != NULL; Index++) {
      StringSize = AsciiStrSize (StringPack[Index]);
      CopyMem (Str, StringPack[Index], StringSize);
      Str += StringSize;
    }
  }
  *Str = 0;

  SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
  Status       = Smbios->Add (Smbios, gImageHandle, &SmbiosHandle, Record);

  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "%a: SMBIOS Type %d installed (handle 0x%x)\n",
            __func__, Template->Type, SmbiosHandle));
  } else {
    DEBUG ((DEBUG_WARN, "%a: Smbios->Add failed: %r\n", __func__, Status));
  }

  FreePool (Record);
  return Status;
}

// ---------------------------------------------------------------------------
// Install SMBIOS Type 22 (Portable Battery)
// ---------------------------------------------------------------------------
STATIC
EFI_STATUS
LogBatterySmbios (
  VOID
  )
{
  CHAR8  *StringPack[] = {
    "Internal",         // 1: Location
    "Goldfish",         // 2: Manufacturer
    "Virtual Battery",  // 3: DeviceName
    NULL
  };

  return LogSmbiosRecord (
           (EFI_SMBIOS_TABLE_HEADER *)&mBatteryType22,
           StringPack
           );
}

// ---------------------------------------------------------------------------
// Discover MMIO base from FDT, with hardcoded fallback
// ---------------------------------------------------------------------------
STATIC
EFI_STATUS
DiscoverMmioBase (
  OUT EFI_PHYSICAL_ADDRESS  *Base
  )
{
  EFI_STATUS           Status;
  FDT_CLIENT_PROTOCOL  *FdtClient;
  CONST UINT32         *RegProp;
  UINTN                AddressCells, SizeCells;
  UINT32               RegSize;

  Status = gBS->LocateProtocol (
                  &gFdtClientProtocolGuid,
                  NULL,
                  (VOID **)&FdtClient
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_INFO,
      "%a: FdtClient protocol not found, using default base 0x%llx\n",
      __func__,
      GOLDFISH_BATTERY_DEFAULT_BASE
      ));
    *Base = GOLDFISH_BATTERY_DEFAULT_BASE;
    return EFI_SUCCESS;
  }

  Status = FdtClient->FindCompatibleNodeReg (
                        FdtClient,
                        GOLDFISH_BATTERY_COMPATIBLE,
                        (CONST VOID **)&RegProp,
                        &AddressCells,
                        &SizeCells,
                        &RegSize
                        );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_INFO,
      "%a: '%a' not found in FDT, using default base 0x%llx\n",
      __func__,
      GOLDFISH_BATTERY_COMPATIBLE,
      GOLDFISH_BATTERY_DEFAULT_BASE
      ));
    *Base = GOLDFISH_BATTERY_DEFAULT_BASE;
    return EFI_SUCCESS;
  }

  //
  // FDT reg is big-endian; SwapBytes32 each cell.
  // #address-cells == 2, #size-cells == 2 => 4 UINT32 cells.
  //
  *Base = LShiftU64 (SwapBytes32 (RegProp[0]), 32)
          | SwapBytes32 (RegProp[1]);

  DEBUG ((
    DEBUG_INFO,
    "%a: Goldfish Battery found in FDT at MMIO 0x%llx\n",
    __func__,
    *Base
    ));

  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// Driver Entry Point
// ---------------------------------------------------------------------------
EFI_STATUS
EFIAPI
GoldfishBatteryDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS               Status;
  GOLDFISH_BATTERY_DEV     *Dev;
  EFI_PHYSICAL_ADDRESS     MmioBase;
  GOLDFISH_BATTERY_INFO    Info;

  //
  // 1. Discover the device via FDT (or fall back to default)
  //
  Status = DiscoverMmioBase (&MmioBase);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // 2. Register the MMIO range in GCD as uncacheable MMIO
  //
  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeMemoryMappedIo,
                  MmioBase,
                  GOLDFISH_BATTERY_MMIO_SIZE,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_WARN,
      "%a: AddMemorySpace returned %r (may already exist)\n",
      __func__,
      Status
      ));
  }

  Status = gDS->SetMemorySpaceAttributes (
                  MmioBase,
                  GOLDFISH_BATTERY_MMIO_SIZE,
                  EFI_MEMORY_UC
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: SetMemorySpaceAttributes failed: %r\n",
      __func__,
      Status
      ));
    return Status;
  }

  //
  // 3. Read initial battery state and log it
  //
  ReadBatteryInfo (MmioBase, &Info);

  DEBUG ((
    DEBUG_INFO,
    "%a: AC=%u Status=%u Health=%u Present=%u Capacity=%u%%\n",
    __func__,
    Info.AcOnline,
    Info.Status,
    Info.Health,
    Info.Present,
    Info.Capacity
    ));

  //
  // 4. Allocate private data
  //
  Dev = AllocateZeroPool (sizeof (*Dev));
  if (Dev == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Dev->Signature = GOLDFISH_BATTERY_SIGNATURE;
  Dev->MmioBase  = MmioBase;

  //
  // 5. Set up the protocol
  //
  Dev->Protocol.GetBatteryInfo = GetBatteryInfo;

  //
  // 6. Install the Goldfish Battery Protocol on a new handle
  //
  Status = gBS->InstallProtocolInterface (
                  &Dev->Handle,
                  &gGoldfishBatteryProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &Dev->Protocol
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: InstallProtocolInterface failed: %r\n",
      __func__,
      Status
      ));
    FreePool (Dev);
    return Status;
  }

  //
  // 7. Install SMBIOS Type 22 (Portable Battery)
  //
  LogBatterySmbios ();

  DEBUG ((
    DEBUG_INFO,
    "%a: GoldfishBatteryDxe initialized successfully\n",
    __func__
    ));

  return EFI_SUCCESS;
}
