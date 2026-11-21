# NullCam.dll (Juno SB) camera driver interface: facts sheet

Static analysis of `imgfs/nullcam.o` (.text at 0x024C0000) and `NULLCAM.dll.2` (data at 0x01E26000). Addresses are VAs. "Data+0xNNNN" = offset into NULLCAM.dll.2.
Tags: **[seen]** read directly from code/data; **[inf]** inferred (name or meaning), evidence given.

## 0. Imports and helpers

IAT is at data+0x000 (rva 0xB000). Names come from coredll ordinals (CE3 ordinal table) and are consistent with usage; CE5-only ordinals are named from usage.

| Thunk | Ordinal | Name | Basis |
|---|---|---|---|
| 0x24c8918 | 1043 | memcmp | table + usage (a,b,16) |
| 0x24c8958 | 1047 | memset | table + usage |
| 0x24c89e8 | 1044 | memcpy | table + usage |
| 0x24c8a28 | 517 | SetLastError | table + usage |
| 0x24c89c8 | 1602 | MapCallerPtr **[inf]** | (ptr,len) -> ptr, NULL = fail, used on every caller buffer |
| 0x24c89d8 | 2508 | CeSafeCopyMemory **[inf]** | (dst,src,n) -> BOOL |
| 0x24c8aa8 | 1536 | OpenMsgQueue **[inf]** | (GetCallerProcess(), hMsgQ, &MSGQUEUEOPTIONS{dwSize=20, bReadAccess=0}) at 0x24c3c48-0x24c3c54 |
| 0x24c8b08 | 1531 | WriteMsgQueue **[inf]** | (h, buf, 16, 250, 0) at 0x24c483c |
| 0x24c8ad8 | 1533 | CloseMsgQueue **[inf]** | (pin->hMsgQ) at close 0x24c435c |
| 0x24c8988 | 1494 | ActivateDeviceEx **[inf]** | (L"Software\Microsoft\DirectX\DirectShow\Capture", 0, 0, dev) at 0x24c15d4, handle -> dev+0x14, later DeactivateDevice (1180) |
| 0x24c8a38 | 2008 | __rt_sdiv | remainder (r1) used for step check |
| others | | RegOpenKeyExW 461, RegQueryValueExW 463, RegCloseKey 455, LoadLibraryW 528, GetProcAddressW 530, FreeLibrary 529, LocalAlloc 33, LocalFree 36, RemoteLocalAlloc 37, RemoteLocalFree 40, SetProcPermissions 611, GetCallerProcess 607, CeGetCallerTrust 1395, GetTickCount 535, Sleep 496, FindWindowW 286 | |

Internal helper `GetBuf(dev, ptr, len, minLen, &err)` 0x24c20a8 **[seen]**: ptr NULL or len < minLen -> err=122 (ERROR_INSUFFICIENT_BUFFER), NULL; MapCallerPtr fails -> err=1784; else mapped ptr.

Driver tunables in `HKLM\Drivers\Capture\NullCam` **[seen]**: `PinCount` (DWORD, only value 2 honoured, default 3; 0x24c1b98-0x24c1bb8), `StreamGuid0..4,6` (REG_SZ GUID; overwrites SubFormat of a data range, 0x24c1bbc-0x24c1e58), `MemoryModel` (DWORD 1/2/4, default 2; 0x24c4144), `LibraryName`+`FunctionName` (REG_SZ; replaces the frame-fill function; 0x24c43a0). Also `HKLM\Software\GoldTek\Camera\GoldTekCamera`==1 or a window class `CAMERA_GTK_WM6` existing enables "GoldTek mode" (0x24c7694, 0x24c62e8), which changes two behaviours noted below.

## 1. IOCTLs per entry point

All IOCTL entry points: MapCallerPtr(pIn,lenIn), MapCallerPtr(pOut,lenOut), MapCallerPtr(pdwBytesReturned,4). **lpBytesReturned must be non-NULL**: if its mapping fails the function returns the value 87 (non-zero = TRUE) without SetLastError (CAM 0x24c35a8/0x24c3738, PIN 0x24c5534/0x24c553c). Otherwise handler status 0 -> TRUE; non-zero -> SetLastError(status), FALSE (0x24c3930, 0x24c5734).

