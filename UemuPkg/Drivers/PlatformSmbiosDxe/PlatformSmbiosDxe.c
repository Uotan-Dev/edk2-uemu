/** @file
  Static SMBIOS Table for the Uotan RISC-V Emulator platform

  Derived from:
  - EmulatorPkg/PlatformSmbiosDxe (Apple Inc., BSD-2-Clause-Patent)
  - GunyahPkg/Drivers/SmbiosPlatformDxe (DroidVM, BSD-2-Clause-Patent)

  Copyright (c) 2026 Nuo Shen, Nanjing University
  Copyright (c) 2012, Apple Inc. All rights reserved.
  Copyright (c) 2020, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Guid/FdtHob.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Protocol/FdtClient.h>
#include <Protocol/Smbios.h>

/***********************************************************************
        SMBIOS data definition  TYPE0  BIOS Information
************************************************************************/
SMBIOS_TABLE_TYPE0 mBIOSInfoType0 = {
  { EFI_SMBIOS_TYPE_BIOS_INFORMATION, sizeof (SMBIOS_TABLE_TYPE0), 0 },
  1,      // Vendor String
  2,      // BiosVersion String
  0,      // BiosSegment
  3,      // BiosReleaseDate String
  0x3F,   // BiosSize (in 64KB) = 4 MiB
  {       // BiosCharacteristics
    0,    //  Reserved                          :2;  ///< Bits 0-1.
    0,    //  Unknown                           :1;
    0,    //  BiosCharacteristicsNotSupported   :1;
    0,    //  IsaIsSupported                    :1;
    0,    //  McaIsSupported                    :1;
    0,    //  EisaIsSupported                   :1;
    0,    //  PciIsSupported                    :1; /// No PCIe on this platform
    0,    //  PcmciaIsSupported                 :1;
    1,    //  PlugAndPlayIsSupported            :1;
    0,    //  ApmIsSupported                    :1;
    1,    //  BiosIsUpgradable                  :1;
    0,    //  BiosShadowingAllowed              :1;
    0,    //  VlVesaIsSupported                 :1;
    0,    //  EscdSupportIsAvailable            :1;
    0,    //  BootFromCdIsSupported             :1;
    1,    //  SelectableBootIsSupported         :1;
    0,    //  RomBiosIsSocketed                 :1;
    0,    //  BootFromPcmciaIsSupported         :1;
    0,    //  EDDSpecificationIsSupported       :1;
    0,    //  JapaneseNecFloppyIsSupported      :1;
    0,    //  JapaneseToshibaFloppyIsSupported  :1;
    0,    //  Floppy525_360IsSupported          :1;
    0,    //  Floppy525_12IsSupported           :1;
    0,    //  Floppy35_720IsSupported           :1;
    0,    //  Floppy35_288IsSupported           :1;
    0,    //  PrintScreenIsSupported            :1;
    0,    //  Keyboard8042IsSupported           :1;
    1,    //  SerialIsSupported                 :1;
    0,    //  PrinterIsSupported                :1;
    0,    //  CgaMonoIsSupported                :1;
    0,    //  NecPc98                           :1;
    0     //  ReservedForVendor                 :32; ///< Bits 32-63.
  },
  {       // BIOSCharacteristicsExtensionBytes[]
    0x03, //  AcpiIsSupported                   :1;
          //  UsbLegacyIsSupported              :1;
          //  AgpIsSupported                    :1;
          //  I2OBootIsSupported                :1;
          //  Ls120BootIsSupported              :1;
          //  AtapiZipDriveBootIsSupported      :1;
          //  Boot1394IsSupported               :1;
          //  SmartBatteryIsSupported           :1;
          //  BIOSCharacteristicsExtensionBytes[1]
    0x0C, //  BiosBootSpecIsSupported              :1;
          //  FunctionKeyNetworkBootIsSupported    :1;
          //  TargetContentDistributionEnabled     :1;
          //  UefiSpecificationSupported           :1;
          //  VirtualMachineSupported              :1;
          //  ExtensionByte2Reserved               :3;
  },
  0,    // SystemBiosMajorRelease
  0,    // SystemBiosMinorRelease
  0xFF, // EmbeddedControllerFirmwareMajorRelease
  0xFF, // EmbeddedControllerFirmwareMinorRelease
};

CHAR8 mBiosVendor[128]  = "Uotan";
CHAR8 mBiosVersion[128] = "edk2-uemu";
CHAR8 mBiosDate[12]     = __DATE__;

