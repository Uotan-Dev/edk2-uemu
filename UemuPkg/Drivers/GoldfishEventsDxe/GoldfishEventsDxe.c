/** @file
  Goldfish Events Keyboard Driver

  Produces EFI_SIMPLE_TEXT_INPUT_PROTOCOL for the Goldfish Events virtual
  keyboard device. Discovers the device from FDT (compatible
  "google,goldfish-events-keypad") or falls back to the hardcoded default
  MMIO base 0x10002000.

  Copyright (c) 2026 Nuo Shen, Nanjing University

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/FdtClient.h>
#include <Protocol/SimpleTextIn.h>

// ---------------------------------------------------------------------------
// Goldfish Events Register Protocol
// ---------------------------------------------------------------------------
#define REG_READ     0x00  // Read: dequeue next 32-bit event word
#define REG_SET_PAGE 0x00  // Write: select page
#define REG_LEN      0x04  // Read: page data length
#define REG_DATA     0x08  // Read: page data byte (offset from this base)

#define EV_KEY       0x01
#define EV_ABS       0x03
#define PAGE_ABSDATA (0x20000 | EV_ABS)  // 0x20003

// Default MMIO window; overridden by FDT when available
#define GOLDFISH_EVENTS_DEFAULT_BASE  0x10002000ULL
#define GOLDFISH_EVENTS_SIZE          0x1000ULL
#define GOLDFISH_EVENTS_COMPATIBLE    "google,goldfish-events-keypad"

// Polling interval in 100 ns units (10 ms)
#define KEYBOARD_TIMER_INTERVAL  EFI_TIMER_PERIOD_MILLISECONDS (10)

// ---------------------------------------------------------------------------
// Linux Input Event Codes (subset used by keyboard)
// ---------------------------------------------------------------------------
#define KEY_ESC          1
#define KEY_1            2
#define KEY_2            3
#define KEY_3            4
#define KEY_4            5
#define KEY_5            6
#define KEY_6            7
#define KEY_7            8
#define KEY_8            9
#define KEY_9            10
#define KEY_0            11
#define KEY_MINUS        12
#define KEY_EQUAL        13
#define KEY_BACKSPACE    14
#define KEY_TAB          15
#define KEY_Q            16
#define KEY_W            17
#define KEY_E            18
#define KEY_R            19
#define KEY_T            20
#define KEY_Y            21
#define KEY_U            22
#define KEY_I            23
#define KEY_O            24
#define KEY_P            25
#define KEY_LEFTBRACE    26
#define KEY_RIGHTBRACE   27
#define KEY_ENTER        28
#define KEY_LEFTCTRL     29
#define KEY_A            30
#define KEY_S            31
#define KEY_D            32
#define KEY_F            33
#define KEY_G            34
#define KEY_H            35
#define KEY_J            36
#define KEY_K            37
#define KEY_L            38
#define KEY_SEMICOLON    39
#define KEY_APOSTROPHE   40
#define KEY_GRAVE        41
#define KEY_LEFTSHIFT    42
#define KEY_BACKSLASH    43
#define KEY_Z            44
#define KEY_X            45
#define KEY_C            46
#define KEY_V            47
#define KEY_B            48
#define KEY_N            49
#define KEY_M            50
#define KEY_COMMA        51
#define KEY_DOT          52
#define KEY_SLASH        53
#define KEY_RIGHTSHIFT   54
#define KEY_LEFTALT      56
#define KEY_SPACE        57
#define KEY_CAPSLOCK     58
#define KEY_F1           59
#define KEY_F2           60
#define KEY_F3           61
#define KEY_F4           62
#define KEY_F5           63
#define KEY_F6           64
#define KEY_F7           65
#define KEY_F8           66
#define KEY_F9           67
#define KEY_F10          68
#define KEY_NUMLOCK      69
#define KEY_SCROLLLOCK   70
#define KEY_RIGHTCTRL    97
#define KEY_RIGHTALT     100
#define KEY_HOME         102
#define KEY_UP           103
#define KEY_PAGEUP       104
#define KEY_LEFT         105
#define KEY_RIGHT        106
#define KEY_END          107
#define KEY_DOWN         108
#define KEY_PAGEDOWN     109
#define KEY_INSERT       110
#define KEY_DELETE       111

#define MAX_KEYBOARD_CODE  112

// ---------------------------------------------------------------------------
// Key Mapping Tables (Linux keycode -> UEFI character)
//
// These match the tables in OvmfPkg/VirtioKeyboardDxe/VirtioKeyboard.c
// and follow the Linux input-event-codes.h keycode assignments.
// ---------------------------------------------------------------------------
STATIC CONST UINT16  mKeyMap[] = {
  [KEY_1]          = '1',  '2',  '3', '4', '5', '6', '7', '8', '9', '0',
  [KEY_MINUS]      = '-',  '=',
  [KEY_Q]          = 'q',  'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
  [KEY_LEFTBRACE]  = '[',  ']',
  [KEY_A]          = 'a',  's',  'd', 'f', 'g', 'h', 'j', 'k', 'l',
  [KEY_SEMICOLON]  = ';',  '\'', '`',
  [KEY_BACKSLASH]  = '\\',
  [KEY_Z]          = 'z',  'x',  'c', 'v', 'b', 'n', 'm',
  [KEY_COMMA]      = ',',  '.',  '/',
  [KEY_SPACE]      = ' ',
  [MAX_KEYBOARD_CODE] = 0x00
};

STATIC CONST UINT16  mKeyMapShift[] = {
  [KEY_1]          = '!', '@',  '#', '$', '%', '^', '&', '*', '(', ')',
  [KEY_MINUS]      = '_', '+',
  [KEY_Q]          = 'Q', 'W',  'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
  [KEY_LEFTBRACE]  = '{', '}',
  [KEY_A]          = 'A', 'S',  'D', 'F', 'G', 'H', 'J', 'K', 'L',
  [KEY_SEMICOLON]  = ':', '"',  '~',
  [KEY_BACKSLASH]  = '|',
  [KEY_Z]          = 'Z', 'X',  'C', 'V', 'B', 'N', 'M',
  [KEY_COMMA]      = '<', '>',  '?',
  [KEY_SPACE]      = ' ',
  [MAX_KEYBOARD_CODE] = 0x00
};

// ---------------------------------------------------------------------------
// Device Path
// ---------------------------------------------------------------------------
typedef struct {
  VENDOR_DEVICE_PATH    Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} GOLDFISH_EVENTS_DEVICE_PATH;

// Goldfish Events Keyboard Device Path GUID:
// { 0x4E6B0DA1, 0x8C9A, 0x4F3E, { 0xA7, 0xD2, 0xE5, 0xB1, 0xF9, 0xC6, 0xD8, 0xA3 } }
STATIC GOLDFISH_EVENTS_DEVICE_PATH  mDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      { (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8) }
    },
    { 0x4E6B0DA1, 0x8C9A, 0x4F3E, { 0xA7, 0xD2, 0xE5, 0xB1, 0xF9, 0xC6, 0xD8, 0xA3 } }
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { (UINT8)sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

// ---------------------------------------------------------------------------
// Private Data
// ---------------------------------------------------------------------------
typedef struct {
  UINTN                             Signature;
  EFI_HANDLE                        Handle;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL    SimpleTextIn;
  EFI_EVENT                         TimerEvent;
  EFI_PHYSICAL_ADDRESS              MmioBase;
  BOOLEAN                           KeyShift;   // Shift pressed
  BOOLEAN                           KeyCtrl;    // Ctrl pressed
  EFI_INPUT_KEY                     Key;        // Buffered key
  BOOLEAN                           KeyReady;   // TRUE if Key is valid
} GOLDFISH_EVENTS_DEV;

#define GOLDFISH_EVENTS_SIGNATURE  SIGNATURE_32 ('G', 'f', 'K', 'b')
#define GOLDFISH_EVENTS_FROM_THIS(a) \
  CR (a, GOLDFISH_EVENTS_DEV, SimpleTextIn, GOLDFISH_EVENTS_SIGNATURE)

// ---------------------------------------------------------------------------
// Low-level MMIO Helpers
// ---------------------------------------------------------------------------
STATIC
UINT32
GoldfishRead32 (
  IN GOLDFISH_EVENTS_DEV  *Dev,
  IN UINT32               Offset
  )
{
  return MmioRead32 (Dev->MmioBase + Offset);
}

STATIC
VOID
GoldfishWrite32 (
  IN GOLDFISH_EVENTS_DEV  *Dev,
  IN UINT32               Offset,
  IN UINT32               Value
  )
{
  MmioWrite32 (Dev->MmioBase + Offset, Value);
}

// ---------------------------------------------------------------------------
// Convert Linux keycode + modifier state -> EFI_INPUT_KEY
// ---------------------------------------------------------------------------
STATIC
VOID
ConvertKeyCode (
  IN OUT GOLDFISH_EVENTS_DEV  *Dev,
  IN     UINT16               Code,
  IN     BOOLEAN              Pressed,
  OUT    EFI_INPUT_KEY        *Key
  )
{
  Key->ScanCode    = SCAN_NULL;
  Key->UnicodeChar = CHAR_NULL;

  if (!Pressed) {
    // Key release — track modifier state and return null
    if (Code == KEY_LEFTSHIFT || Code == KEY_RIGHTSHIFT) {
      Dev->KeyShift = FALSE;
    }

    if (Code == KEY_LEFTCTRL || Code == KEY_RIGHTCTRL) {
      Dev->KeyCtrl = FALSE;
    }

    return;
  }

  // Key press — update modifier state
  if (Code == KEY_LEFTSHIFT || Code == KEY_RIGHTSHIFT) {
    Dev->KeyShift = TRUE;
    return;
  }

  if (Code == KEY_LEFTCTRL || Code == KEY_RIGHTCTRL) {
    Dev->KeyCtrl = TRUE;
    return;
  }

  if (Code == KEY_LEFTALT || Code == KEY_RIGHTALT ||
      Code == KEY_CAPSLOCK || Code == KEY_NUMLOCK || Code == KEY_SCROLLLOCK)
  {
    return;
  }

  // Function keys F1–F10
  if ((Code >= KEY_F1) && (Code <= KEY_F10)) {
    Key->ScanCode = SCAN_F1 + (Code - KEY_F1);
    return;
  }

  // Special keys
  switch (Code) {
    case KEY_ESC:
      Key->ScanCode = SCAN_ESC;
      return;

    case KEY_BACKSPACE:
      Key->UnicodeChar = CHAR_BACKSPACE;
      return;

    case KEY_TAB:
      Key->UnicodeChar = CHAR_TAB;
      return;

    case KEY_ENTER:
      Key->UnicodeChar = CHAR_CARRIAGE_RETURN;
      return;

    case KEY_UP:
      Key->ScanCode = SCAN_UP;
      return;

    case KEY_DOWN:
      Key->ScanCode = SCAN_DOWN;
      return;

    case KEY_LEFT:
      Key->ScanCode = SCAN_LEFT;
      return;

    case KEY_RIGHT:
      Key->ScanCode = SCAN_RIGHT;
      return;

    case KEY_HOME:
      Key->ScanCode = SCAN_HOME;
      return;

    case KEY_END:
      Key->ScanCode = SCAN_END;
      return;

    case KEY_PAGEUP:
      Key->ScanCode = SCAN_PAGE_UP;
      return;

    case KEY_PAGEDOWN:
      Key->ScanCode = SCAN_PAGE_DOWN;
      return;

    case KEY_INSERT:
      Key->ScanCode = SCAN_INSERT;
      return;

    case KEY_DELETE:
      Key->ScanCode = SCAN_DELETE;
      return;

    default:
      break;
  }

  // Regular character keys
  if (Code < MAX_KEYBOARD_CODE) {
    if (Dev->KeyShift) {
      Key->ScanCode    = mKeyMapShift[Code];
      Key->UnicodeChar = mKeyMapShift[Code];
    } else {
      Key->ScanCode    = mKeyMap[Code];
      Key->UnicodeChar = mKeyMap[Code];
    }

    // Ctrl masks the character to [1..26]
    if (Dev->KeyCtrl) {
      Key->UnicodeChar &= 0x1F;
    }
  }
}

// ---------------------------------------------------------------------------
// Poll the Goldfish Events device for new key events
// ---------------------------------------------------------------------------
STATIC
VOID
PollKeyboard (
  IN OUT GOLDFISH_EVENTS_DEV  *Dev
  )
{
  UINT32        Type, Code, Value;
  EFI_INPUT_KEY Key;

  for ( ; ; ) {
    Type = GoldfishRead32 (Dev, REG_READ);
    // When queue is empty, dequeue_event() returns 0 (EV_SYN)
    if (Type == 0) {
      break;
    }

    Code  = GoldfishRead32 (Dev, REG_READ);
    Value = GoldfishRead32 (Dev, REG_READ);

    if (Type != EV_KEY) {
      continue;
    }

    ConvertKeyCode (Dev, (UINT16)Code, (Value == 1), &Key);

    // Discard key releases and modifier-only events (ScanCode == SCAN_NULL
    // and UnicodeChar == CHAR_NULL)
    if (Key.ScanCode == SCAN_NULL && Key.UnicodeChar == CHAR_NULL) {
      continue;
    }

    // Buffer only the latest key for ReadKeyStroke
    Dev->Key      = Key;
    Dev->KeyReady = TRUE;
  }
}

// ---------------------------------------------------------------------------
// Timer callback — polls the device for new keystrokes
// ---------------------------------------------------------------------------
STATIC
VOID
EFIAPI
KeyboardTimerHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  GOLDFISH_EVENTS_DEV  *Dev = Context;

  PollKeyboard (Dev);

  if (Dev->KeyReady) {
    gBS->SignalEvent (Dev->SimpleTextIn.WaitForKey);
  }
}

// ---------------------------------------------------------------------------
// EFI_SIMPLE_TEXT_INPUT_PROTOCOL.Reset
// ---------------------------------------------------------------------------
STATIC
EFI_STATUS
EFIAPI
GoldfishEventsReset (
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  IN BOOLEAN                         ExtendedVerification
  )
{
  GOLDFISH_EVENTS_DEV  *Dev = GOLDFISH_EVENTS_FROM_THIS (This);

  Dev->KeyReady = FALSE;
  Dev->KeyShift = FALSE;
  Dev->KeyCtrl  = FALSE;
  ZeroMem (&Dev->Key, sizeof (Dev->Key));

  // Drain any pending events
  while (GoldfishRead32 (Dev, REG_READ) != 0) {
    GoldfishRead32 (Dev, REG_READ);
    GoldfishRead32 (Dev, REG_READ);
  }

  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// EFI_SIMPLE_TEXT_INPUT_PROTOCOL.ReadKeyStroke
// ---------------------------------------------------------------------------
STATIC
EFI_STATUS
EFIAPI
GoldfishEventsReadKeyStroke (
  IN  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  OUT EFI_INPUT_KEY                   *Key
  )
{
  GOLDFISH_EVENTS_DEV  *Dev;
  EFI_TPL              OldTpl;

  if (Key == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dev = GOLDFISH_EVENTS_FROM_THIS (This);

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (Dev->KeyReady) {
    *Key           = Dev->Key;
    Dev->KeyReady  = FALSE;
    gBS->RestoreTPL (OldTpl);
    return EFI_SUCCESS;
  }

  gBS->RestoreTPL (OldTpl);
  return EFI_NOT_READY;
}

// ---------------------------------------------------------------------------
// EFI_SIMPLE_TEXT_INPUT_PROTOCOL.WaitForKey (event notify function)
// ---------------------------------------------------------------------------
STATIC
VOID
EFIAPI
GoldfishEventsWaitForKey (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This = Context;
  GOLDFISH_EVENTS_DEV             *Dev  = GOLDFISH_EVENTS_FROM_THIS (This);

  // Small stall to give other timer-driven drivers a chance to run
  gBS->Stall (1000);

  // Poll the device right now (in case the timer hasn't fired yet)
  PollKeyboard (Dev);

  if (Dev->KeyReady) {
    gBS->SignalEvent (Event);
  }
}

// ---------------------------------------------------------------------------
// Discover the device via FDT or fall back to the hardcoded default
// ---------------------------------------------------------------------------
STATIC
EFI_STATUS
DiscoverMmioBase (
  OUT EFI_PHYSICAL_ADDRESS  *Base
  )
{
  EFI_STATUS                   Status;
  FDT_CLIENT_PROTOCOL          *FdtClient;
  CONST UINT32                 *RegProp;
  UINTN                        AddressCells, SizeCells;
  UINT32                       RegSize;

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
      GOLDFISH_EVENTS_DEFAULT_BASE
      ));
    *Base = GOLDFISH_EVENTS_DEFAULT_BASE;
    return EFI_SUCCESS;
  }

  Status = FdtClient->FindCompatibleNodeReg (
                        FdtClient,
                        GOLDFISH_EVENTS_COMPATIBLE,
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
      GOLDFISH_EVENTS_COMPATIBLE,
      GOLDFISH_EVENTS_DEFAULT_BASE
      ));
    *Base = GOLDFISH_EVENTS_DEFAULT_BASE;
    return EFI_SUCCESS;
  }

  // FDT reg is big-endian; SwapBytes32 each cell.
  // #address-cells == 2, #size-cells == 2 => 4 × UINT32.
  *Base = LShiftU64 (SwapBytes32 (RegProp[0]), 32) | SwapBytes32 (RegProp[1]);

  DEBUG ((
    DEBUG_INFO,
    "%a: Goldfish Events found in FDT at MMIO 0x%llx\n",
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
GoldfishEventsDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS             Status;
  GOLDFISH_EVENTS_DEV    *Dev;
  EFI_PHYSICAL_ADDRESS   MmioBase;

  Status = DiscoverMmioBase (&MmioBase);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Register the MMIO range in GCD as uncacheable MMIO
  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeMemoryMappedIo,
                  MmioBase,
                  GOLDFISH_EVENTS_SIZE,
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
                  GOLDFISH_EVENTS_SIZE,
                  EFI_MEMORY_UC
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: SetMemorySpaceAttributes failed: %r\n",
            __func__, Status));
    return Status;
  }

  Dev = AllocateZeroPool (sizeof (*Dev));
  if (Dev == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Dev->Signature = GOLDFISH_EVENTS_SIGNATURE;
  Dev->MmioBase  = MmioBase;

  // Bring the device into LIVE state: write PAGE_ABSDATA to REG_SET_PAGE,
  // then read REG_LEN (this transitions STATE_INIT -> STATE_BUFFERED ->
  // STATE_LIVE inside the emulated device).
  GoldfishWrite32 (Dev, REG_SET_PAGE, PAGE_ABSDATA);
  GoldfishRead32 (Dev, REG_LEN);

  // Set up the SimpleTextIn protocol
  Dev->SimpleTextIn.Reset         = GoldfishEventsReset;
  Dev->SimpleTextIn.ReadKeyStroke = GoldfishEventsReadKeyStroke;

  // Create WaitForKey event
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_WAIT,
                  TPL_NOTIFY,
                  GoldfishEventsWaitForKey,
                  &Dev->SimpleTextIn,
                  &Dev->SimpleTextIn.WaitForKey
                  );
  if (EFI_ERROR (Status)) {
    goto FreeDev;
  }

  // Create periodic polling timer
  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  KeyboardTimerHandler,
                  Dev,
                  &Dev->TimerEvent
                  );
  if (EFI_ERROR (Status)) {
    goto CloseWaitForKey;
  }

  Status = gBS->SetTimer (
                  Dev->TimerEvent,
                  TimerPeriodic,
                  KEYBOARD_TIMER_INTERVAL
                  );
  if (EFI_ERROR (Status)) {
    goto CloseTimer;
  }

  // Install the SimpleTextIn protocol and device path on a new handle
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Dev->Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mDevicePath,
                  &gEfiSimpleTextInProtocolGuid,
                  &Dev->SimpleTextIn,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    goto CloseTimer;
  }

  DEBUG ((DEBUG_INFO, "%a: Goldfish keyboard driver started\n", __func__));
  return EFI_SUCCESS;

CloseTimer:
  gBS->CloseEvent (Dev->TimerEvent);

CloseWaitForKey:
  gBS->CloseEvent (Dev->SimpleTextIn.WaitForKey);

FreeDev:
  FreePool (Dev);
  return Status;
}