| Entry | IOCTL | Code | Input | Output | Handler / effect |
|---|---|---|---|---|---|
| CAM_IOControl 0x24c3528 | IOCTL_CS_PROPERTY | 0x002F0003 | >= 24 (CSPROPERTY) + per-set struct | per Id | Set compared (memcmp 16) against 7 GUIDs, else 50 (0x24c3920) |
| CAM | IOCTL_POWER_CAPABILITIES **[inf name]** | 0x00321000 | - | >= 48 | zero 48 bytes, byte0 DeviceDx = 0x11 (D0,D4), returns 48 (0x24c36bc-0x24c36dc) |
| CAM | IOCTL_POWER_GET | 0x00321004 | - | >= 4 | DWORD Dx from global 0x01E2B210 (0x24c35ec-0x24c3604) |
| CAM | IOCTL_POWER_SET | 0x00321008 | - | >= 4, Dx read **from pOut** | Dx > 4 ignored; Dx==0 -> power-up 0x24c27b8; Dx 1-4 -> 0x24c27dc (empty function); writes Dx back, returns 4 (0x24c3624-0x24c367c) |
| CAM | other | | | | 50 ERROR_NOT_SUPPORTED |
| PIN_IOControl 0x24c54bc | IOCTL_CS_PROPERTY | 0x002F0003 | >= 24 | per Id | Set must be KSPROPSETID_Connection (text 0x9cc) -> 0x24c51cc, else 50 |
| PIN | IOCTL_STREAM_INSTANTIATE **[inf name]** (function 7) | 0x002F001F | >= 40 (CSPROPERTY_STREAMEX_S) | unused | Set must be 443A0706-... (text 0x9dc), Id (+0x10) must be 0 (else 87, 0x24c5658) -> 0x24c3ba4; Flags not checked |
| PIN | IOCTL_CS_BUFFERS **[inf name]** (function 0x1007) | 0x002F401F | >= 4: DWORD command | >= 120: CS_STREAM_DESCRIPTOR | cmd 0 alloc 0x24c4b3c, 1 enqueue 0x24c3f04, 2 dealloc 0x24c488c, else 87 (0x24c4ea0). Short in/out -> *pBytesReturned=120, error 87 (0x24c55bc). BytesReturned not written on success. |
| PIN | other | | | | 50 |

Handler return codes seen: 87, 122, 234 (ERROR_MORE_DATA), 1784, 50, 1062 (enqueue while STOP), 5023 (RUN from STOP), 21 (timer start fail on RUN), 1359 (timer start fail on PAUSE), 14 (alloc fail).

## 2. Property sets

Flags values handled (same numbers as KS) **[seen]**: GET 0x1, SET 0x2, SETSUPPORT 0x100 (returns success immediately, nothing written, in Pin/VideoControl/ProcAmp/CameraControl/Connection), BASICSUPPORT 0x200, DEFAULTVALUES 0x10000.

**Value-buffer convention differs per set [seen]:**
- CAM sets (ProcAmp, CameraControl, VideoControl): GET and SET both take the full `*_S` struct as **input**; GET also writes the full struct to output.
- PIN Connection set: SET value is read **from the output buffer** (KS style: CSPROPERTY in pIn, data in pOut).

### 2.1 KSPROPSETID_Pin 8C134960-51AD-11CF-878A-94F801C10000 (CAM, 0x24c29d0)
Only Flags GET (or 0x100) accepted; others 87 (0x24c2a48). Input is CSP_PIN (32 bytes, PinId at +0x18) and PinId must be < PinCount (dev+0xdc) except for CTYPES.

| Id | CE name **[inf]** | Output | Value |
|---|---|---|---|
| 0 | CINSTANCES | 8: {PossibleCount=1, CurrentCount=dev[0xe0+12*PinId]} | 0x24c2e58 |
| 1 | CTYPES | 4: PinCount (3, or 2 via registry) | 0x24c2e14. Bug: EnterCriticalSection(dev) is not left on the error paths (lenOut<4 or copy fail) |
| 2 | DATARANGES | CSMULTIPLE_ITEM{Size,Count} + N*1336 | 0x24c2d2c. Needed size = N*0x538+8 is written to BytesReturned first; short buffer -> 234. Size field = total bytes. **Count = 1 unless GoldTek mode**, but all N ranges are copied regardless (0x24c2db0-0x24c2dc0) |
| 3 | DATAINTERSECTION | 1192: CS_DATAFORMAT_VIDEOINFOHEADER | 0x24c2c08. Input = CSP_PIN + CSMULTIPLE_ITEM{Size,Count} at +0x20 + items at +0x28 with fixed stride 1336; requires lenIn >= Size*Count+32. Each item matched against pin ranges (see 5.4); on match copies item[0..64) and item[0xd0..0xd0+1128) to output; last match wins |
| 4 | CATEGORY | 16: GUID | 0x24c2b80 |
| 5 | NAME | WCHAR string | 0x24c2b08: "Capture"/"Still"/"Preview" (text 0x24c00b8 + 20*PinId) |
| 6 | DEVICENAME | WCHAR string | 0x24c2a8c: "PIN1:" for every pin (text 0x24c007c + 20*PinId) |