CHAR8 *mBIOSInfoType0Strings[] = {
  mBiosVendor,  // Vendor
  mBiosVersion, // Version
  mBiosDate,    // Release Date
  NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE1  System Information
************************************************************************/
SMBIOS_TABLE_TYPE1 mSysInfoType1 = {
  { EFI_SMBIOS_TYPE_SYSTEM_INFORMATION, sizeof (SMBIOS_TABLE_TYPE1), 0 },
  1, // Manufacturer String
  2, // ProductName String
  3, // Version String
  4, // SerialNumber String
  { 0xED6C6B3D, 0xFB78, 0x4B97, { 0xBB, 0x3A, 0xB8, 0x8C, 0x7E, 0xBE, 0xD5, 0x4A }},
  SystemWakeupTypePowerSwitch,
  5, // SKUNumber String
  6, // Family String
};

CHAR8 mSysInfoManufName[128]          = "Uotan";
CHAR8 mSysInfoProductName[128]        = "Uotan RISC-V Emulator";
CHAR8 mSysInfoVersionName[128]        = "1.1";
CHAR8 mSysInfoSerial[sizeof (UINT64) * 2 + 1] = "Not Specified";
CHAR8 mSysInfoSKU[sizeof (UINT64) * 2 + 1]    = "Not Specified";

CHAR8 *mSysInfoType1Strings[] = {
  mSysInfoManufName,
  mSysInfoProductName,
  mSysInfoVersionName,
  mSysInfoSerial,
  mSysInfoSKU,
  "Virtual Machine",
  NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE2  Board Information
************************************************************************/
SMBIOS_TABLE_TYPE2 mBoardInfoType2 = {
  { EFI_SMBIOS_TYPE_BASEBOARD_INFORMATION, sizeof (SMBIOS_TABLE_TYPE2), 0 },
  1, // Manufacturer String
  2, // ProductName String
  3, // Version String
  4, // SerialNumber String
  5, // AssetTag String
  {     // FeatureFlag
    1,  //  Motherboard           :1;
    0,  //  RequiresDaughterCard  :1;
    0,  //  Removable             :1;
    0,  //  Replaceable           :1;
    0,  //  HotSwappable          :1;
    0,  //  Reserved              :3;
  },
  6,                        // LocationInChassis String
  0,                        // ChassisHandle;
  BaseBoardTypeMotherBoard, // BoardType;
  0,                        // NumberOfContainedObjectHandles;
  { 0 }                     // ContainedObjectHandles[1];
};

CHAR8 mChassisAssetTag[128];

CHAR8 *mBoardInfoType2Strings[] = {
  mSysInfoManufName,
  mSysInfoProductName,
  mSysInfoVersionName,
  mSysInfoSerial,
  mChassisAssetTag,
  "Virtual Machine",
  NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE3  Enclosure Information
************************************************************************/
SMBIOS_TABLE_TYPE3 mEnclosureInfoType3 = {
  { EFI_SMBIOS_TYPE_SYSTEM_ENCLOSURE, sizeof (SMBIOS_TABLE_TYPE3), 0 },
  1,                         // Manufacturer String
  MiscChassisTypeDeskTop,    // Type;
  2,                         // Version String
  3,                         // SerialNumber String
  4,                         // AssetTag String
  ChassisStateSafe,          // BootupState;
  ChassisStateSafe,          // PowerSupplyState;
  ChassisStateSafe,          // ThermalState;
  ChassisSecurityStatusNone, // SecurityStatus;
  { 0, 0, 0, 0 },           // OemDefined[4];
  1,                         // Height;
  1,                         // NumberofPowerCords;
  0,                         // ContainedElementCount;
  0,                         // ContainedElementRecordLength;
  {{ 0 }},                   // ContainedElements[1];
};
CHAR8 *mEnclosureInfoType3Strings[] = {
  mSysInfoManufName,
  mSysInfoProductName,
  mSysInfoSerial,
  mChassisAssetTag,
  NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE4  Processor Information
************************************************************************/
SMBIOS_TABLE_TYPE4 mProcessorInfoType4 = {
  { EFI_SMBIOS_TYPE_PROCESSOR_INFORMATION, sizeof (SMBIOS_TABLE_TYPE4), 0 },
  1,                               // Socket String
  CentralProcessor,                // ProcessorType;
  ProcessorFamilyIndicatorFamily2, // ProcessorFamily; (actual value in Family2)
  2,                               // ProcessorManufacture String;
  {                                // ProcessorId;
    { 0, 0, 0, 0 }
  },
  3, // ProcessorVersion String;
  {       // Voltage;
    0,    // ProcessorVoltageCapability5V        :1;
    0,    // ProcessorVoltageCapability3_3V      :1;
    0,    // ProcessorVoltageCapability2_9V      :1;
    0,    // ProcessorVoltageCapabilityReserved  :1; ///< Bit 3, must be zero.
    0,    // ProcessorVoltageReserved            :3; ///< Bits 4-6, must be zero.
    1     // ProcessorVoltageIndicateLegacy      :1;
  },
  0,                      // ExternalClock;
  0,                      // MaxSpeed; (unknown — no PMU on virtual RISC-V)
  0,                      // CurrentSpeed; (unknown)
  0x41,                   // Status; (CPU Enabled, Socket Populated)
  ProcessorUpgradeOther,   // ProcessorUpgrade;
  0xFFFF,                 // L1CacheHandle;
  0xFFFF,                 // L2CacheHandle;
  0xFFFF,                 // L3CacheHandle;
  0,                      // SerialNumber;
  0,                      // AssetTag;
  4,                      // PartNumber;
  1,                      // CoreCount; (patched at runtime via FDT)
  1,                      // EnabledCoreCount; (patched at runtime)
  0,                      // ThreadCount;
  0xEC,                   // ProcessorCharacteristics;
  ProcessorFamilyRiscVRV64, // ProcessorFamily2;
  0,                      // CoreCount2;
  0,                      // EnabledCoreCount2;
  0,                      // ThreadCount2;
  0,                      // ThreadEnabled;
  0,                      // SocketType;
};

CHAR8 mProcessorVersion[128] = "rv64imafdc";

CHAR8 *mProcessorInfoType4Strings[] = {
  "CPU0",   // Socket
  "Uotan",  // Manufacturer
  mProcessorVersion, // Version (populated from FDT riscv,isa)
  "Virtual RISC-V 64-bit Processor", // PartNumber
  NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE11  OEM Strings
************************************************************************/
SMBIOS_TABLE_TYPE11 mOemStringsType11 = {
  { EFI_SMBIOS_TYPE_OEM_STRINGS, sizeof (SMBIOS_TABLE_TYPE11), 0 },
  1 // StringCount
};
CHAR8 *mOemStringsType11Strings[] = {
  "https://github.com/Uotan-Dev/uotan_riscv_emu-ng",
  NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE16  Physical Memory Array Information
************************************************************************/
SMBIOS_TABLE_TYPE16 mPhyMemArrayInfoType16 = {
  { EFI_SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY, sizeof (SMBIOS_TABLE_TYPE16), 0 },
  MemoryArrayLocationSystemBoard, // Location;
  MemoryArrayUseSystemMemory,     // Use;
  MemoryErrorCorrectionNone,      // MemoryErrorCorrection;
  0x80000000,                     // MaximumCapacity; (2 TB, will use Extended)
  0xFFFE,                         // MemoryErrorInformationHandle;
  1,                              // NumberOfMemoryDevices;
  0x00000000ULL,                  // ExtendedMaximumCapacity;
};
CHAR8 *mPhyMemArrayInfoType16Strings[] = { NULL };

/***********************************************************************
        SMBIOS data definition  TYPE17  Memory Device Information
************************************************************************/
SMBIOS_TABLE_TYPE17 mMemDevInfoType17 = {
  { EFI_SMBIOS_TYPE_MEMORY_DEVICE, sizeof (SMBIOS_TABLE_TYPE17), 0 },
  0,                       // MemoryArrayHandle; (patched at runtime)
  0xFFFE,                  // MemoryErrorInformationHandle;
  64,                      // TotalWidth; (unknown)
  64,                      // DataWidth; (unknown)
  0x2000,                  // Size; (512 MB placeholder, patched at runtime)
  MemoryFormFactorOther,   // FormFactor;
  0,                       // DeviceSet;
  1,                       // DeviceLocator String
  2,                       // BankLocator String
  MemoryTypeOther,         // MemoryType;
  {     // TypeDetail;
    0,  // Reserved        :1;
    0,  // Other           :1;
    0,  // Unknown         :1;
    0,  // FastPaged       :1;
    0,  // StaticColumn    :1;
    0,  // PseudoStatic    :1;
    0,  // Rambus          :1;
    0,  // Synchronous     :1;
    0,  // Cmos            :1;
    0,  // Edo             :1;
    0,  // WindowDram      :1;
    0,  // CacheDram       :1;
    0,  // Nonvolatile     :1;
    0,  // Registered      :1;
    1,  // Unbuffered      :1;
    0,  // Reserved1       :1;
  },
  0,                      // Speed; (unknown)
  3,                      // Manufacturer String
  0,                      // SerialNumber String
  0,                      // AssetTag String
  0,                      // PartNumber String
  0,                      // Attributes; (unknown rank)
  0,                      // ExtendedSize; (patched at runtime if >= 32GB)
  0,                      // ConfiguredMemoryClockSpeed; (unknown)
  0,                      // MinimumVoltage; (unknown)
  0,                      // MaximumVoltage; (unknown)
  0,                      // ConfiguredVoltage; (unknown)
  MemoryTechnologyDram,   // MemoryTechnology
  {{                      // MemoryOperatingModeCapability
    0,                    // Reserved                        :1;
    0,                    // Other                           :1;
    0,                    // Unknown                         :1;
    1,                    // VolatileMemory                  :1;
    0,                    // ByteAccessiblePersistentMemory  :1;
    0,                    // BlockAccessiblePersistentMemory :1;
    0                     // Reserved                        :10;
  }},
  0,                     // FirwareVersion
  0,                     // ModuleManufacturerID (unknown)
  0,                     // ModuleProductID (unknown)
  0,                     // MemorySubsystemControllerManufacturerID (unknown)
  0,                     // MemorySubsystemControllerProductID (unknown)
  0,                     // NonVolatileSize
  0xFFFFFFFFFFFFFFFFULL, // VolatileSize; (patched at runtime)
  0,                     // CacheSize
  0,                     // LogicalSize
  0,                     // ExtendedSpeed,
  0                      // ExtendedConfiguredMemorySpeed
};
CHAR8 *mMemDevInfoType17Strings[] = {
  "DIMM 0",
  "BANK 0",
  "Uotan",
  NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE19  Memory Array Mapped Address
************************************************************************/
SMBIOS_TABLE_TYPE19 mMemArrMapInfoType19 = {
  { EFI_SMBIOS_TYPE_MEMORY_ARRAY_MAPPED_ADDRESS,
    sizeof (SMBIOS_TABLE_TYPE19), 0 },
  0xFFFFFFFF, // StartingAddress;
  0xFFFFFFFF, // EndingAddress;
  0, // MemoryArrayHandle; (patched at runtime)
  1, // PartitionWidth;
  0, // ExtendedStartingAddress;
  0, // ExtendedEndingAddress;
};
CHAR8 *mMemArrMapInfoType19Strings[] = { NULL };

/***********************************************************************
        SMBIOS data definition  TYPE32  Boot Information
************************************************************************/
SMBIOS_TABLE_TYPE32 mBootInfoType32 = {
  { EFI_SMBIOS_TYPE_SYSTEM_BOOT_INFORMATION, sizeof (SMBIOS_TABLE_TYPE32), 0 },
  { 0, 0, 0, 0, 0, 0 },          // Reserved[6];
  BootInformationStatusNoError    // BootStatus
};
CHAR8 *mBootInfoType32Strings[] = { NULL };

/**
  Create an SMBIOS record.

  Converts a fixed SMBIOS structure and an array of pointers to strings
  into an SMBIOS record where the strings are concatenated on the end
  of the fixed record and terminated via a double NULL, then adds the
  record to the SMBIOS table.

  @param  Template    Fixed SMBIOS structure, required.
  @param  StringPack  Array of strings to convert to an SMBIOS string pack.
                      NULL is OK.
  @param  DataSmbiosHandle  The new SMBIOS record handle. NULL is OK.
**/
STATIC
EFI_STATUS
LogSmbiosData (
  IN  EFI_SMBIOS_TABLE_HEADER  *Template,
  IN  CHAR8                    **StringPack,
  OUT EFI_SMBIOS_HANDLE        *DataSmbiosHandle
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

  //
  // Locate Smbios protocol.
  //
  Status = gBS->LocateProtocol (
                  &gEfiSmbiosProtocolGuid,
                  NULL,
                  (VOID **)&Smbios
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Calculate the size of the fixed record and optional string pack
  Size = Template->Length;
  if (StringPack == NULL) {
    // At least a double null is required
    Size += 2;
  } else {
    for (Index = 0; StringPack[Index] != NULL; Index++) {
      StringSize = AsciiStrSize (StringPack[Index]);
      Size += StringSize;
    }
    if (StringPack[0] == NULL) {
      // At least a double null is required
      Size += 1;
    }

    // Don't forget the terminating double null
    Size += 1;
  }

  // Copy over Template
  Record = (EFI_SMBIOS_TABLE_HEADER *)AllocateZeroPool (Size);
  if (Record == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (Record, Template, Template->Length);

  // Append string pack
  Str = ((CHAR8 *)Record) + Record->Length;

  if (StringPack != NULL) {
    for (Index = 0; StringPack[Index] != NULL; Index++) {
      StringSize = AsciiStrSize (StringPack[Index]);
      CopyMem (Str, StringPack[Index], StringSize);
      Str += StringSize;
    }
  }

  *Str         = 0;
  SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
  Status       = Smbios->Add (Smbios, gImageHandle, &SmbiosHandle, Record);

  if ((Status == EFI_SUCCESS) && (DataSmbiosHandle != NULL)) {
    *DataSmbiosHandle = SmbiosHandle;
  }

  ASSERT_EFI_ERROR (Status);
  FreePool (Record);
  return Status;
}

/**
  Read the timebase frequency from the device tree's /cpus node.

  Uses the FDT GUID HOB to locate the flattened device tree, then
  reads the timebase-frequency property from the /cpus node.

  @return  Timebase frequency in Hz, or 0 on failure.
**/
STATIC
UINT64
GetTimeBaseFrequency (
  VOID
  )
{
  VOID                *Hob;
  CONST VOID          *FdtBase;
  INT32               Node;
  CONST FDT_PROPERTY  *Prop;
  INT32               Len;

  Hob = GetFirstGuidHob (&gFdtHobGuid);
  if (Hob == NULL) {
    DEBUG ((DEBUG_WARN, "%a: no FDT HOB found\n", __func__));
    return 0;
  }

  FdtBase = (CONST VOID *)(UINTN)*(CONST UINT64 *)GET_GUID_HOB_DATA (Hob);
  if (FdtCheckHeader (FdtBase) != 0) {
    DEBUG ((DEBUG_WARN, "%a: invalid FDT header\n", __func__));
    return 0;
  }

  Node = FdtSubnodeOffset (FdtBase, 0, "cpus");
  if (Node < 0) {
    DEBUG ((DEBUG_WARN, "%a: /cpus node not found\n", __func__));
    return 0;
  }

  Prop = FdtGetProperty (FdtBase, Node, "timebase-frequency", &Len);
  if ((Prop == NULL) || (Len != sizeof (UINT32))) {
    DEBUG ((DEBUG_WARN, "%a: timebase-frequency not found\n", __func__));
    return 0;
  }

  //
  // Device-tree cells are big-endian
  //
  return SwapBytes32 (*(CONST UINT32 *)Prop->Data);
}

#define BENCH_SORT_SIZE  256

//
// In-place recursive quicksort with Hoare partition and
// median-of-three pivot selection.
//
STATIC
VOID
QuickSortInt32 (
  INT32  *Array,
  INTN    Left,
  INTN    Right
  )
{
  INTN   I, J;
  INT32  Pivot, Tmp;

  if (Left >= Right) return;

  //
  // Median-of-three pivot
  //
  I = (Left + Right) / 2;
  if (Array[Left] > Array[I]) { Tmp = Array[Left]; Array[Left] = Array[I]; Array[I] = Tmp; }
  if (Array[Left] > Array[Right]) { Tmp = Array[Left]; Array[Left] = Array[Right]; Array[Right] = Tmp; }
  if (Array[I] > Array[Right]) { Tmp = Array[I]; Array[I] = Array[Right]; Array[Right] = Tmp; }
  Pivot = Array[I];

  //
  // Hoare partition
  //
  I = Left;
  J = Right;

  while (1) {
    while (Array[I] < Pivot) I++;
    while (Array[J] > Pivot) J--;
    if (I >= J) break;
    Tmp = Array[I]; Array[I] = Array[J]; Array[J] = Tmp;
    I++; J--;
  }

  QuickSortInt32 (Array, Left, J);
  QuickSortInt32 (Array, J + 1, Right);
}

/**
  Estimate the CPU frequency in MHz using the RISC-V cycle counter
  (rdcycle) and the time CSR (rdtime).

  Reads rdcycle and rdtime, waits approximately 10 ms by spinning
  on rdtime, then reads both again. Computes:
    CPU MHz = DeltaCycles * TimeBaseFreq / (DeltaTime * 1,000,000)

  The cycle counter must be accessible from S-mode (mcounteren.CY = 1),
  which is the standard configuration under OpenSBI.

  @return  Estimated CPU frequency in MHz, or 0 if measurement failed.
**/
STATIC
UINT32
EstimateCpuFrequencyMHz (
  VOID
  )
{
  UINT64   TimeBaseFreq;
  UINT64   StartTime;
  UINT64   EndTime;
  UINT64   StartCycle;
  UINT64   EndCycle;
  UINT64   DeltaTime;
  UINT64   DeltaCycle;
  UINT64   FreqMHz;
  UINT64   Lcg;
  UINTN    Idx;
  INT32    SortArray[BENCH_SORT_SIZE];

  TimeBaseFreq = GetTimeBaseFrequency ();
  if (TimeBaseFreq == 0) {
    return 0;
  }

  DEBUG ((
    DEBUG_INFO,
    "%a: timebase-frequency = %lu Hz\n",
    __func__,
    TimeBaseFreq
    ));

  //
  // Fill array with pseudo-random data via LCG
  //
  Lcg = 1;
  for (Idx = 0; Idx < BENCH_SORT_SIZE; Idx++) {
    Lcg            = Lcg * 1103515245ULL + 12345ULL;
    SortArray[Idx] = (INT32)(Lcg >> 16);
  }

  //
  // Measure: qsort workload, rdtime only at boundaries
  //
  StartTime  = RiscVReadTimer ();
  asm volatile ("rdcycle %0" : "=r" (StartCycle));

  QuickSortInt32 (SortArray, 0, BENCH_SORT_SIZE - 1);

  asm volatile ("rdcycle %0" : "=r" (EndCycle));
  EndTime    = RiscVReadTimer ();

  //
  // Verify the array is sorted (guards against compiler eliminating
  // dead stores)
  //
  for (Idx = 1; Idx < BENCH_SORT_SIZE; Idx++) {
    if (SortArray[Idx - 1] > SortArray[Idx]) {
      DEBUG ((DEBUG_WARN, "%a: sort verification failed at %lu\n",
        __func__, Idx));
      return 0;
    }
  }

  DeltaTime  = EndTime - StartTime;
  DeltaCycle = EndCycle - StartCycle;

  if ((DeltaTime == 0) || (DeltaCycle == 0)) {
    DEBUG ((DEBUG_WARN, "%a: zero delta (time=%lu, cycle=%lu)\n",
      __func__, DeltaTime, DeltaCycle));
    return 0;
  }

  //
  // CPU MHz = DeltaCycles * TimeBaseFreq / (DeltaTime * 1,000,000)
  //
  FreqMHz = DivU64x64Remainder (
              MultU64x64 (DeltaCycle, TimeBaseFreq),
              MultU64x64 (DeltaTime, 1000000ULL),
              NULL
              );

  //
  // Sanity-check: reject implausible values (< 10 MHz or > 10 GHz)
  //
  if ((FreqMHz < 10) || (FreqMHz > 10000)) {
    DEBUG ((DEBUG_WARN, "%a: implausible frequency %lu MHz, discarding\n",
      __func__, FreqMHz));
    return 0;
  }

  DEBUG ((DEBUG_INFO,
    "%a: estimated CPU frequency: %lu MHz (qsort %d elements)\n",
    __func__, FreqMHz, BENCH_SORT_SIZE));
  return (UINT32)FreqMHz;
}

/**
  Log CPU information from the device tree.

  Enumerates "riscv" compatible CPU nodes, counts cores, and reads
  the riscv,isa property for the processor version string. Also
  estimates CPU frequency using rdcycle/rdtime.

  @param  FdtClient  Pointer to the FDT client protocol.
**/
STATIC
VOID
LogCpuInfo (
  IN FDT_CLIENT_PROTOCOL  *FdtClient
  )
{
  EFI_STATUS  Status;
  INT32       Node;
  UINT32      Count;
  CONST VOID  *Prop;
  UINT32      PropSize;

  Count = 0;

  Status = FdtClient->FindCompatibleNode (
                        FdtClient,
                        "riscv",
                        &Node
                        );
  while (!EFI_ERROR (Status)) {
    Count++;

    //
    // Read riscv,isa property from the first CPU node for the
    // processor version string.
    //
    if (Count == 1) {
      Status = FdtClient->GetNodeProperty (
                            FdtClient,
                            Node,
                            "riscv,isa",
                            &Prop,
                            &PropSize
                            );
      if (!EFI_ERROR (Status) && (Prop != NULL) && (PropSize > 0)) {
        //
        // Copy up to sizeof(mProcessorVersion)-1 bytes, ensuring
        // null termination even if the property is longer.
        //
        AsciiStrnCpyS (
          mProcessorVersion,
          sizeof (mProcessorVersion),
          (CONST CHAR8 *)Prop,
          PropSize < sizeof (mProcessorVersion)
            ? PropSize
            : sizeof (mProcessorVersion) - 1
          );
        DEBUG ((
          DEBUG_INFO,
          "%a: CPU riscv,isa = %a\n",
          __func__,
          mProcessorVersion
          ));
      }
    }

    Status = FdtClient->FindNextCompatibleNode (
                          FdtClient,
                          "riscv",
                          Node,
                          &Node
                          );
  }

  if (Count != 0) {
    DEBUG ((
      DEBUG_INFO,
      "%a: Found %u CPU nodes\n",
      __func__,
      Count
      ));
    mProcessorInfoType4.CoreCount = (UINT8)Count;
    mProcessorInfoType4.EnabledCoreCount = (UINT8)Count;
  }

  //
  // Estimate CPU frequency using rdcycle + rdtime
  //
  {
    UINT32  FreqMHz;

    FreqMHz = EstimateCpuFrequencyMHz ();
    if (FreqMHz != 0) {
      mProcessorInfoType4.MaxSpeed     = (UINT16)FreqMHz;
      mProcessorInfoType4.CurrentSpeed = (UINT16)FreqMHz;
    }
  }

  // TYPE4 Processor Information
  LogSmbiosData (
    (EFI_SMBIOS_TABLE_HEADER *)&mProcessorInfoType4,
    mProcessorInfoType4Strings,
    NULL
    );
}

/**
  Log memory information from the device tree.

  Enumerates all /memory nodes via FdtClient and creates Type 19
  (Memory Array Mapped Address) records for each contiguous region.
  Also populates Type 17 (Memory Device) with the total RAM size.

  @param  FdtClient  Pointer to the FDT client protocol.
**/
STATIC
VOID
LogMemoryInfo (
  IN FDT_CLIENT_PROTOCOL  *FdtClient
  )
{
  EFI_STATUS   Status;
  INT32        Node;
  CONST UINT32 *Reg;
  UINT32       RegSize;
  UINTN        AddressCells;
  UINTN        SizeCells;
  UINT64       CurBase;
  UINT64       CurSize;
  UINT64       TotalSize;
  UINT64       TotalSizeInMB;

  TotalSize = 0;

  Status = FdtClient->FindMemoryNodeReg (
                        FdtClient,
                        &Node,
                        (CONST VOID **)&Reg,
                        &AddressCells,
                        &SizeCells,
                        &RegSize
                        );
  while (!EFI_ERROR (Status)) {
    ASSERT (AddressCells <= 2);
    ASSERT (SizeCells <= 2);

    while (RegSize > 0) {
      CurBase = SwapBytes32 (*Reg++);
      if (AddressCells > 1) {
        CurBase = (CurBase << 32) | SwapBytes32 (*Reg++);
      }
      CurSize = SwapBytes32 (*Reg++);
      if (SizeCells > 1) {
        CurSize = (CurSize << 32) | SwapBytes32 (*Reg++);
      }
      RegSize -= (AddressCells + SizeCells) * sizeof (UINT32);
      TotalSize += CurSize;

      DEBUG ((
        DEBUG_INFO,
        "%a: Found memory region: 0x%llx - 0x%llx (length: 0x%llx)\n",
        __func__,
        CurBase,
        CurBase + CurSize - 1,
        CurSize
        ));

      //
      // TYPE19 Memory Array Mapped Address
      // StartingAddress/EndingAddress are in KB per SMBIOS spec
      //
      mMemArrMapInfoType19.StartingAddress = (UINT32)RShiftU64 (CurBase, 10);
      mMemArrMapInfoType19.EndingAddress
        = (UINT32)RShiftU64 (CurBase + CurSize - 1, 10);
      LogSmbiosData (
        (EFI_SMBIOS_TABLE_HEADER *)&mMemArrMapInfoType19,
        mMemArrMapInfoType19Strings,
        NULL
        );
    }

    Status = FdtClient->FindNextMemoryNodeReg (
                          FdtClient,
                          Node,
                          &Node,
                          (CONST VOID **)&Reg,
                          &AddressCells,
                          &SizeCells,
                          &RegSize
                          );
  }

  //
  // TYPE17 Memory Device Information - update size from FDT then log
  //
  if (TotalSize > 0) {
    TotalSizeInMB = RShiftU64 (TotalSize, 20);
    if (TotalSizeInMB >= 0x7FFF) {
      // Size >= 32767 MB: use ExtendedSize field per SMBIOS spec
      mMemDevInfoType17.Size = 0x7FFF;
      mMemDevInfoType17.ExtendedSize = (UINT32)TotalSizeInMB;
    } else {
      mMemDevInfoType17.Size = (UINT16)TotalSizeInMB;
    }
    mMemDevInfoType17.VolatileSize = TotalSize;
  }
  LogSmbiosData (
    (EFI_SMBIOS_TABLE_HEADER *)&mMemDevInfoType17,
    mMemDevInfoType17Strings,
    NULL
    );
}

/**
  Main entry for this driver.

  @param  ImageHandle   Image handle this driver.
  @param  SystemTable   Pointer to SystemTable.

  @retval EFI_SUCCESS   SMBIOS records successfully installed.
**/
EFI_STATUS
EFIAPI
PlatformSmbiosDriverEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS           Status;
  EFI_SMBIOS_HANDLE    SmbiosHandle;
  FDT_CLIENT_PROTOCOL  *FdtClient;

  Status = gBS->LocateProtocol (
                  &gFdtClientProtocolGuid,
                  NULL,
                  (VOID **)&FdtClient
                  );
  ASSERT_EFI_ERROR (Status);

  //
  // TYPE0 BIOS Information
  //
  LogSmbiosData (
    (EFI_SMBIOS_TABLE_HEADER *)&mBIOSInfoType0,
    mBIOSInfoType0Strings,
    NULL
    );

  //
  // TYPE1 System Information
  //
  LogSmbiosData (
    (EFI_SMBIOS_TABLE_HEADER *)&mSysInfoType1,
    mSysInfoType1Strings,
    NULL
    );

  //
  // TYPE3 Enclosure Information
  //
  LogSmbiosData (
    (EFI_SMBIOS_TABLE_HEADER *)&mEnclosureInfoType3,
    mEnclosureInfoType3Strings,
    &SmbiosHandle
    );
  mBoardInfoType2.ChassisHandle = (UINT16)SmbiosHandle;

  //
  // TYPE2 Board Information
  //
  LogSmbiosData (
    (EFI_SMBIOS_TABLE_HEADER *)&mBoardInfoType2,
    mBoardInfoType2Strings,
    NULL
    );

  //
  // TYPE11 OEM Strings
  //
  LogSmbiosData (
    (EFI_SMBIOS_TABLE_HEADER *)&mOemStringsType11,
    mOemStringsType11Strings,
    NULL
    );

  //
  // TYPE16 Physical Memory Array Information
  //
  LogSmbiosData (
    (EFI_SMBIOS_TABLE_HEADER *)&mPhyMemArrayInfoType16,
    mPhyMemArrayInfoType16Strings,
    &SmbiosHandle
    );
  mMemArrMapInfoType19.MemoryArrayHandle = SmbiosHandle;
  mMemDevInfoType17.MemoryArrayHandle    = SmbiosHandle;

  //
  // TYPE4 Processor Information (FDT-based)
  //
  LogCpuInfo (FdtClient);

  //
  // TYPE17 + TYPE19 Memory Information (FDT-based)
  //
  LogMemoryInfo (FdtClient);

  //
  // TYPE32 Boot Information
  //
  LogSmbiosData (
    (EFI_SMBIOS_TABLE_HEADER *)&mBootInfoType32,
    mBootInfoType32Strings,
    NULL
    );

  return EFI_SUCCESS;
}