String Ids: BytesReturned = (wcslen+1)*2; short buffer -> 234.

Pins **[seen]** (dev init 0x24c19c8-0x24c1a88, names table):

| PinId | Name | Category | Ranges (in order) |
|---|---|---|---|
| 0 | Capture | FB6C4281-0353-11D1-905F-0000C0CC16BA (PINNAME_VIDEO_CAPTURE) | A, B |
| 1 | Still | FB6C428A-... (PINNAME_VIDEO_STILL) | A, B, C, D, E, F |
| 2 | Preview | FB6C4282-... (PINNAME_VIDEO_PREVIEW) | A, G |

### 2.2 84FF1B03-2D79-4BFC-8AEA-BE85F4B9F464 (CAM, 0x24c20f8)
Ignores Id and Flags (handler is not passed the input). Output >= 4 (else 122): writes DWORD **1**, BytesReturned=4. **[inf]** CE `PROPSETID_VERSION` / driver version (DRIVER_VERSION 1).

### 2.3 PROPSETID_VIDCAP_VIDEOPROCAMP C6E13360-... (CAM, 0x24c2ee8)
Struct CSPROPERTY_VIDEOPROCAMP_S, 40 bytes: CSPROPERTY(24), Value +0x18, Flags +0x1c, Capabilities +0x20, pad. Id <= 9, or <= 13 in GoldTek mode (0x24c2f54-0x24c2f6c), else 87.
- GET: input >= 40, output >= 40; writes Property, Value, Flags, Capabilities; BytesReturned 40 (0x24c3118-0x24c3220).
- SET: input >= 40; Flags (+0x1c) must be 1 (AUTO) or 2 (MANUAL) and a subset of Capabilities; for MANUAL, Value must be in [Min,Max] and (Value-Min) % Step == 0 (0x24c3030-0x24c3088). AUTO stores flags only.
- BASICSUPPORT (0x200, 0x24c24e8): output >= 72 gets CSPROPERTY_DESCRIPTION(40){AccessFlags=0x10003, DescriptionSize=72, PropTypeSet=KSPROPTYPESETID_General 97E99BA0-BDEA-11CF-A5D6-28DB04C10000 Id 3 (VT_I4) Flags 0, MembersListCount=1, Reserved=0} + MEMBERSHEADER{1 (RANGES),16,1,0} + STEPPING_LONG{Step,0,Min,Max}. 40..71 bytes: description only, BytesReturned 40, error 234. 4..39: AccessFlags only, success.
- DEFAULTVALUES (0x10000, 0x24c2628): output >= 60: description (DescriptionSize=60) + MEMBERSHEADER{3 (VALUES),4,1,1 (DEFAULT)} + LONG default.

Per-Id table (records at dev+0x4a0+32*Id, built at 0x24c15f8-0x24c1990; record = {Default?, Current, Flags, Caps, pValues, pStep, GetOK, SetOK}) **[seen]**:

| Id | KS name | Default | Flags | Caps | Range (step 1) | Hardware action on SET MANUAL **[seen]** / meaning **[inf]** |
|---|---|---|---|---|---|---|
| 0 | BRIGHTNESS | 3 | 2 | 3 | 1..5 | 0x24c6dc0 -> sensor "Brightness" (global 0x1E5D41C+8) |
| 1 | CONTRAST | 100 | 2 | 3 | 0..10000 | stored only |
| 2 | HUE | 0 | 1 | 1 | -18000..18000 | stored only |
| 3 | SATURATION | 100 | 1 | 3 | 0..10000 | stored only |
| 4 | SHARPNESS | 1 | 1 | 1 | 1..2 | stored only |
| 5 | GAMMA | 1 | 1 | 1 | 1..4 | stored only |
| 6 | COLORENABLE | 1 | 1 | 3 | 0..1 | stored only |
| 7 | WHITEBALANCE | 1 | 1 | 3 | 1..4 | 0x24c6e08 -> "WhiteBalance" (+4) |
| 8 | BACKLIGHT_COMP | 1 | 1 | 1 | 0..1 | stored only |
| 9 | GAIN | 1 | 1 | 1 | -10..15 | stored only |
| 10 | (GoldTek mode only) | 1 | 2 | 3 | 1..4 | 0x24c6e68 -> "Effect" (+0x14) **[inf name]** |
| 11 | (GoldTek) | 1 | 2 | 3 | 1..2 | 0x24c6e88 -> "Flicker" (+0xc) **[inf name]** |
| 12 | (GoldTek) | 1 | 2 | 3 | 0..1 | 0x24c6f90 -> 0x24c8408: value 1 runs a sensor command/poll sequence on reg 0x3f **[inf: autofocus trigger]** |
| 13 | (GoldTek) | 0 | 2 | 3 | 0..2 | GET calls 0x24c8508: reads reg 0x3f, returns 0/1/2 **[inf: focus status]** |

Global field names come from the registry string list at 0x24c7d70-0x24c7d88 (Zoom, WhiteBalance, Brightness, Flicker, FocusMode, Effect, Exposure in global order +0..+0x18) **[inf]**. Flags: 1=AUTO, 2=MANUAL (KS values).

### 2.4 PROPSETID_VIDCAP_CAMERACONTROL C6E13370-... (CAM, 0x24c3238)
Same struct/size (40) and same GET/SET/BASICSUPPORT/DEFAULTVALUES logic; Id <= 7. Records at dev+0x660+32*Id.

| Id | KS name | Default | Flags | Caps | Range | Action |
|---|---|---|---|---|---|---|
| 0 | PAN | 0 | 1 | 3 | -180..180 | stored only |
| 1 | TILT | 0 | 1 | 3 | -180..180 | stored only |
| 2 | ROLL | 0 | 1 | 3 | -180..180 | stored only |
| 3 | ZOOM | 1 | 1 | 3 | 1..4 | 0x24c6de0(320,240,v) -> "Zoom" |
| 4 | EXPOSURE | 1 | 1 | 3 | 1..2 | 0x24c6e28 -> "Exposure" |
| 5 | IRIS | 2500 | 1 | 3 | 0..3456 | stored only |
| 6 | FOCUS | 1 | 2 | 2 | 1..2 | 0x24c6e48 -> "FocusMode" |
| 7 | FLASH **[inf, CE]** | 0 | 2 | 2 | 0..200 | stored only |

### 2.5 PROPSETID_VIDCAP_VIDEOCONTROL 6A2E0670-28E4-11D0-A18C-00A0C9118956 (CAM, 0x24c2150)
Input copied (24 bytes); Id 0 and 3 only.
- Id 0 CAPS, struct 32: CSPROPERTY + StreamIndex +0x18 + Caps +0x1c. StreamIndex < PinCount. GET -> Caps = dev[0xd0+4*idx]; SET -> Caps (no bits outside 0xF) stored; DEFAULTVALUES -> table data+0x8f4. Default caps **[seen]**: pin0 = 0x4, pin1 = 0xC, pin2 = 0x3 (KS bits: 1 FlipH, 2 FlipV, 4 ExternalTriggerEnable, 8 Trigger).
- Id 3 MODE, struct 32: CSPROPERTY + StreamIndex +0x18 + Mode +0x1c. **Only SET with Mode == 8 (Trigger) and StreamIndex 0 or 1** and that pin instantiated -> sets pin state to RUN (0x24c3c88(pin,2,NULL), 0x24c225c). This is the still-capture trigger (see 5).

### 2.6 PROPSETID_VIDCAP_VIDEOCOMPRESSION C6E13343-... (text 0x5b0)
Handler 0x24c2148 returns 87 for everything. (The brief's labels for 0x5b0 and 0x590 are swapped.)

### 2.7 PROPSETID_VIDCAP_DROPPEDFRAMES C6E13344-... (text 0x590, CAM, 0x24c23d8)
Copies lenIn bytes of input into a 72-byte stack local (no bound check). Id must be 0 (CURRENT) and Flags 1, output >= 48 (else 234); BytesReturned 48. Output CSPROPERTY_DROPPEDFRAMES_CURRENT_S: CSPROPERTY(24), PictureNumber (LONGLONG, +0x18) = capture pin's frame counter (pin+0x10), DropCount (LONGLONG, +0x20) = pin+0xc, AverageFrameSize (+0x28) = pin+0x8 (only set in driver-allocated buffer mode). Always reports **pin 0** (dev+0xe8); if pin 0 not instantiated returns 87.

### 2.8 KSPROPSETID_Connection 1D58C920-AC9B-11CF-A5D6-28DB04C10000 (PIN, 0x24c51cc)

| Id | CE name **[inf]** | Flags | Data (in pOut) |
|---|---|---|---|
| 0 | STATE | GET: lenOut >= 4, writes state, BytesReturned 4. SET: lenOut >= 4, new state read from pOut | CSSTATE: 0 STOP, 1 PAUSE, 2 RUN **[seen values; inf names]** |
| 1 | DATAFORMAT | SET only (GET -> 87) | CS_DATAFORMAT_VIDEOINFOHEADER. Driver copies biSize+1152 bytes (biSize read from pOut+0x70), so pass the full 1192 even though only lenOut >= 4 is checked (0x24c538c) |
| 2 | ALLOCATORFRAMING | GET or BASICSUPPORT: lenOut >= 24, writes 24, BytesReturned 24. SET: lenOut >= 24, state must be STOP, RequirementsFlags must equal memory model; sets Frames | CSALLOCATOR_FRAMING (see 4) |

STATE SET transitions (0x24c4f24):
- same as current: no-op success.
- -> STOP: resets PictureNumber/DropCount; if not already STOP: stops sensor stream, timeKillEvent, state=0, then **returns every queued buffer through the message queue unfilled** (0x24c4558).
- -> PAUSE: from STOP allocates the buffer-slot array (Frames entries, 136 bytes each, LocalAlloc LPTR) and resets slot count; starts the per-pin MMTimer (period = AvgTimePerFrame/10000 ms, resolution 10, TIME_PERIODIC; 0x24c4d30); state=1. From RUN: stops sensor stream, state=1.
- -> RUN: from STOP -> 5023. Still pin (PinId 1): returns success but does **not** change state. Capture/Preview: configures sensor (format, mode 1=capture/3=preview), Sleep(300), state=2, restarts timestamp base.

### 2.9 443A0706-9554-41F4-BB70-ECB5D210FB3B (PIN, IOCTL 0x2F001F; 0x24c3ba4)
**[inf]** CE `CSPROPSETID_StreamEx`, Id 0 = `CSPROPERTY_STREAMEX_INIT`. Input CSPROPERTY_STREAMEX_S (40): CSP_PIN (PinId +0x18, reserved +0x1c) + HANDLE hMsgQueue +0x20 + pad. Fails 87 if: this handle already instantiated (pin+0 != -1), PinId >= PinCount, hMsgQueue NULL, pin already has 1 instance (max 1 per PinId, 0x24c1ee0), or OpenMsgQueue fails. pin+0 is set to PinId before the later checks, so after a failure close and reopen PIN1: before retrying.

## 3. Unknown GUIDs
- 84FF1B03-...: version set, returns 1 (2.2).
- 443A0706-...: stream init set used only with IOCTL 0x2F001F, Id 0 (2.9).
- 0x002F401F command DWORD: 0 allocate, 1 enqueue, 2 deallocate **[seen]** (CE names CS_ALLOCATE/CS_ENQUEUE/CS_DEALLOCATE **[inf]**).

## 4. Struct layouts

CSPROPERTY (24): +0 GUID Set, +0x10 Id, +0x14 Flags (8-aligned; used as 24 in every copy).

CSP_PIN (32): CSPROPERTY, +0x18 PinId, +0x1c Reserved (unread).

CSPROPERTY_STREAMEX_S (40): CSP_PIN, +0x20 HANDLE hMsgQueue (client's queue handle), +0x24 pad.

CSPROPERTY_VIDEOPROCAMP_S / CAMERACONTROL_S (40): CSPROPERTY, +0x18 Value, +0x1c Flags, +0x20 Capabilities, +0x24 pad.

CSPROPERTY_VIDEOCONTROL_CAPS_S / _MODE_S (32): CSPROPERTY, +0x18 StreamIndex, +0x1c Caps/Mode.

CSPROPERTY_DROPPEDFRAMES_CURRENT_S (48): see 2.7.

CSPROPERTY_DESCRIPTION (40): +0 AccessFlags, +4 DescriptionSize, +8 PropTypeSet (24: GUID, Id, Flags), +0x20 MembersListCount, +0x24 Reserved. MEMBERSHEADER (16): MembersFlags, MembersSize, MembersCount, Flags.

CSALLOCATOR_FRAMING (24) (0x24c5310-0x24c5350): +0 RequirementsFlags = memory model (1 driver, 2 client-limited, 4 client-unlimited **[inf names]**; default 2), +4 PoolType = 1, +8 Frames = max buffers (default 2), +0xc FrameSize = biSizeImage, +0x10 FileAlignment 0, +0x14 Reserved 0.

CSMULTIPLE_ITEM (8): Size, Count.

CS_DATARANGE_VIDEO (1336 = 0x538) **[seen in data and code offsets]**:

| Off | Size | Field | Value in all ranges |
|---|---|---|---|
| 0x00 | 4 | FormatSize | 1336 |
| 0x04 | 4 | Flags | 0 |
| 0x08 | 4 | SampleSize | = biSizeImage |
| 0x0c | 4 | Reserved | 0 |
| 0x10 | 16 | MajorFormat | MEDIATYPE_Video 73646976-0000-0010-8000-00AA00389B71 |
| 0x20 | 16 | SubFormat | YV12 32315659-... or RGB565 E436EB7B-524F-11CE-9F53-0020AF0BA770 |
| 0x30 | 16 | Specifier | FORMAT_VideoInfo 05589F80-C356-11CE-BF01-00AA0055595A |
| 0x40 | 4 | bFixedSizeSamples | 1 |
| 0x44 | 4 | bTemporalCompression | 1 |
| 0x48 | 4 | StreamDescriptionFlags | 2 |
| 0x4c | 4 | MemoryAllocationFlags | 0 |
| 0x50 | 128 | ConfigCaps (CS_VIDEO_STREAM_CONFIG_CAPS) | +0 guid = FORMAT_VideoInfo, +0x10 VideoStandard 0, +0x14 InputSize, +0x1c MinCroppingSize, +0x24 MaxCroppingSize, +0x2c CropGranularityX/Y, CropAlignX/Y = 1, +0x3c MinOutputSize, +0x44 MaxOutputSize, +0x4c OutputGranularityX/Y (= width/height), +0x54 taps 0, **+0x68 MinFrameInterval (8)**, **+0x70 MaxFrameInterval (8)**, +0x78 MinBitsPerSecond, +0x7c MaxBitsPerSecond |
| 0xd0 | 1128 | VideoInfoHeader (CS_VIDEOINFOHEADER) | see below |

CS_VIDEOINFOHEADER (1128 = 0x468): +0 rcSource (16), +0x10 rcTarget (16), +0x20 dwBitRate, +0x24 dwBitErrorRate, **+0x28 AvgTimePerFrame (8)**, **+0x30 BITMAPINFOHEADER (40)**, +0x58 union (bit masks / palette; RGB565 ranges have F800,07E0,001F here), padded to 0x468. **[inf]** union = dwBitMasks[3] + RGBQUAD[256] (1036 bytes) + 4 pad.

CS_DATAFORMAT_VIDEOINFOHEADER (1192 = 0x4a8): +0 CSDATAFORMAT (64: FormatSize, Flags, SampleSize, Reserved, MajorFormat, SubFormat, Specifier), +0x40 CS_VIDEOINFOHEADER. So AvgTimePerFrame at +0x68, BITMAPINFOHEADER at +0x70.

CS_STREAM_DESCRIPTOR (120 = 0x78): CSSTREAM_HEADER (56) + CS_FRAME_INFO (64). Evidence: pFrameInfo NULL check `cmn r5,#56` at 0x24c3cb8; driver-alloc init 0x24c4a50-0x24c4ab0; fill 0x24c3cd0-0x24c3ddc.

| Off | Size | Field | Driver behaviour |
|---|---|---|---|
| 0x00 | 4 | Size | 56 (driver-alloc init) |
| 0x04 | 4 | TypeSpecificFlags | 0 |
| 0x08 | 8 | PresentationTime.Time | (GetTickCount()-start)*10000 (100 ns units) on fill |
| 0x10 | 4 | PresentationTime.Numerator | 1 |
| 0x14 | 4 | PresentationTime.Denominator | 1 |
| 0x18 | 8 | Duration | AvgTimePerFrame of current format |
| 0x20 | 4 | FrameExtent | buffer size (driver-alloc init) |
| 0x24 | 4 | DataUsed | fill function return (biSizeImage) |
| 0x28 | 4 | Data | client buffer pointer; must stay identical across alloc/enqueue/dealloc |
| 0x2c | 4 | OptionsFlags | on fill: 4 (DATADISCONTINUITY) on first frame after start or after a drop, then &= ~0x110, \|= 1 |
| 0x30 | 4 | buffer handle **[inf name]** | written by driver at allocate: ((Data & 0xFFFF) + (slot<<16)) ^ GetCallerProcess(); must be passed back unchanged |
| 0x34 | 4 | unused/pad | |
| 0x38 | 4 | FrameInfo.ExtendedHeaderSize | 64 |
| 0x3c | 4 | dwFrameFlags | 0 |
| 0x40 | 8 | PictureNumber | per-pin delivered-frame counter |
| 0x48 | 8 | DropCount | per-pin drop counter (no buffer queued at tick) |
| 0x50 | 40 | hDirectDraw, hSurfaceHandle, DirectDrawRect, Reserved1-4 **[inf, KS_FRAME_INFO]** | not written |

CS_MSGQUEUE_BUFFER (16), message written to the client's queue (0x24c3dd8-0x24c3de0): +0 Size = 12, +4 Flags = 1 (frame buffer **[inf]**), +8 Context = 0, +0xc pStreamDescriptor = the descriptor pointer the driver received at enqueue (the MapCallerPtr'd form of the client's pointer **[inf]**; match on low 25 bits or on Data/handle).

## 5. Streaming protocol

Devices: `CAM1:` (CAM_*) and `PIN1:` (PIN_*). CAM_Init requires CeGetCallerTrust()==2 (0x24c3a34). CAM_Init calls ActivateDeviceEx on `Software\Microsoft\DirectX\DirectShow\Capture` with the CAM context, which loads the PIN side (PIN_Init stores it, 0x24c5464) **[inf]**. **CAM1: is single-open**: a second CAM_Open fails with 1247 ERROR_ALREADY_INITIALIZED until CAM_Close (0x24c1b04, 0x24c39c8). PIN1: may be opened many times; each open is one stream object (0x5b0 bytes, 0x24c57a4) and the stream is chosen by PinId at instantiate. All pins report DEVICENAME "PIN1:".

1. Open `CAM1:`. Query Pin CTYPES, NAME, CATEGORY, DATARANGES (ignore Count unless GoldTek mode; derive N from Size: (Size-8)/1336).
2. Client `CreateMsgQueue` (read side, cbMaxMessage >= 16). Open `PIN1:`.
3. IOCTL 0x2F001F with CSPROPERTY_STREAMEX_S{Set=443A0706..., Id=0, PinId, hMsgQueue}. The driver opens the queue for writing and loads the pin's first range as the current format (0x24c3bf4-0x24c3c10).
4. Optional: Connection DATAFORMAT SET (CS_DATAFORMAT_VIDEOINFOHEADER in pOut; any state). Match rule (0x24c28f8): Major/Sub/Specifier GUIDs equal, AvgTimePerFrame within the range's [MinFrameInterval, MaxFrameInterval], and the 40-byte BITMAPINFOHEADER byte-identical to the range's. Copy the BITMAPINFOHEADER verbatim from DATARANGES.
5. Optional, while STOP and before the first PAUSE: ALLOCATORFRAMING GET (memory model, FrameSize) and SET Frames (default 2). The slot array is sized at the first STOP->PAUSE and is not resized later.
6. Connection STATE SET 1 (PAUSE). Required before allocating or enqueuing (slot array is created here; enqueue in STOP -> 1062).
7. IOCTL 0x2F401F cmd 0 (allocate) per buffer, up to Frames, pOut = a CS_STREAM_DESCRIPTOR the client keeps at a fixed address:
   - model 2 (default) and 4: client sets Data (buffer of at least stride*|biHeight| bytes, stride = ((biWidth*biBitCount+31)/8)&~3) and the driver copies the 120 bytes and writes back the handle at +0x30.
   - model 1: driver RemoteLocalAlloc's the buffer and returns a filled descriptor. Size uses signed biHeight (0x24c4a40), so YV12 ranges (negative height) give a negative size: avoid model 1 for YV12.
8. IOCTL 0x2F401F cmd 1 (enqueue) each descriptor. Driver validates handle and Data, maps Data for stride*|biHeight| bytes, and stores the descriptor pointer (0x24c3f04).
9. Capture/Preview: Connection STATE SET 2 (RUN). Each MMTimer tick (0x24c4628) takes the first queued slot, writes the frame into Data, updates the header/frame info **in the client's descriptor** from the timer thread, then WriteMsgQueue(16-byte CS_MSGQUEUE_BUFFER, 250 ms timeout). No queued buffer -> DropCount++ and discontinuity flag. Client: ReadMsgQueue, process, re-enqueue (cmd 1). There is no blocking dequeue IOCTL; the message queue is the only completion path.
10. Still (PinId 1): instantiate, set format (C-F for 800x600..2048x1536), PAUSE, allocate, enqueue, then on `CAM1:` VIDEOCONTROL MODE SET {StreamIndex=1, Mode=8}. Connection STATE RUN on the still pin is accepted but does nothing (0x24c502c). On the next tick the driver reconfigures the sensor, sleeps 800 ms, pauses Capture/Preview (saving their state), fills the buffer (fill routine called twice, PictureNumber +2), posts the message, sets the still pin back to PAUSE and restores the other pins (0x24c4670-0x24c4854). Enqueue the buffer before triggering: with no buffer the pin stays RUN and repeats the 800 ms reconfiguration every tick.
11. Teardown: STATE SET 0 (STOP) returns queued buffers through the queue (not refilled; Time=0, DataUsed stale). cmd 2 (deallocate) each non-queued descriptor (queued -> 87). CloseHandle PIN1: (closes the queue, frees slots, releases the PinId instance), then CAM1:.

Frame data **[seen, 0x24c5884]**: default fill copies biSizeImage bytes from the sensor capture buffers for the pin's mode (1 capture, 2 still (waits up to 1000 ms for a new frame), 3 preview) with no format conversion; sensor output format is chosen from biCompression (UYVY 1, YV12 2, RGB24 3/4, else RGB565; 0x24c4210). Any format with width 160 or |height| 120 is captured at 320x240 and passed through a rotate/crop routine (0x24c5810, not analysed). DataUsed = biSizeImage.

## 6. Formats (data ranges)

All: bFixedSizeSamples 1, bTemporalCompression 1, StreamDescriptionFlags 2, rcSource/rcTarget 0 except F, biXPels/biYPels/biClrUsed 0.

| Range | Data off | SubFormat | W x H (biHeight) | bpp | biCompression | biPlanes | SampleSize | AvgTimePerFrame | Frame interval min..max (100 ns) | dwBitRate | Pins |
|---|---|---|---|---|---|---|---|---|---|---|---|
| A | 0x25b8 | RGB565 | 160 x 120 | 16 | 0x8003 | 1 | 38400 | 333333 (30 fps) | 333000..1333333 | 9216000 | Capture, Still, Preview (first range of each = default format) |
| B | 0x2af0 | YV12 | 320 x -240 | 12 | 'YV12' | 1 | 115200 | 666666 (15 fps) | 666000..2666666 | 13824000 | Capture, Still |
| C | 0x1b48 | YV12 | 800 x -600 | 12 | 'YV12' | 1 | 720000 | 666666 | 666000..2666666 | 86400000 | Still |
| D | 0x10d8 | YV12 | 1280 x -960 | 12 | 'YV12' | 1 | 1843200 | 666666 | 666000..2666666 | 221184000 | Still |
| E | 0x1610 | YV12 | 1600 x -1200 | 12 | 'YV12' | 1 | 2880000 | 666666 | 666000..2666666 | 345600000 | Still |
| F | 0x2080 | YV12 | 2048 x -1536 | 12 | 'YV12' | **3** | 4718592 | 666666 | 666000..2666666 | 566231040 | Still; rcSource=rcTarget={0,0,2048,-1536} |
| G | 0x3028 | RGB565 | 320 x 240 | 16 | 3 (BI_BITFIELDS) | 1 | 153600 | 333333 | 333000..1333333 | 36864000 | Preview |

RGB565 masks at VIH+0x58: F800/07E0/001F. 0x8003 = BI_BITFIELDS | 0x8000 (**[inf]** CE BI_SRCPREROTATE). ConfigCaps sizes equal the image size; Min/MaxBitsPerSecond match the 4x/1x frame-interval bounds.

Registry SubFormat overrides map: StreamGuid0 -> A, 1 -> G, 2 -> B, 3 -> C, 4 -> D, 6 -> F (no StreamGuid5 for E) (0x24c1e80-0x24c1eac).

## 7. Open questions for a device probe

1. Actual `MemoryModel` and `PinCount` values in the device registry (ALLOCATORFRAMING GET answers the first).
2. Whether the pointer in CS_MSGQUEUE_BUFFER.pStreamDescriptor equals the client's own pointer or its slot-mapped alias.
3. Whether Capture and Preview can run at the same time (both reconfigure the one sensor on RUN).
4. Real content of the 160x120 output (rotate/crop path) and whether YV12 frames are top-down as biHeight<0 claims.
5. Whether the Juno's camera app (GoldTek) holds `CAM1:` open in the background, which would make our CAM_Open fail with 1247.
6. Meaning of ProcAmp Ids 10-13 and CameraControl FLASH on this sensor; Id 12/13 look like autofocus trigger/status.
7. Whether IOCTL_POWER_SET D4 does anything (its handler is empty here; the power manager may still cut power elsewhere).
