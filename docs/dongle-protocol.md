# Carlinkit CPC200-CCPA ("Autokit" / "Auto Box") USB host protocol reference

Implementation-grade reference for driving a Carlinkit CPC200-CCPA (model string `A15W`) and related
Carlinkit "Auto Box" dongles from a head unit over USB bulk. Compiled 2026-11-05 purely from public
source code and public reverse engineering documentation, before this project had an adapter.
Nothing here was verified against real hardware when it was written.

**This is the reference `packages/carplay_native/src/dongle/` was written from**, and a dated
record rather than live documentation: when the hardware disagrees with it, the code and the
comments next to it win, and the finding belongs in `AGENTS.md` or a doc comment. No code was
ported from any of the projects below. The plugin is a clean implementation of the protocol facts
(ids, offsets, sequences), which are not copyrightable. The licence column in section 0 matters
only if that ever changes; this project is GPL-3.0-or-later, so every source listed is compatible,
LIVI with its attribution term.

Conventions:

* All integers are **little endian**. `u32` = unsigned 32 bit, `i32` = signed 32 bit, `f32` = IEEE 754 float.
* "Host" = head unit (your Flutter app). "Dongle" / "adapter" / "box" = the Carlinkit device.
* `OUT` = host to dongle, `IN` = dongle to host.
* **[UNCERTAIN]** marks facts that are unverified, inferred, or where sources disagree.
* Citations use the short source keys from section 0, then `path:line` inside that repo.

---

## 0. Sources and licenses

| Key | Repository | Commit / tag read | Language | License | May I port code? |
|-----|------------|-------------------|----------|---------|------------------|
| NCP | github.com/rhysmorgan134/node-CarPlay | `670f19eda2a0b0047a5a538b9602b263f442433a` (2025-06-08) | TypeScript | MIT | Yes, keep MIT notice |
| PYC | github.com/electric-monk/pycarplay | `8d9a31370546515991260b4056c1650e9e097755` (2020-12-05) | Python | MIT | Yes, keep MIT notice |
| LIVI8 | github.com/f-io/LIVI tag `v8.3.0` | `d95dd361ed28154f7fff0627c0be10af91abebff` (2026-08-21) | TypeScript (Electron) | GPL-3.0-or-later **plus a section 7(b) attribution term** ("Based on LIVI by Lasse Heitgres" must appear in About screen and README) | Only if your plugin is GPL-3.0 compatible and you add the attribution. Otherwise use as documentation only. |
| LIVI | github.com/f-io/LIVI `main` | `7fa9f8020d6422c66cdae92c3ab019a06e91b3a2` (2026-11-05) | Rust | same as LIVI8 | LIVI dropped the stock dongle protocol after v8.3.0 (v9+ is a native CarPlay stack plus a reflashed dongle "LIVI Link"). Not useful for the stock protocol. |
| CCPA | github.com/lvalen91/CPC200-CCPA_resources | `a5831314037a221f0110359a53d87d6b7356bc95` (2026-11-02) | Markdown docs from firmware RE + USB captures, firmware 2025.10.15.1127 | MIT | Docs: yes (facts are not copyrightable anyway) |
| CLK | github.com/lvalen91/carlink | `4aaa4913c22fef91284e9bfee0c590fa8e615ad5` (2026-10-14) | Kotlin (Android / AAOS host for CCPA) | Unlicense (public domain) | Yes, freely |
| CLF | github.com/lvalen91/carlink_flutter | `d3e8876b65b8bce9c6264928870551156a37d14c` (2026-02-28) | Dart / Flutter (older CCPA host) | MIT | Yes, keep MIT notice |
| CLR | github.com/lvalen91/carlink_linux | `fbbfa59400dac4704f34a5d76e745080ce7d6338` (2026-06-23) | Rust (Linux CCPA host) | Unlicense | Yes, freely |
| FCP | github.com/niellun/FastCarPlay | `f483456318344710d2890fc7ee0d67608892dea2` (2026-03-31) | C++ (Raspberry Pi) | GPL-3.0 | Only if GPL compatible |
| CXX | github.com/jwinarske/carlinkit-cxx | `28282e20042c7610a254793c545285ec9392d8bc` (2026-07-12) | C++ | Apache-2.0 | Yes, keep NOTICE / license |
| RCP | github.com/OneMakerShow/round-carplay | `7eaa4017d6cdab91ad6368ba7ec9c533331663be` (2025-09-03) | TypeScript (node-CarPlay derivative) | MIT | Yes |
| LVR | github.com/ludwig-v/wireless-carplay-dongle-reverse-engineering | `b437135316275a5d3c375bdd1ceac2a6bcf1315b` | firmware RE notes | GPL-3.0 | Docs only |
| V2K | github.com/Veyron2K/Carlinkit-CPC200-Autokit-Reverse-Engineering | `21cd52ae8c021a212fed34d46ed367cdac015a45` | firmware RE notes | GPL-3.0 | Docs only |
| MAH | github.com/mahnat/ccpa-200-re | `cd5b6606e1534e43bfa79ce6da62da18767da487` | RE notes | no license file (all rights reserved by default) | Docs only, do not copy |

Path conventions: CLK paths are relative to `app/src/main/kotlin/com/carlink/` unless they start with `app/` or
`documents/` (Java video code `video/H264Renderer.java` is under `app/src/main/java/com/carlink/`); CLR paths are relative to `crates/carlink/src/` unless they start with
`crates/`; CCPA paths are relative to `documentation/` when they start with a section folder like `02_Protocol_Reference/`.

Also seen but not read in depth: harrylepotter/carplay-receiver (pycarplay derivative, `protocol.py`),
rhysmorgan134/react-carplay (UI around NCP), f-io/pi-carplay (old name of LIVI).

For a permissively licensed host the split would be: port structure and constants from NCP, PYC,
CLK, CLF, CLR, CXX freely; use LIVI8, FCP, LVR, V2K only as protocol documentation.

---

## 1. USB identification and transport

### 1.1 IDs

| VID:PID | What | Source |
|---------|------|--------|
| `0x1314:0x1520` | Carlinkit Auto Box, head unit facing gadget. Matched by every host implementation. Measured live on a CCPA test unit. | NCP `src/modules/DongleDriver.ts:87-90`; PYC `link.py:13-15` (only 0x1520); LIVI8 `src/main/services/usb/constants.ts:3-4`; CCPA `documentation/01_Firmware_Architecture/hardware_platform.md:230` |
| `0x1314:0x1521` | Same device, the default `USBPID` in the CCPA firmware config (`riddle.conf` key `USBPID`). | NCP `DongleDriver.ts:89`; LIVI8 `constants.ts:4`; CCPA `hardware_platform.md:147,241` |

* The PID is a firmware config value (`riddle.conf` `USBVID` / `USBPID`, stored as bare hex strings), so
  0x1520 vs 0x1521 is per unit / per build. **Match both** (CCPA `hardware_platform.md:241`). LIVI's
  udev rule matches `ATTR{idVendor}=="1314", ATTR{idProduct}=="152*"` (LIVI8
  `assets/linux/99-LIVI.rules.template:4`).
* Strings: iManufacturer "Magic Communication Tec.", iProduct "Auto Box" (CCPA `hardware_platform.md:232-233`).

* Related IDs that are **not** the head unit interface: `0x08E4:0x01C0` is what the dongle presents to the
  **phone** on its female port (`iap2,ncm`); the CCPA host app guide still scans for it as an alternate
  (CCPA `hardware_platform.md:119-134`, `04_Implementation/host_app_guide.md:105-108`). `0x1314:0x2D00`
  is the third party "OCBM" firmware (CCPA `06_Reference/vehicle_platforms/gm_usb_host_admission.md:4-5`).
* Model variants sharing this protocol: U2W_AUTOKIT, A15W (CPC200-CCPA), U2AC_AUTOKIT ("2air"), all
  NXP i.MX6UL, 128 MB RAM, 16 MB NOR (CCPA `01_Firmware_Architecture/device_variants_and_conversion.md:22-26`).
  A15W hardware revisions: Rev A `hwVersion` `YMA0-WR2C-0003` (RTL8822CS Wi-Fi), Rev B `YMA0-WN16-0003`
  (NXP IW416), both `boxType` "YA", `productType` "A15W" (CCPA `hardware_revisions.md:22-31`).

Other IDs found in the dongle firmware scripts (from the LVR dump of A15W firmware 2025.02.25.1521,
paths under `Extracted/A15W_2025.02.25.1521/`) and in issue trackers:

| VID:PID | Meaning | Source |
|---------|---------|--------|
| 1314:1521 | stock default `USBPID` on A15W (`riddle_default.conf`) | LVR `etc/riddle_default.conf:2-3`; lsusb in github.com/rhysmorgan134/react-carplay/issues/76, issues/26, github.com/rhysmorgan134/node-CarPlay/issues/77 |
| 1314:1520 | hardcoded fallback in `start_accessory.sh` when riddle.conf has no USBPID; used by CPC200-Autokit (`Auto_Box`), CPC200-CCPM (wired only), and some CCPA units (one on 2025.10.15.1127) | LVR `script/start_accessory.sh:64-73`; github.com/f-io/LIVI/issues/271, issues/98 |
| 12d1:0001 | MTP/ADB gadget mode (only if riddle.conf has no USB ids); also LIVI Link custom firmware | LVR `script/start_mtp.sh:43-51` |
| 1d6b:a4a5 | "UDisk" mass storage pass through mode | LVR `script/start_mass_storage.sh:80-84` |
| 18d1:2d00 | dongle posing as an Android phone in AOA mode (U2AW/U2AC converters) | LVR `script/start_aoa.sh:22-24` |
| 05ac:12a8 | dongle posing as an iPhone: U2W / U2AW / U2AC wired-to-wireless converters (these do **not** speak this protocol) | github.com/rhysmorgan134/node-CarPlay/issues/88 |

* A real bug was caused by listing only 0x1520 (github.com/rhysmorgan134/react-carplay/issues/76).
* bcdDevice 4.09 seen on units (github.com/f-io/LIVI/issues/326); iSerial `0123456789FEDCBA` placeholder on
  many units unless `USBSerial=UUID` (LVR `script/start_accessory.sh:74-78`). Do not use the serial as an id;
  use `uuid` from the BoxSettings IN JSON.
* bDeviceClass seen as 0x00 on stock units in the wild (github.com/f-io/LIVI/issues/307) and 0xEF on the
  CCPA test unit. If `UdiskMode=1` the gadget becomes `accessory,mass_storage` with an 8 MB FAT "APK"
  volume, so search for the interface with class 0xFF and two bulk endpoints instead of blindly using
  interface 0 (LVR `script/start_accessory.sh:27-32`) [inference by the research agent].
* Which models speak this protocol: CPC200-CCPA (`A15W`), CPC200-Autokit (`Auto_Box`), CPC200-CCPM /
  Auto-CA (wired only). Not: U2W, U2AW, U2AC (2Air), CP2A (converters for cars with factory CarPlay),
  CPC200-TBOX (Android box), Mini Ultra (other SoCs) (LVR `README.md:22-25`;
  github.com/rhysmorgan134/react-carplay/issues/73).

### 1.2 Descriptors (CCPA, measured)

From CCPA `documentation/01_Firmware_Architecture/hardware_platform.md:228-239` (live, 2026-07-08):

| Field | Value |
|-------|-------|
| bDeviceClass | 0xEF (Miscellaneous / IAD) |
| Configuration | high speed config #1, "android_accessory" |
| Interface 0 | bInterfaceClass 0xFF (vendor), bInterfaceSubClass 0xF0, bInterfaceProtocol 0, 2 endpoints |
| Bulk IN | `0x83`, wMaxPacketSize 512 (**conflict:** `0x81` per CXX `docs/ARCHITECTURE.md:10,14` and lvalen91/ocbm `host/gm_ccpa/docs/00_HANDOFF.md:424-425` "hardware verified") |
| Bulk OUT | `0x02`, wMaxPacketSize 512 (**conflict:** `0x01` per the same sources) |
| Speed | USB 2.0 high speed (also on units with a Type-C socket) |

The 0x83/0x02 unit was a stripped test build with NCM composed in; plausibly a pure `accessory` gadget gives
0x81/0x01 [UNCERTAIN, inference]. Either way: discover endpoints at runtime.

* The dongle side is the Linux legacy `f_accessory` gadget (`/dev/usb_accessory`). **No Android Open
  Accessory control handshake** (requests 51/52/53) is needed: just claim interface 0 and do bulk
  transfers (CCPA `usb_protocol.md:36`, `hardware_platform.md:237`).
* No kernel driver binds to interface 0 on Linux/macOS, so libusb can claim it (still call
  `libusb_set_auto_detach_kernel_driver` defensively).
* Do **not** hardcode endpoint addresses. Every implementation picks "first IN" and "first OUT"
  endpoint of interface 0 (NCP `DongleDriver.ts:118-133`; LIVI8 `driver/dongle/dongleDriver.ts:344-372`;
  PYC `link.py:23-31`). Other units or older firmware may use 0x81/0x01.
* Measured raw throughput: IN 339 Mbit/s, OUT 90 Mbit/s (CCPA `hardware_platform.md:243-248`), far
  above CarPlay needs (8 to 30 Mbit/s).

### 1.3 Open / claim / reset sequence used by implementations

| Impl | Sequence | Source |
|------|----------|--------|
| NCP (node) | find device (retry every 3 s) -> `open()` -> `reset()` -> `close()` -> wait **3000 ms** ("Device disappears after reset for 1-3 seconds") -> find again -> `open()` -> `selectConfiguration(1)` -> `claimInterface(interfaces[0])` -> start | NCP `src/node/CarplayNode.ts:94-150`, `DongleDriver.ts:96-143` |
| NCP (WebUSB) | `open()` -> `reset()` -> initialise (no re-find) | NCP `src/web/CarplayWeb.ts:120-131` |
| PYC | `usb.core.find` -> `reset()` -> `set_configuration()` -> interface (0,0) -> `clear_halt()` on IN and OUT | PYC `link.py:17-31` |
| LIVI8 | no reset on normal bring-up: `open()` -> `selectConfiguration(1)` -> `claimInterface(0)` -> pick endpoints -> start read loop. A reset (`open` -> `reset` -> `close`) is only done on a user-triggered "force reset"; a reset that makes the device drop off the bus counts as success. | LIVI8 `dongleDriver.ts:321-372`; `src/main/services/usb/USBService.ts:415-511` |

Notes:

* NCP comment: resetting through node-usb "can cause transfer issues and for it to disappear", after
  reset subsequent writes fail with `LIBUSB_ERROR_NO_DEVICE` until the device is re-found
  (NCP `DongleDriver.ts:92-94`, `CarplayNode.ts:116-128`). A USB reset makes the dongle **re-enumerate**
  (new device handle). Plan for: reset, close handle, wait for hotplug arrival (or poll 1 to 3 s), reopen.
* Dongle side commands that also cause re-enumeration: `0xCD` reboot (re-appears after about 10 to 15 s),
  `0xCE` USB gadget reset (CCPA `usb_protocol.md:1956-1992`).

More implementations (paths relative to each repo):

| Impl | Sequence | Source |
|------|----------|--------|
| FCP | poll `libusb_open_device_with_vid_pid` every 200 ms; on every link attempt `libusb_reset_device` then immediately (same handle, no wait) `set_configuration(1)`, `claim_interface(0)`; last IN and last OUT endpoint of intf 0 alt 0 | FCP `src/protocol/connection.cpp:193-234,446-467` |
| CXX | `libusb_open_device_with_vid_pid` + `libusb_set_auto_detach_kernel_driver(h,1)`; **no reset on a normal connect** ("the dongle re-enumerates on reset (new address and node), which breaks the open handle and drops udev permissions"); `set_configuration(1)` tolerating `LIBUSB_ERROR_BUSY`; first bulk IN / bulk OUT endpoints; reset only after 3 failed claims, `LIBUSB_ERROR_NOT_FOUND` after reset = success; backoff 250 ms doubling to 5 s; RX transfers submitted **before** the first send so no early dongle response is missed | CXX `src/usb_device.cpp:22-103`, `src/dongle_manager.cpp:50-153`, `src/dongle.cpp:94-95` |
| RCP | no reset on start (manual "force reset" only); `selectConfiguration(1)`, interface 0 | RCP `src/main/carplay/DongleDriver.ts:92-101`, `src/main/usb/USBService.ts:108-115,240-289` |
| CLK (Kotlin, CCPA specific) | `UsbManager.requestPermission`; claim the **first** interface that has a bulk IN and a bulk OUT, `claimInterface(force=true)`; **no USB reset**: instead, right after open it writes DisconnectPhone (0x0F) and CloseDongle (0x15), then sleeps 200 ms, because "adapter firmware retains session state across USB reconnects ... stays in PLUGGED/STREAMING state and ignores our OPEN command" | CLK `app/src/main/kotlin/com/carlink/usb/UsbDeviceWrapper.kt:116-190,716-757`; `CarlinkManager.kt:1051-1068`; `documents/revisions.txt:339` |
| CLF (Dart, older) | `setConfiguration(config 0)`, `interfaces.first`, claim force; no reset at start ("matches Autokit behavior"); a reset exists via reflection on hidden `UsbDeviceConnection.resetDevice`, after which the device is re-found by VID/PID because the device path changes | CLF `lib/driver/usb/usb_device_wrapper.dart:110-131`; `lib/carlink.dart:259`; `android/src/main/kotlin/com/carlink/UsbDeviceManager.kt:310-378` |
| CLR (Rust, Linux) | match VID 0x1314 and `(pid & 0xFFF0) == 0x1520` ("dongle switches across several product IDs (0x1520-0x152f) during its mode-switch / handshake / re-enumeration"); auto detach; `set_active_configuration(1)` only if not already 1 ("re-setting can trigger a reset"); scans **all interfaces and alt settings** for a bulk pair and selects the alt setting; reset off by default (`CARLINK_RESET=1`: reset, drop handle, sleep 3 s, rescan every 250 ms up to 5 s). Note: CLR says its unit had the bulk endpoints on interface 1 alt 1, but that unit was in NCM mode and CLR's README says Open was not yet tested in normal mode | CLR `crates/carlink/src/transport/acquire.rs:24-182`; `README.md:173-176` |
| CCPA host guide | USB reset, **wait 3 s ("mandatory")**, re-detect, open, start heartbeat timer, init messages 120 ms apart, start read loop | CCPA `04_Implementation/host_app_guide.md:120-147`; `01_Firmware_Architecture/initialization.md:22-31` |

CXX's docs list endpoints 0x81 / 0x01 for its unit (CXX `docs/ARCHITECTURE.md:10,14`) while CCPA measured
0x83 / 0x02: confirms that endpoint addresses vary, look them up from the descriptor.

CXX also notes the dongle can expose a USB mass storage partition that the desktop auto mounts at
`/run/media/$USER/APK` (CXX `README.md:205-206`): that is a separate interface/config state; unmount it, do
not let it hold the device.

**Recommendation for the plugin (Linux, libusb):** open by VID 0x1314 and PID 0x1520 or 0x1521; enable
auto detach; optional reset only on the very first open after app start or after a failure, then close
and wait for the device to re-appear (hotplug or poll up to about 5 s); `set_configuration(1)` (ignore BUSY);
claim the interface that has class 0xFF and a bulk IN + bulk OUT pair (normally interface 0, alt 0; select the alt setting if the pair is on another alt, as CLR does); `clear_halt` both (PYC); submit IN transfers; start heartbeat; send 0x0F + 0x15, wait 200 ms (CLK); send init.

### 1.4 Reading and writing

| Aspect | What implementations do | Source |
|--------|------------------------|--------|
| Read | Two bulk IN transfers per message: first exactly 16 bytes (header), then exactly `length` bytes (payload) if `length > 0`. | NCP `DongleDriver.ts:167-209`; LIVI8 `dongleDriver.ts:478-519`; PYC `link.py:63-85` |
| Read timeout | LIVI8 uses 1000 ms per transfer and simply re-issues on timeout (bulk is lossless). PYC treats errno 110 (timeout) as benign. | LIVI8 `dongleDriver.ts:59,838`; PYC `link.py:66-70` |
| Error budget | Both NCP and LIVI8 close the device and report "failure" after 5 consecutive header/parse errors (`MAX_ERROR_COUNT = 5`). LIVI8 resets the counter after each good message. | NCP `DongleDriver.ts:17,170-174`; LIVI8 `dongleDriver.ts:58,814-854` |
| Write | NCP and LIVI8: header + payload concatenated, one `transferOut`. PYC: header and payload as two separate writes under a lock. Both work. | NCP `sendable.ts:18-29`; LIVI8 `protocol/sendables.ts:43-61`; PYC `link.py:37-45` |
| Concurrency | Serialize writes (one writer at a time, a message must not interleave with another). PYC uses a lock. | PYC `link.py:37-45` |

#### The ZLP bug (important for the read strategy)

CCPA `documentation/01_Firmware_Architecture/usb_zlp_bug.md` (status in doc: "Confirmed (AA + CarPlay),
Fix Identified, Untested"):

* When the dongle writes a bulk IN transfer whose size is an exact multiple of 512 it does **not**
  terminate it with a zero length packet (`g_android_accessory.ko` parameter `accZLP` exists since
  2023.09.27 but is always 0) (lines 11, 38-72).
* The dongle kernel writes in 16384 byte chunks. Only transfers **larger than 16 KB** whose total
  size is 512 aligned are fatal (lines 92-99, 179-199). Typical trigger: album art
  (e.g. 143868 + 4 = 143872 = 281 x 512) (lines 23-27, 87-93).
* Symptom: one bulk IN read never completes while the host waits for a short packet. On AA a silent
  stall for 60 to 70 s; on CarPlay a reconnect loop about every 15 s (lines 15-19, 102-119).
* Confirmed on firmware 2025.10.15.1127CAY and 2025.12.29.1746; no firmware enables the fix (lines 6, 72, 94).
* **Host rule:** never read "until short packet". Read the 16 byte header, then request exactly
  `length` bytes for the payload (libusb completes a transfer when the requested length is reached,
  without needing a ZLP). Do not issue one big fixed size read (for example 512 KB) that relies on the
  device ending the transfer short. If you use a larger read buffer, request sizes must be multiples
  of 512 (wMaxPacketSize) and you must reassemble messages by header length across transfers
  (lines 136-141).

#### lvalen91 hosts (CCPA specific)

| Impl | Reads | Writes |
|------|-------|--------|
| CLK | 16 byte header read, then the payload in requests of `min(remaining, 16384)`; read timeout 30000 ms, "no data within 15 s of loop start = IN endpoint dead"; max payload 2 MiB; on a bad header just `continue` (no resync; comment: "CPC200 aligns one-message-per-bulk-transaction in practice") | header+payload in one `bulkTransfer`, 1000 ms timeout; not serialized (comment admits `bulkTransfer` is not documented thread safe) |
| CLF | header 16 B then payload in 16384 B chunks; max 1 MiB, larger frames drained | one buffer, 1000 ms, nested retries |
| CLR | 64 KiB reads into a reassembler ("a single bulk transfer may return a partial message, several messages, or a message split across transfers"), 1 byte slide resync, 2 MiB cap; 1000 ms poll; `clear_halt` on errors, 5 consecutive errors = disconnected | mutex serialized `write_bulk`, on stall `clear_halt(out)` and retry once |

Sources: CLK `usb/UsbDeviceWrapper.kt:27-57,408-563,620-653`, `protocol/AdapterDriver.kt:24-49`; CLF
`android/src/main/kotlin/com/carlink/BulkTransferManager.kt:66-250`; CLR `crates/carlink-protocol/src/message.rs:230-314`,
`crates/carlink/src/source/ccpa.rs:40-178,420-440`, `crates/carlink/src/transport/usb.rs:31-68`.

Observation: CLK and CLF work in the field with exact 16 byte header reads, which means the dongle sends
each header as its own short USB transfer (16 < 512 bytes ends the transfer). With libusb, a read buffer
smaller than an arriving packet gives `LIBUSB_ERROR_OVERFLOW`, so if you do exact length reads, always
request lengths that will end on a packet boundary the dongle itself produces (header 16, then payload
length); a stream reader with 512 multiple buffers avoids the issue entirely.

#### Stream style readers (C++ implementations)

| Impl | Reads | Reassembly / resync | Writes |
|------|-------|---------------------|--------|
| FCP | 32 async libusb transfers of 2048 B each, infinite timeout, ring of 128 slots, event thread at realtime priority with 1 ms `libusb_handle_events_timeout_completed` | byte stream reader across slots; on a bad header discards the rest of the slot. Accepts payload length 0..2 MiB and magic 0x55AA55AA or 0x55BB55BB | two sync transfers (header, then payload), 3000 ms timeout; single writer thread; queue of 128 drops new messages when full; consecutive Touch Move messages coalesced. Heartbeat only when nothing was written for 3 s |
| CXX | ring of 8 async transfers of 131072 B (128 KiB), DMA buffers from `libusb_dev_mem_alloc`, infinite timeout | `StreamParser`: zero copy when one message fits in a chunk, else accumulate; on a bad header scans forward 1 byte at a time; max payload 16 MiB | one async transfer header+payload, 1000 ms timeout, mutex serialized |

Sources: FCP `src/settings.h:48-50`, `src/protocol/connection.cpp:254-440`, `src/protocol/usb_buffer.cpp:97-141`,
`src/protocol/message.h:14,291-293`; CXX `src/dongle.h:55-56`, `src/usb_device.cpp:123-129`,
`src/stream_parser.h:19-122`, `src/dongle.cpp:33-108`.

Both are fine since the requested sizes (2048, 131072) are multiples of 512, so a ZLP-less 512 aligned
message just completes the transfer when the buffer is full and the next transfer continues the stream.
**Recommended design for the plugin:** a native (C/C++ via FFI, or Kotlin on Android) reader thread that
keeps 4 to 8 bulk IN transfers of 16 to 128 KiB in flight and parses a byte stream (CXX style), with a 1 MiB
(CCPA limit) or 16 MiB sanity cap on `length` and a forward scan for the magic on desync. This keeps a read
always pending (the dongle resets after 4.5 s if it cannot write) and is immune to the ZLP bug. Keep all
writes on one serialized queue; header + payload in one transfer is fine.

On Android (later): `UsbDeviceConnection.bulkTransfer` historically limits a single transfer to 16384 bytes
on older Android versions (before API 28), and `UsbRequest` queueing is the async path (general Android
knowledge, not from the repos). CLK and CLF read in 16384 byte chunks but send large SendFiles (icons) in a
single transfer, i.e. they rely on newer Android. CLK permission flow: `PendingIntent` for action
`com.carlink.USB_PERMISSION`, `FLAG_MUTABLE` on API 31+, receiver `RECEIVER_NOT_EXPORTED`, 30 s timeout; manifest
`USB_DEVICE_ATTACHED` filter with vendor 4884, products 5408, 5409 (and 2276/448) (CLK
`usb/UsbDeviceWrapper.kt:26,116-190`, `app/src/main/res/xml/usb_device_filter.xml:3-7`). CLK runs the reader
thread at `THREAD_PRIORITY_URGENT_DISPLAY - 2`.

---

## 2. Message framing

Every message = 16 byte header + optional payload (CCPA `usb_protocol.md:10-34`; NCP `common.ts:98-195`; PYC `protocol.py:17-66`).

| Offset | Size | Field | Value |
|--------|------|-------|-------|
| 0 | u32 | magic | `0x55AA55AA` (bytes on wire `AA 55 AA 55`). `0x55BB55BB` = AES encrypted payload, only after the host enabled encryption with type `0xF0` (see 3.12). Never happens unless you send `0xF0`. |
| 4 | u32 | length | payload length in bytes, header excluded |
| 8 | u32 | type | message type (section 3) |
| 12 | u32 | typeCheck | `type ^ 0xFFFFFFFF` (bitwise NOT of type) |
| 16 | n | payload | type specific |

Validation (CCPA `usb_protocol.md:30-34`): magic must match, typeCheck must equal `~type`, `length <= 1048576`
(1 MiB). On a bad header NCP/LIVI8 count an error and read the next 16 bytes (no resync scan).

Examples:

```
Heartbeat:  AA 55 AA 55  00 00 00 00  AA 00 00 00  55 FF FF FF
Frame(0x0C):AA 55 AA 55  00 00 00 00  0C 00 00 00  F3 FF FF FF
```

(CCPA `usb_protocol.md:146,1227-1232`)

Strings in payloads are ASCII/UTF-8, sometimes NUL terminated: trim trailing `\0` before parsing
(LIVI8 `protocol/decode.ts:171-270` does this everywhere).

Resync: after a host restart the dongle may still be mid stream (video and audio keep flowing, no new
Plugged), and the first bytes read can be in the middle of a message ("Invalid buffer size - Expecting
16") (github.com/rhysmorgan134/node-CarPlay/issues/25). A robust reader scans forward byte by byte for
`AA 55 AA 55` followed by a valid typeCheck (CXX `src/stream_parser.h:20-23,61`). After a resync, request a
keyframe (CarPlay) because the decoder has no SPS yet.

Max sizes seen: payload length cap 1 MiB in firmware validation (CCPA); video frames up to about 165 KB;
album art up to about 180 KB; debug log replies up to 16 MB (only if you send 0x88).

---

## 3. Message types

### 3.1 Master table

Dir: OUT = host to dongle, IN = dongle to host. "Impl" = which open source host implements it.

| Type | Hex | Name(s) | Dir | Payload | Impl | Notes / source |
|------|-----|---------|-----|---------|------|----------------|
| 1 | 0x01 | Open | OUT and IN | 28 B | all | OUT: session params (3.2). IN: echo of the same 28 B struct = "dongle ready / opened" (LIVI8 treats an IN Open with or without payload as `DongleReady`, `decode.ts:214-223,254-256`). |
| 2 | 0x02 | Plugged (PlugIn) | IN | 4 or 8 B | all | phone connected (3.4) |
| 3 | 0x03 | Phase | IN | 4 B | NCP, LIVI8 | u32 phase (section 4.3) |
| 4 | 0x04 | Unplugged (PlugOut) | IN | 0 | all | phone gone, tear down |
| 5 | 0x05 | Touch | OUT | 16 B | all | single touch (section 7) |
| 6 | 0x06 | VideoData | IN | 20 B + H.264 | all | main screen video (section 5) |
| 7 | 0x07 | AudioData | IN and OUT | 12 B + 1/4/n | all | PCM, audio command, volume ramp; OUT = microphone (section 6) |
| 8 | 0x08 | Command (CarPlayControl) | IN and OUT | 4 B | all | u32 command id (3.3) |
| 9 | 0x09 | LogoType | OUT | 4 B | NCP, LIVI8 | u32: 1 = HomeButton, 2 = Siri (NCP `sendable.ts:291-310`). Rarely used. |
| 10 | 0x0A | BluetoothAddress | IN | 17 B ASCII | all | dongle BT MAC "XX:XX:XX:XX:XX:XX" |
| 11 | 0x0B | CarPlayModeChange | IN | var | none | 28 B StModeChange struct (3.10). LIVI8 names it `CarplayControl` and ignores it. |
| 12 | 0x0C | BluetoothPIN (IN) / Frame (OUT) | both | IN: ASCII PIN; OUT: 0 B | NCP, LIVI8 (IN) | OUT with no payload = request IDR keyframe, same as Command 12 (CCPA `usb_protocol.md:134-160`) |
| 13 | 0x0D | BluetoothDeviceName | IN | ASCII | all | dongle BT name |
| 14 | 0x0E | WifiDeviceName | IN | ASCII | all | dongle Wi-Fi SSID |
| 15 | 0x0F | DisconnectPhone | OUT | 0 | NCP, LIVI8 | drop phone session, dongle stays open, phone may reconnect (NCP `sendable.ts:344-348`) |
| 16 | 0x10 | AirPlayModeChange / DashboardData | IN | var | none | iPhone AirPlay mode change; log only |
| 17 | 0x11 | AutoConnect_By_BluetoothAddress (LIVI8: WifiStatusData) | OUT (also IN) | ASCII MAC | LIVI8 | OUT: ask dongle to connect to this paired phone (LIVI8 `sendables.ts:494-496`, used instead of Command 1002 when a target phone is known) |
| 18 | 0x12 | BluetoothPairedList | IN and OUT | UTF-8 text | all | list of paired phones (MAC and name concatenated, often without separator, e.g. `DE:AD:BE:EF:00:06User`; split with a MAC regex, CLK `protocol/MessageParser.kt:476-519`; CCPA `usb_protocol.md:1315`) |
| 19 | 0x13 | BluetoothOnlineList (LIVI8: DiskInfo) | IN | var | none | BT online device list |
| 20 | 0x14 | ManufacturerInfo | IN | 8 B (2 x u32) | NCP, PYC, LIVI8 | meaning unknown [UNCERTAIN] |
| 21 | 0x15 | CloseDongle (StopPhoneConnection) | OUT | 0 | NCP, LIVI8 | full stop; "need to send open command again" (NCP `sendable.ts:339-343`) |
| 22 | 0x16 | CameraFrame (LIVI8: CameraInfo) | both | var | none | reverse camera feature, not needed |
| 23 | 0x17 | MultiTouch | OUT | n x 16 B | all | section 7 |
| 24 | 0x18 | HiCarLink (ConnectionURL) | IN | ASCII | NCP, LIVI8 | HiCar QR URL, ignore for CarPlay |
| 25 | 0x19 | BoxSettings (BoxInfo) | OUT and IN | JSON | all | OUT: config JSON (3.6). IN: box info JSON and phone info JSON (3.7) |
| 26 | 0x1A | PayResult | ? | ? | none | unused |
| 30 | 0x1E | BroadCastRemoteCxCy | IN | 28 B | none | phone screen size broadcast (Cx, Cy u32 at 0, 4); do not send 0x1E |
| 34 | 0x22 | ForgetBluetoothAddr | OUT | ASCII MAC | LIVI8 | unpair a phone (LIVI8 `sendables.ts:498-500`) |
| 35 | 0x23 | Bluetooth_ConnectStart (LIVI8: PeerBluetoothAddress) | IN | 17 B MAC | LIVI8 | phone BT connecting |
| 36 | 0x24 | Bluetooth_Connected (LIVI8: PeerBluetoothAddressAlt) | IN | 17 B MAC | LIVI8 | phone BT connected |
| 37 | 0x25 | Bluetooth_DisConnect (LIVI8: UiHidePeerInfo) | IN | 0 | LIVI8 | log only, never tear down USB session |
| 38 | 0x26 | Bluetooth_Listen (LIVI8: UiBringToForeground) | IN | 0 | LIVI8 | log only |
| 40 | 0x28 | iAP2PlistBinary | IN | var | none | log only |
| 41 | 0x29 | GnssData | OUT | NMEA text | LIVI8 | GPS to phone (3.11) |
| 42 | 0x2A | MediaData / MetaData (DashBoard_DATA) | IN | 4 B subtype + data | NCP, LIVI8 | now playing, album art, navigation (3.9) |
| 43 | 0x2B | Connection_PINCODE (LIVI8: ShowPinCode) | IN | ASCII | none | BT pairing PIN to display |
| 44 | 0x2C | NaviVideoData (AltVideoFrame, LIVI8: ClusterVideoData) | IN | 20 B + H.264 | LIVI8 | second video stream for instrument cluster (5.6) |
| 110 | 0x6E | NaviFocusRequest (LIVI8: ClusterFocusRequest) | IN (LIVI8 sends OUT) | 0 | LIVI8 | [UNCERTAIN] direction: CCPA says IN, LIVI8 sends it OUT (`sendables.ts:504-505`) |
| 111 | 0x6F | NaviFocusRelease (LIVI8: ClusterFocusRelease) | as above | 0 | LIVI8 | |
| 119 | 0x77 | FactorySetting | IN: 0 B; OUT: 4 B | none | IN = idle notification, log only. OUT = **factory reset**, never send by accident (CCPA `usb_protocol.md:1650-1680`) |
| 136 | 0x88 | DebugTest | OUT | 4 B | none | 1 open log, 2 read log (reply arrives as type 0x99), 3 periodic |
| 153 | 0x99 | SendFile (UploadFile) | OUT (IN only as debug log reply) | var | all | write a file on the dongle (3.5) |
| 160 | 0xA0 | AppInfo | OUT | JSON | none | only the vendor AutoKit app sends it; firmware 2025.10 ignores it |
| 161 | 0xA1 | ExtendedManufacturerInfo / ICCOA info | IN | 12 to 24 B | none | ICCOA only |
| 162 | 0xA2 | BoxConfig | OUT | JSON | FCP | AutoKit and FCP send `{"DayNightMode": n}` (AutoKit also `WiFiChannel`); CCPA: not in the 2025.10 dispatch table, but `configuration.md` says the same parser as BoxSettings serves it [UNCERTAIN] |
| 163 | 0xA3 | SessionToken / VendorSessionInfo | IN | ASCII base64 | LIVI8 (decrypts for logging) | AES-128-CBC encrypted telemetry JSON, key ASCII `W2EC1X1NbZ58TXtn`, IV = first 16 decoded bytes (CCPA `usb_protocol.md:428-527`; LIVI8 `driver/dongle/vendorSessionInfo.ts:3-25`). Ignore or log. |
| 170 | 0xAA | HeartBeat | OUT | 0 | all | keepalive every 2 s (3.8) |
| 177 | 0xB1 | UpdateProgress | IN | i32 | LIVI8 | firmware update progress |
| 187 | 0xBB | UpdateState (CMD_UPDATE) | IN | i32 | LIVI8 | 1 start, 2 ok, 3 fail, 5 OTA start, 6 OTA ok, 7 OTA fail (LIVI8 `messages/dongleEvents.ts:287-342`) |
| 204 | 0xCC | SoftwareVersion | IN | 32 B ASCII, NUL padded | all | e.g. `2025.02.25.1521CAY`; LIVI8 keeps only `YYYY.MM.DD.HHMM` (`decode.ts:194-201`) |
| 205 | 0xCD | Reboot | OUT | 0 | none | reboots dongle |
| 206 | 0xCE | ResetUSB | OUT | 0 | none | dongle drops and re-enumerates USB |
| 240 | 0xF0 | EnableCrypt | OUT 4 B / IN 0 B | none | do not send (3.12) |
| 253 | 0xFD | D_Ready (display ready) | OUT | 0 | none | optional fire and forget after your decoder is ready; sets a display ready flag in the firmware, "may gate video" [UNCERTAIN, no open source host sends it] (CCPA `usb_protocol.md:1994-2006`) |
| 255 | 0xFF | CMD_ACK | IN | 0 | none | Open acknowledgment on some paths [UNCERTAIN] |
| 4096 | 0x1000 | DuckAudio | none | n/a | LIVI8 internal only | LIVI8 lists it in its enum but only uses it as an internal event (`wire.ts:51`, `dongleDriver.ts:522-545`); not a wire message |

Sources: type table NCP `common.ts:71-96`; LIVI8 `driver/dongle/protocol/wire.ts:3-52`; CCPA
`usb_protocol.md:56-212,1620-1648,2357-2420` (firmware dispatch table of `ARMadb-driver` 2025.10.15).
Name conflicts between LIVI8 and CCPA are shown as "A (LIVI8: B)"; CCPA's names come from strings in
the firmware binary and are more trustworthy.

### 3.2 Open (0x01, OUT, 28 bytes)

| Off | Type | Field | Values used | Notes |
|-----|------|-------|-------------|-------|
| 0 | u32 | width | e.g. 800, 1280, 2400 | requested video width in px |
| 4 | u32 | height | e.g. 480/640, 720, 960 | |
| 8 | u32 | fps | NCP 20, PYC 60, LIVI8 60 | |
| 12 | u32 | format | 5 everywhere | CCPA: 1 = "basic" (few IDR), 5 = "full H.264 mode", responds to frame requests, frequent IDR (`usb_protocol.md:591-600`) |
| 16 | u32 | packetMax | 49152 everywhere | meaning unclear, always 49152 [UNCERTAIN] |
| 20 | u32 | iBoxVersion | 2 everywhere | |
| 24 | u32 | phoneWorkMode | 2 = CarPlay, 4 = Android (Auto) | LIVI8 `PhoneWorkMode { CarPlay = 2, Android = 4 }` (`src/main/shared/types/Config.ts:12-15`). CCPA: this field drives only the iPhone stack; Android mode comes from `/etc/android_work_mode` (`usb_protocol.md:2133-2145`). |

Sources: NCP `sendable.ts:227-262`, `DongleDriver.ts:52-76`; PYC `protocol.py:102-120`; LIVI8
`protocol/sendables.ts:239-268`; CCPA `usb_protocol.md:577-604`.

The dongle answers with an IN Open echoing the 28 byte struct (CCPA capture `usb_protocol.md:1320`).
CCPA: the Open handler rewrites `HU_VIEWAREA_INFO` to full screen when width/height changed
(`usb_protocol.md:602-604`), so send view/safe area files **after** Open.

### 3.3 Command (0x08) ids

Payload: one u32 id. Same message type in both directions. Header bytes:
`AA 55 AA 55 04 00 00 00 08 00 00 00 F7 FF FF FF <id u32>`. A few ids carry a longer payload
(packet longer than 20 bytes in total): parse by length (CCPA `command_ids.md:34-46,398`).

Direction codes: H>D = host to dongle (handled by dongle), H>D>P = forwarded to the phone,
P>D>H = phone event forwarded to the host, D>H = dongle status.

Primary source: firmware command name table of `ARMadb-driver` 2025.10.15.1127 (CCPA
`documentation/02_Protocol_Reference/command_ids.md:199-361`, details `command_details.md`).
NCP name = `src/modules/messages/common.ts:22-66`. LIVI8 name = `src/main/shared/types/ProjectionEnums.ts:1-99`.

| Id | Firmware name (CCPA) | Dir | NCP name | LIVI8 name | Meaning / host action |
|----|----------------------|-----|----------|------------|-----------------------|
| 0 | (invalid) | | invalid | invalid | |
| 1 | StartRecordMic | D>H (also listed H>D>P) | startRecordAudio | startRecordAudio | IN: start microphone capture and upload (section 6.5) |
| 2 | StopRecordMic | D>H (also H>D>P) | stopRecordAudio | stopRecordAudio | IN: stop microphone |
| 3 | RequestHostUI | P>D>H | requestHostUI | requestHostUI | user tapped the car / "back to car" icon in CarPlay: show native UI, session stays alive. Payload 4 or 68 bytes |
| 4 | DisableBluetooth / PhoneBtMacNotify | H>D (4 B); D>H (24 B) | | disableBluetooth | D>H variant: `[0..3]` = 4, `[4..20]` ASCII phone MAC, 3 unknown bytes. Parse by length |
| 5 | SiriButtonDown | H>D>P | siri | voiceAssistant | start Siri / voice assistant (steering wheel button press) |
| 6 | SiriButtonUp | H>D>P | | voiceAssistantRelease | button release; send after 5 |
| 7 | UseCarMic | H>D (dongle echoes it at init) | mic | mic | host captures mic and uploads it (required on A15W, it has no onboard mic) |
| 8 | UseBoxMic | H>D | | boxMic | dongle's own mic |
| 12 | RequestKeyFrame (Frame) | H>D | frame | frame | request IDR (section 5.3). Equivalent to type 0x0C with empty payload |
| 14 | Hide | P>D>H | | hideUI | phone asks host to hide the projection (CarPlay went to background); not a disconnect |
| 15 | UseBoxI2SMic | H>D | **boxMic** (wrong name) | boxMici2s | dongle I2S mic. NCP's "boxMic = 15" is a misnomer |
| 16 | StartNightMode | H>D | enableNightMode | enableNightMode | dark CarPlay UI, works mid session (verified 2026-06) |
| 17 | StopNightMode | H>D | disableNightMode | disableNightMode | light UI |
| 18 | StartGNSSReport | both: D>H when the phone wants location (CCPA `usb_protocol.md:2327-2328`); CLK also sends it H>D on Plugged when GPS forwarding is on | | startGnssReport | start sending GnssData 0x29 |
| 19 | StopGNSSReport | as 18 | | stopGnssReport | |
| 21 | UsePhoneMic | H>D | | phoneMic | phone uses its own mic |
| 22 | UseBluetoothAudio | H>D | audioTransferOn | audioTransferOn | phone audio goes over Bluetooth to the car (not over USB) |
| 23 | UseBoxTransAudio | H>D | audioTransferOff | audioTransferOff | **default you want**: audio is transferred to the host over USB |
| 24 | Use24GWiFi | H>D | wifi24g | wifi24g | `touch /etc/wifi_use_24G`, **takes effect after dongle reboot** |
| 25 | Use5GWiFi | H>D | wifi5g | wifi5g | `rm -f /etc/wifi_use_24G` (5 GHz is default), after reboot |
| 26 | RefreshFrame | H>D | | refreshFrame | force a frame refresh |
| 28 / 29 | Start / StopStandbyMode | both | | enable/disableStandbyMode | power management, not a disconnect |
| 30 / 31 | Start / StopBleAdv | H>D | | start/stopBleAdvertising | BLE advertising on/off |
| 100 | CtrlButtonLeft | H>D>P | left | left | D-pad. Send press, then 105 (release) |
| 101 | CtrlButtonRight | H>D>P | right | right | |
| 102 | CtrlButtonUp | H>D>P | | up | |
| 103 | CtrlButtonDown | H>D>P | | down | |
| 104 | CtrlButtonEnter | H>D>P | selectDown | selectDown | select pressed |
| 105 | CtrlButtonRelease | H>D>P | selectUp | selectUp | select released |
| 106 | CtrlButtonBack | H>D>P | back | back | |
| 111 | CtrlKnobLeft (counter clockwise) | H>D>P | | knobLeft | |
| 112 | CtrlKnobRight (clockwise) | H>D>P | | knobRight | |
| 113 | CtrlKnobUp | H>D>P | **up** | knobUp | NCP maps "up" to 113 |
| 114 | CtrlKnobDown | H>D>P | **down** | knobDown | NCP maps "down" to 114; PYC `BtnDown = 114` |
| 200 | MusicACHome | H>D>P | home | home | CarPlay home |
| 201 | MusicPlay | H>D>P | play | play | |
| 202 | MusicPause | H>D>P | pause | pause | |
| 203 | MusicPlayOrPause | H>D>P | playOrPause | playPause | |
| 204 | MusicNext | H>D>P | next | next | |
| 205 | MusicPrev | H>D>P | prev | prev | |
| 300 | PhoneAnswer | H>D>P | acceptPhone | acceptPhone | |
| 301 | PhoneHungUp | H>D>P | rejectPhone | rejectPhone | |
| 302..311 | PhoneKey0..9 | H>D>P | | phoneKey0..9 | DTMF |
| 312 / 313 | PhoneKeyStar / PhoneKeyPound | H>D>P | | phoneKeyStar / Hash | |
| 314 | CarPlay_PhoneHookSwitch | H>D>P | | phoneKeyHookSwitch | |
| 400..403 | LaunchAppMaps / Phone / Music / NowPlaying | H>D>P | | | CarPlay only |
| 410..412 | ShowUI / StopUI / SuggestUI | H>D>P | | | CarPlay only |
| 500 | RequestVideoFocus | D>H | requestVideoFocus | requestVideoFocus | dongle wants the host to show video (arrives just before Phase 8 and on AA focus changes) |
| 501 | ReleaseVideoFocus | D>H | releaseVideoFocus | releaseVideoFocus | e.g. AA "exit to car" |
| 502 / 503 | RequestAudioFocus / ...Transient | D>H | | | |
| 504 | RequestAudioFocusDuck | D>H | | requestAudioFocusDuck | duck other audio |
| 505 | ReleaseAudioFocus | D>H | | releaseAudioFocus | |
| 506 / 507 | RequestNaviFocus / ReleaseNaviFocus | D>H | | requestClusterFocus / releaseClusterFocus | navigation audio focus (prompt starting / ended) |
| 508 | RequestNaviScreenFocus | both | | requestClusterStreamFocus | handshake that starts cluster video 0x2C (5.6) |
| 509 | ReleaseNaviScreenFocus | H>D | | releaseClusterStreamFocus | stops 0x2C |
| 600 / 601 | DVRCommand_* | H>D | | (LIVI8 reuses 600/601 as internal UI events) | dead code |
| 700 | CusCommand_UpdateAudioVolume | H>D>P | | | forwards HU volume |
| 701 / 702 | CusCommand_HFPCallStart / Stop | H>D>P | | | |
| 1000 | SupportWifi | H>D, echoed D>H | wifiEnable | wifiEnable | host sends at init; dongle also emits after Open |
| 1001 | SupportAutoConnect | D>H (also H>D) | autoConnetEnable | autoConnetEnable | |
| 1002 | StartAutoConnect | H>D | wifiConnect | wifiConnect | start (re)connecting to the last phone |
| 1003 | ScaningDevices | D>H | scanningDevice | scanningDevice | status |
| 1004 | DeviceFound | D>H | deviceFound | deviceFound | status |
| 1005 | DeviceNotFound | D>H | deviceNotFound | deviceNotFound | status |
| 1006 | DeviceConnectFailed | D>H | connectDeviceFailed | connectDeviceFailed | status, NOT session failure |
| 1007 | DeviceBluetoothConnected | D>H | btConnected | btConnected | status |
| 1008 | DeviceBluetoothNotConnected | D>H | btDisconnected | btDisconnected | can fire during active USB CarPlay, ignore |
| 1009 | DeviceWifiConnected | D>H | wifiConnected | wifiConnected | status |
| 1010 | DeviceWifiNotConnected | D>H | wifiDisconnected | wifiDisconnected | **never treat as disconnect** (sent at init and about every 12 s while idle) |
| 1011 | DeviceBluetoothPairStart | D>H | btPairStart | btPairStart | |
| 1012 | WiFiPair / SupportWifiNeedKo | H>D | wifiPair | wifiPair | NCP/LIVI8 send it 15 s after start if nothing plugged (start pairing mode); PYC calls it `SupportWifiNeedKo` |
| 1013 | GetBluetoothOnlineList | H>D | | getBtOnlineList | dongle answers with type 0x13 |

Rules:

* Ids 1000 to 1013 received from the dongle are **pure status**, never end or reset the session on them
  (CCPA `usb_protocol.md:2278-2303`, `command_details.md:574-584`). Session end = Unplugged (0x04), Phase 0,
  or your own heartbeat/read failure.
* Siri and call start/stop arrive as **AudioData commands** (6.3), not as Command ids.
* Ids not present in the firmware switch (ignored if sent): 9..11, 13, 20, 27, 107..110, 115..199,
  206..299, 315..399, 404..409, 413..499, 510..599, 602..699, 703..999, >= 1014 (CCPA `command_details.md:664-683`).
* Conflicts resolved: NCP `boxMic = 15` is really I2S mic, use 8 for the box mic; NCP `up/down = 113/114`
  are knob up/down, D-pad up/down are 102/103. Both work for CarPlay focus navigation in practice
  [UNCERTAIN which feels right in CarPlay; LIVI8 uses 102/103 for D-pad].

### 3.4 Plugged (0x02, IN)

| Off | Type | Field |
|-----|------|-------|
| 0 | u32 | phoneType |
| 4 | u32 | wifi (optional; present when payload length is 8) |

phoneType values (NCP `readable.ts:110-116`; LIVI8 `shared/types/Config.ts:17-23`; CCPA `usb_protocol.md:563-575`):

| Value | Meaning |
|-------|---------|
| 1 | AndroidMirror |
| 2 | CarLife (CCPA only, unverified) |
| 3 | CarPlay |
| 4 | iPhoneMirror |
| 5 | AndroidAuto |
| 6 | HiCar |
| 7 | ICCOA (CCPA only, unverified) |
| 8 | CarPlay wireless, **older firmware only**; 2025.10 sends 3 with wifi = 1 (CCPA `device_identification.md:60,95`) |

AutoKit (vendor app) also knows -2 (unknown/reset), -1 (Android, link type not known yet), 0 (invalid)
(CCPA `device_identification.md:46-60`).

`wifi` field: 0 = wired USB, 1 = wireless (CCPA `device_identification.md:35-42`). It is the transport
discriminator, not phoneType. [UNCERTAIN] one wired capture reported 1 (CCPA `usb_protocol.md:561`).
Parse defensively: if length == 4 there is no wifi field (NCP `readable.ts:118-139`, PYC `protocol.py:226-246`).

Host reaction (NCP `CarplayNode.ts:46-62`; LIVI8 `dongleDriver.ts:742-763`): cancel the "pair" timer, mark
device connected, wait for Phase 8 / first VideoData before showing video. For phoneType 5 (Android Auto)
make sure phoneWorkMode/android work mode match (3.7, 4.4).

### 3.5 SendFile (0x99, OUT)

Payload (NCP `sendable.ts:154-183`; PYC `protocol.py:83-100`; CCPA `usb_protocol.md:848-857`):

| Off | Type | Field |
|-----|------|-------|
| 0 | u32 | nameLen = strlen(path) + 1 (includes the NUL) |
| 4 | nameLen bytes | path, ASCII, NUL terminated |
| 4 + nameLen | u32 | contentLen |
| 8 + nameLen | contentLen bytes | content |

Content helpers used by implementations: "number" = 4 byte u32 LE; "boolean" = u32 0/1; "string" = raw
ASCII without NUL, max 16 chars (NCP truncation warning `sendable.ts:213-221`, LIVI8 strips non
printable and cuts to 16 `sendables.ts:225-233`).

Example (`/tmp/screen_dpi` = 160): bytes `AA 55 AA 55 | 1C 00 00 00 | 99 00 00 00 | 66 FF FF FF | 10 00 00 00 | "/tmp/screen_dpi" 00 | 04 00 00 00 | A0 00 00 00`
(payload 28 bytes = 4 + 16 + 4 + 4).

Every upload is staged via `/tmp/uploadFileTmp`; there is no path whitelist (CCPA `usb_protocol.md:859-896`).

Known paths. Abbreviations: HAG = CCPA `documentation/04_Implementation/host_app_guide.md`, CFG =
`01_Firmware_Architecture/configuration.md`, USB = `02_Protocol_Reference/usb_protocol.md`,
INIT = `01_Firmware_Architecture/initialization.md`, ISS = `02_Protocol_Reference/inbound_session_sequence.md`.

| Path | Content | Values / meaning | Sent by | Source |
|------|---------|------------------|---------|--------|
| `/tmp/screen_dpi` | u32 | DPI (160, 240, ...) | NCP, PYC, LIVI8, CCPA hosts | NCP `sendable.ts:185-221`; HAG:731 |
| `/etc/android_work_mode` | u32 | 0 idle, 1 Android Auto, 2 CarLife, 3 AndroidMirror, 4 HiCar, 5 ICCOA. **Reset to 0 by the dongle on every disconnect: resend on every connection.** Without 1 here, wireless AA pairing fails even though BT pairs. | NCP (if `androidWorkMode`), LIVI8 (always, value 1 by default) | HAG:683-707,737; CFG:1287-1339; LIVI8 `dongleDriver.ts:72-78,690` |
| `/tmp/night_mode` | u32 | 0 day, 1 night (INIT says 2 = auto [UNCERTAIN]) | all | USB:874; INIT:48 |
| `/tmp/hand_drive_mode` | u32 | 0 LHD, 1 RHD | all | USB:875 |
| `/tmp/charge_mode` | u32 | 0 slow (default), 1 fast charge of the phone port (GPIO 6/7) | NCP (true), LIVI8 (true), PYC (0) | USB:918-931 |
| `/etc/box_name` | ASCII, max 16 | name shown in CarPlay | all | NCP `sendable.ts:185-221` |
| `/etc/airplay.conf` | text, `key = value\n` lines | NCP/LIVI8 write `oemIconVisible = 1`, `name = AutoBox`, `model = Magic-Car-Link-1.00`, `oemIconPath = /etc/oem_icon.png`, optional `oemIconLabel = <label>`: controls the OEM "back to car" icon in CarPlay | NCP `sendable.ts:312-338`; LIVI8 `sendables.ts:393-419` |
| `/etc/oem_icon.png` | PNG | OEM icon referenced by airplay.conf | (path only) | NCP `sendable.ts:191` |
| `/etc/icon_120x120.png`, `/etc/icon_180x180.png`, `/etc/icon_256x256.png` | PNG | CarPlay car icons | LIVI8 `uploadHostIcons` | LIVI8 `dongleDriver.ts:467-471`; INIT:47 |
| `/etc/airplay_car.conf`, `/etc/carplay_logo_type` | | listed by LIVI8, unused | | LIVI8 `sendables.ts:107-127` |
| `/etc/RiddleBoxData/HU_VIEWAREA_INFO` | 24 B, 6 x u32 | `[screenW, screenH, viewW, viewH, originX, originY]` | LIVI8, CCPA hosts | LIVI8 `sendables.ts:148-169`; HAG:816-819 |
| `/etc/RiddleBoxData/HU_SAFEAREA_INFO` | 20 B, 5 x u32 | `[safeW, safeH, originX, originY, drawOutside]`, drawOutside 1 = wallpaper fills view area, UI stays in safe area. Only way to set a main screen safe area (CarPlay only) | LIVI8, CCPA hosts | LIVI8 `sendables.ts:170-191`; HAG:817-822; CFG:965-988 |
| `/etc/RiddleBoxData/HU_NAVISCREEN_VIEWAREA_INFO` / `..._SAFEAREA_INFO` | 24 B / 20 B | same layouts for the cluster screen; send **after** BoxSettings (BoxSettings rewrites them full size) | LIVI8 | LIVI8 `dongleDriver.ts:655-683` |
| `/etc/RiddleBoxData/HU_SCREEN_INFO` | 24 B | `[w, h, 0, 0, fps, dpi]` | none | HAG:815 |
| `/tmp/screen_fps`, `/tmp/screen_size` | | set from Open by the firmware | none | USB:872-873 |
| `/tmp/gnss_info` | NMEA text | GPS fallback | none | USB:877 |
| `/tmp/<model>_Update.img` (e.g. `A15W_Update.img`) | firmware | **auto starts OTA update**, never write by accident | LIVI8 updater | FWU = `04_Implementation/firmware_update.md:54-90`; LIVI8 `FirmwareUpdateService.ts:120,295` |
| `/tmp/hwfs.tar.gz`, `*.hwfs` | archive | auto extracted / module upgrade | none | USB:881-911 |

View/safe area rules: `safe area <= view area <= Open size` or Phase 13 loop; Open resets
`HU_VIEWAREA_INFO` to full screen when the size changes, so send these files after Open. Android Auto
ignores them (AA only supports a symmetric margin via `androidAutoSizeW/H`) (CFG:891-924). LIVI8 rounds
all values to even numbers.

### 3.6 BoxSettings (0x19, OUT): JSON

Payload: UTF-8/ASCII JSON object, no NUL terminator in NCP/LIVI8.

What implementations send:

* NCP (`sendable.ts:264-289`): `{"mediaDelay":300,"syncTime":<unix seconds>,"androidAutoSizeW":<w>,"androidAutoSizeH":<h>}`.
  Note NCP's helper is named `getCurrentTimeInMs` but returns **seconds** (`utils.ts:5-7`). FCP also sends
  seconds (`time(nullptr)`), CXX sends epoch **milliseconds** (CXX `src/dongle.cpp:14-18`). The firmware
  does `settimeofday(syncTime + 28800)`, i.e. seconds (CCPA). Use seconds.
* FCP (`src/protocol/connection.cpp:550-555`): `syncTime`, `mediaDelay` 300, `androidAutoSizeW/H` (AA tier fit),
  `drivePosition`, `HiCarConnectMode` 0, `GNSSCapability` 7, `DashboardInfo` 1, `UseBTPhone` 0. FCP also sends
  type 0xA2 `{"DayNightMode":n}` after BoxSettings (`connection.cpp:538-569`).
* CXX (`src/protocol.h:272-288`): same keys as FCP but `GNSSCapability` 0 ("advertise only with a real GNSS feed").
* CLK (`protocol/MessageSerializer.kt:439-518`): `mediaDelay` 1000, `syncTime` (s), `androidAutoSizeW/H`,
  `mediaSound` 1, `WiFiChannel` 36, `DashboardInfo` 7, `GNSSCapability` 3, `boxName`, `autoConn` true,
  `autoPlay` false; deliberately **not** `callQuality` / `OemName`. CLK warns: `CallQuality`/`VoiceQuality`
  are rejected ("apk callQuality value transf box value error") and value 2 (24 kHz) "is documented to
  soft-brick mic input, adapter's AirPlay AudioStream input buffer is hardcoded 640B (16kHz x 20ms x 2B)"
  (`MessageSerializer.kt:446-456`; CLK `documents/revisions.txt:327-332`). CLF sends `callQuality: 2` and
  `wifiChannel`/`WiFiChannel` 161: do not copy that.
* CLR (`source/ccpa.rs:594-625`): `mediaDelay` 300, `syncTime`, AA size, `mediaSound` 1, `WiFiChannel` 36,
  `DashboardInfo` 5, `GNSSCapability` 3, `boxName` "CarLink", `autoConn` 1, `UseBTPhone` 0, `CallQuality` 1, `VoiceQuality` 1.
* AA size from display size, CLK algorithm (`MessageSerializer.kt:403-432`): landscape tier by width
  (>= 1920 -> 1920x1080, >= 1280 -> 1280x720, else 800x480; portrait mirrored); if displayAR >= tierAR then
  W = tierW, H = (int)(tierW / AR) & 0xFFFE capped at tierH, else H = tierH, W = (int)(tierH * AR) & 0xFFFE
  capped at tierW. The dongle then sends the tier frame with your content area; crop it.
* LIVI8 (`protocol/sendables.ts:270-370`, constants `driver/dongle/dongleConfig.ts`):

```json
{
  "mediaDelay": 1000,
  "syncTime": 1762300000,
  "androidAutoSizeW": 1280, "androidAutoSizeH": 720,
  "wifiChannel": 36,
  "mediaSound": 1,
  "callQuality": 1,
  "gps": 1,
  "DashboardInfo": 7,
  "GNSSCapability": 15,
  "autoConn": 1,
  "UseBTPhone": 0,
  "wifiName": "LIVI (D)", "btName": "LIVI (D)",
  "boxName": "App", "OemName": "App",
  "naviScreenInfo": {"width":1280,"height":720,"fps":60,
                     "safearea":{"width":1280,"height":720,"x":0,"y":0,"outside":0}}
}
```

  LIVI8 notes: `mediaDelay` comment "Steers a buffer inside the dongle firmware. Warning: the Dongle has
  a known bug, it resonates between 375-425ms" (`dongleConfig.ts:5-7`); `wifiChannel` default 36 for
  5 GHz, 1 for 2.4 GHz (`sendables.ts:308-312`); `androidAutoSizeW/H` snapped to an AA tier
  800x480, 1280x720, 1920x1080, 2560x1440, 3840x2160 (`shared/utils/androidAuto.ts:38-81`);
  `naviScreenInfo` only when a cluster display is configured.
  **Key spelling conflict:** LIVI8 sends `wifiChannel`, the CCPA firmware table only lists `WiFiChannel`
  (`usb_protocol.md:665`). JSON keys are case sensitive in cJSON lookups (cJSON's
  `cJSON_GetObjectItem` is case insensitive, `cJSON_GetObjectItemCaseSensitive` is not) [UNCERTAIN which
  the firmware uses]: send `WiFiChannel`, optionally both.

Firmware key table (CCPA, binary verified against `ARMadb-driver` 2025.10.15, `usb_protocol.md:636-684`):

| JSON key | riddle.conf key | Type / values | Effect |
|----------|-----------------|---------------|--------|
| `btName` | CustomBluetoothName | string | dongle BT name. Passed to a shell via popen without escaping: never put quotes or shell metacharacters in it |
| `wifiName` | CustomWifiName | string | dongle Wi-Fi SSID, same injection caveat |
| `fps` | CustomFrameRate | int | overrides Open fps |
| `gps` | HudGPSSwitch | 0/1 | GPS from head unit enabled |
| `lang` | BoxConfig_UI_Lang | string | dongle UI language |
| `bgMode` | BackgroundMode | int | dongle connection UI visibility |
| `syncMode` | iAP2TransMode | int | iAP2 framing mode, leave alone |
| `startDelay` | BoxConfig_DelayStart | int s | USB init delay |
| `mediaDelay` | MediaLatency | int ms | dongle side audio buffer |
| `mediaSound` | MediaQuality | 0 = 44.1 kHz, 1 = 48 kHz | media PCM rate |
| `autoConn` | NeedAutoConnect | 0/1 | auto reconnect last phone |
| `androidWorkMode` | AndroidWorkMode | 0..5 | also writes `/etc/android_work_mode` (0 idle, 1 AA, 2 CarLife, 3 Mirror, 4 HiCar, 5 ICCOA) |
| `drivePosition` | CarDrivePosition | 0 LHD, 1 RHD | |
| `echoDelay` | EchoLatency | int ms | WebRTC AEC delay for mic processing |
| `androidAutoSizeW` / `androidAutoSizeH` | AndroidAutoWidth / Height | int px | AA video size |
| `screenPhysicalW` / `screenPhysicalH` | ScreenPhysicalW / H | int mm | physical size, used for CarPlay DPI |
| `brand` | CarBrand | string | rewrites airplay.conf and logo |
| `ScreenDPI` | ScreenDPI | int | CarPlay render density |
| `boxName` | CustomBoxName | string | display name |
| `WiFiChannel` | WiFiChannel | 1..11, 36..165 | dongle AP channel |
| `UseBTPhone` | UseBTPhone | 0/1 | route calls over BT HFP instead of USB audio |
| `HiCarConnectMode` | HiCarConnectMode | int | |
| `GNSSCapability` | GNSSCapability | bitmask: 1 GPGGA, 2 GPRMC, 8 PASCD | default 0 = phone never asks for location |
| `AutoResetUSB` | AutoResetUSB | 0/1 | USB reset on disconnect |
| `DashboardInfo` | DashboardInfo | bitmask, 0..7 | value 4 (bit 2) = route guidance, needed for navigation JSON (0x2A subtype 200). Bits 0/1 are described differently: `usb_protocol.md:1067-1070` says 1 vehicleInformation, 2 vehicleStatus; `configuration.md:416-438` and CLK say 1 MediaPlayer/now playing, 2 Location (needs GNSSCapability). LIVI8 comment: media(1), vehicle(2), route(4). Use 5 or 7 |
| `DayNightMode` | DayNightMode | int | |
| `DockPosition` | DuckPosition (firmware typo) | int | |

Special keys handled before the table (not stored in riddle.conf) (CCPA `usb_protocol.md:674-684`):

| Key | Effect |
|-----|--------|
| `syncTime` | `settimeofday(syncTime + 28800)`: Unix seconds, firmware adds a hardcoded UTC+8 offset |
| `mediaVol`, `callVol`, `speechVol`, `ringVol`, `navVol`, `otherVol` | floats 0.0..1.0 into `HU_AUDIOVOLUME_INFO[0..5]` |
| `naviScreenInfo` `{width,height,fps}` | registers the cluster / nav screen; enables type 0x2C video (5.6) |
| `naviScreenInfo.safearea` `{width,height,x,y,outside}` | nav screen safe area |

Keys that do nothing on 2025.10 firmware according to CCPA: `autoPlay` (no mapping), `callQuality`
("removed/broken", no observed effect) (`usb_protocol.md:696,725`).

Ranges and defaults (from `riddleBoxCfg --info`, CCPA `01_Firmware_Architecture/configuration.md:1138-1232,1562-1675`):
`btName`/`wifiName`/`boxName` 0..15 chars; `fps` 0 or 20..60 (a ceiling, not a target); `startDelay`
0..120; `mediaDelay` 300..2000, default 1000, **values below 500 are clamped to 500** by AppleCarPlay
(CFG:218-228); `mediaSound` default 1; `autoConn` default 1; `echoDelay` 20..2000, default 320;
`androidAutoSizeW/H` 0..4096; `ScreenDPI` 0..480 (0 auto); `WiFiChannel` default 36; `GNSSCapability`
default 0; `AutoResetUSB` default 1; `DashboardInfo` 0..7, default 1; `DayNightMode` 0 auto (follow
commands 16/17), 1 force day, 2 force night, default 2; `DockPosition` 0 left, 1 right, 2 bottom.
Also not mapped (inert): `oemName`/`OemName`, `lightType`, `productType` (CFG:1176,1519; CFG:1941 claims
`OemName` maps to CustomBoxName [UNCERTAIN]). `GNSSCapability` and `DashboardInfo` changes may only take
effect after the iAP2 identification datastore is rebuilt and the dongle rebooted (CFG:450-453).

Practical guidance: values persist in `/etc/riddle.conf` on the dongle across reboots, so a stale value
from another host (e.g. the vendor app) stays active until you overwrite it. Send every key you care about
on every start. Keep strings plain alphanumeric (the name fields go unescaped into shell commands).

### 3.7 BoxSettings (0x19, IN): JSON from the dongle

Two shapes (CCPA `usb_protocol.md:806-846,2157-2170`; NCP `readable.ts:315-337`; LIVI8 `messages/dongleEvents.ts:157-189`):

Box info (sent during init):

```json
{"uuid":"651ede982f0a99d7f9138131ec5819fe","MFD":"20240119","boxType":"YA",
 "OemName":"carlink_test","productType":"A15W","HiCar":1,"hwVersion":"YMA0-WR2C-0003",
 "WiFiChannel":36,"CusCode":"","DevList":[{"id":"DE:AD:BE:EF:00:06","type":"CarPlay"}],"ChannelList":"..."}
```

Phone info (sent when a phone session forms, about 3 s after Plugged):

```json
{"MDLinkType":"CarPlay","MDModel":"iPhone18,4","MDOSVersion":"23D5089e","MDLinkVersion":"935.3.1",
 "btMacAddr":"DE:AD:BE:EF:00:06","btName":"","cpuTemp":53}
```

* `MDLinkType` values seen: `CarPlay`, `AndroidAuto` (CCPA). LIVI8 treats `RiddleLinktype_UNKNOWN?` /
  `RiddleLinktype_UNKOWN?` (firmware typo) as "the phone is the other kind than the current
  phoneWorkMode": it flips phoneWorkMode 2 <-> 4 by sending DisconnectPhone, waiting 120 ms and sending
  Open again with the other mode (LIVI8 `dongleDriver.ts:167-190,765-785`).
* `DevList` entries in LIVI8's type: `id, type, name, index, time, rfcomm` (`dongleEvents.ts:157-165`).

### 3.8 HeartBeat (0xAA, OUT, empty)

* Every implementation sends it every **2000 ms** (NCP `DongleDriver.ts:258-260`; PYC `protocol.py:122-124`
  `lifecycle = 2`; LIVI8 `dongleDriver.ts:608-613`).
* Where it starts: NCP starts it after sending the init batch; LIVI8 v8.3.0 starts it when the IN Open
  echo arrives and **stops it on Unplugged** and restarts on the next Open echo (`dongleDriver.ts:608-613,729-740`).
  CCPA says it "must start BEFORE initialization messages on cold start" and that the dongle disconnects
  at about 10 to 11.7 s without heartbeat (binary constant 15000 ms) (`usb_protocol.md:1225-1253`).
  Safe choice: start the 2 s heartbeat timer immediately after claiming the interface, never stop it
  while the device is open.

Firmware details (CCPA `documentation/01_Firmware_Architecture/heartbeat_analysis.md`):

| Fact | Value | Line |
|------|-------|------|
| Direction | host to dongle only; on 2025.10.15.1127CAY the dongle never sends heartbeats back (716 sent, 0 received in a 1452 s capture). Do not wait for an echo. | 90-92 |
| Dongle watchdog | 15000 ms without heartbeat: "Host No Response, we will reset connection!!!" | 100,150-157 |
| Practical budget | problems from about 10 s after USB connect, disconnect seen at 11.7 s: design for 10 s | 14-21,105-111 |
| Interval | 2000 ms recommended (firmware has a 2000 ms timer constant), never more than 5000 ms | 117-130,303-307 |
| When to start | timer started before the init messages; first beat at t = 2 s (after Open went out) | 29-56 |
| Config | `SendHeartBeat` in riddle.conf only affects heartbeats sent by the dongle; the watchdog always runs | 197-216 |
| Dongle write timeout | 4500 ms: "Send to Host timeout, we will reset connection!!!". Implication: **always keep a bulk IN read pending**, or the dongle resets the link when it cannot deliver data for 4.5 s | 101,233 |

### 3.9 MediaData / MetaData (0x2A, IN)

Payload: u32 subtype at offset 0, data from offset 4.

| Subtype | Content | Source |
|---------|---------|--------|
| 1 | JSON now playing, **followed by one trailing NUL byte** (NCP and LIVI8 drop the last byte before JSON.parse) | NCP `readable.ts:254-293`; LIVI8 `decode.ts:83-107` |
| 2 | album art from Android Auto: **PNG** at offset 4 (CLK); LIVI8 treats like 3 | CLK `protocol/MessageTypes.kt:462-471`; LIVI8 `decode.ts:83-95`, `readable.ts:99-104` |
| 3 | album art: raw JPEG bytes (`FF D8 FF E0`, 300x300 to 600x600, 170 to 180 KB) | CCPA `usb_protocol.md:266-308`. LIVI8 also accepts base64 ASCII here. |
| 100 | CallStatus JSON (CCPA) / "ControlAutoplayTrigger" (LIVI8, ignores payload) | CCPA `usb_protocol.md:310-353`; LIVI8 `readable.ts:99-104` |
| 101 | communication update JSON, only with patched firmware | CCPA `usb_protocol.md:273` |
| 200 | navigation JSON (needs `DashboardInfo` value 4 set) | CCPA `usb_protocol.md:355-427`; LIVI8 `decode.ts:125-150` |
| 201 | navigation maneuver icon PNG (Android Auto only) | CCPA `usb_protocol.md:275,1515` |

Now playing JSON keys (subtype 1): `MediaSongName`, `MediaAlbumName`, `MediaArtistName`,
`MediaAPPName`, `MediaSongDuration` (ms), `MediaSongPlayTime` (ms), `MediaPlayStatus` (int, 1 = playing
[UNCERTAIN other values]). Messages are partial updates: merge keys into state (e.g. play time arrives
alone). (CCPA `usb_protocol.md:291-302`; LIVI8 `readable.ts:111-119`.)

CallStatus JSON (subtype 100): `CallStatus` (0 idle/ended, 1 dialing, 2 ringing, 3 connecting, 4 active),
`CallDirection` (1 incoming, 2 outgoing), `CallID`, `CallName`, `CallNumber`. Arrives about 300 ms after
the matching audio command (CCPA `usb_protocol.md:310-353`).

Navigation JSON (subtype 200), partial updates: `NaviStatus` (0 inactive, 1 active; AA uses 2 for
inactive), `NaviTimeToDestination` (s), `NaviDestinationName`, `NaviDistanceToDestination` (m),
`NaviAPPName`, `NaviRemainDistance` (m to next maneuver), `NaviRoadName`, `NaviOrderType`,
`NaviManeuverType` (CarPlay CPManeuverType 0..53, table in LIVI8 `shared/types/NavigationTypes.ts:6-60`),
`NaviTurnAngle`, `NaviTurnSide`, `NaviRoundaboutExit`, AA only: `NaviNextTurnTimeSeconds`. The JSON can
contain **duplicate keys** (`NaviRoadName` twice). Android Auto sends `NaviOrderType` instead of
`NaviManeuverType` with a different enum. (CCPA `usb_protocol.md:355-427,1458-1539`.)

### 3.10 CarPlayModeChange (0x0B, IN)

28 byte variant, 7 x i32 (CCPA `usb_protocol.md:1851-1869`): handDriveMode (0 L, 1 R), nightMode (0/1),
screenMode, audioMode (0 n/a, 1 take, 2 untake, 3 borrow, 4 unborrow), phoneMode (-1 not, 1 in call),
speechMode (-1 not, 1 speaking, 2 recognizing), naviMode (-1 not, 1 navigating). Other sizes exist;
parse by length. Optional (useful for Siri UI state). No open source host uses it.

### 3.11 GnssData (0x29, OUT)

CCPA (`usb_protocol.md:969-993`): `u32 nmeaLength` then NMEA ASCII (`\r\n` terminated), max about 1 KB.
**Conflict:** LIVI8 sends the NMEA text without the u32 length prefix (`sendables.ts:63-78`) and it was
working in LIVI. [UNCERTAIN] Requires `GNSSCapability > 0` in BoxSettings and Command 18
(StartGNSSReport) from the dongle. Optional for a first version.

### 3.12 Encryption (0xF0): do not use

The vendor AutoKit app sends `0xF0` with a random u32 seed to switch the link to AES with magic
`0x55BB55BB` for all types except 0x06, 0x07, 0x2A, 0x2C. It cannot be switched off until reboot.
Not needed for CarPlay or Android Auto (CCPA `usb_protocol.md:1682-1809`). Only FCP implements it (optional,
off by default): AES-128-CFB per message, key `"SkBRDy3gmrw1ieH0"[(seed + i) % 16]`, IV 16 zero bytes with
`iv[1]=seed&0xFF, iv[4]=seed>>8, iv[9]=seed>>16, iv[12]=seed>>24` (FCP `src/protocol/aes_cipher.cpp:22-68`). CCPA notes
the firmware binary calls `AES_cbc_encrypt` while AutoKit uses CFB [UNCERTAIN; FCP's CFB is what works].

---

## 4. Session lifecycle

### 4.1 Startup sequences used by implementations

All sends are fire and forget unless noted. "cmd N" = Command (0x08) with id N. "file X = v" = SendFile.

**NCP** (`src/modules/DongleDriver.ts:214-261`, `src/node/CarplayNode.ts:114-150`), all sent at once
(`Promise.all`), then:

1. file `/tmp/screen_dpi` = 160
2. Open(800, 640, 20, 5, 49152, 2, 2)
3. file `/tmp/night_mode` = 0/1
4. file `/tmp/hand_drive_mode` = 0 (LHD) / 1 (RHD)
5. file `/tmp/charge_mode` = 1
6. file `/etc/box_name` = "nodePlay"
7. BoxSettings `{mediaDelay:300, syncTime, androidAutoSizeW, androidAutoSizeH}`
8. cmd 1000 (wifiEnable)
9. cmd 25 (5 GHz) or 24
10. cmd 7 (car mic) or 15
11. cmd 23 (audio over USB) or 22
12. optional file `/etc/android_work_mode` = 1
13. start read loop; after **1000 ms** cmd 1002 (wifiConnect); heartbeat every 2000 ms
14. after **15000 ms** without Plugged: cmd 1012 (wifiPair)
15. on Plugged with phoneType 3: cmd 12 every 5000 ms

**LIVI8** (`src/main/services/projection/driver/dongle/dongleDriver.ts:321-342,608-727,856-884`), 120 ms
between messages:

1. Open(w, h, fps, 5, 49152, 2, phoneWorkMode = last used mode, 2 CarPlay or 4 Android)
2. wait for the **IN Open** echo ("DongleReady"); then start heartbeat (2000 ms) and send the post open config:
3. file `HU_VIEWAREA_INFO`, file `HU_SAFEAREA_INFO`
4. BoxSettings (full JSON, 3.6)
5. cluster `HU_NAVISCREEN_VIEWAREA_INFO` / `..._SAFEAREA_INFO` (only with cluster)
6. file `/etc/box_name`, `/tmp/night_mode`, `/tmp/screen_dpi` (AA dpi formula), `/etc/android_work_mode` = 1,
   `/tmp/charge_mode` = 1, `/etc/airplay.conf` (icon config), `/tmp/hand_drive_mode`
7. cmd 7 (mic), cmd 25/24, cmd 23/22
8. after 150 ms cmd 1002, or type 0x11 with a BT MAC to connect to a specific phone
9. 15 s after bring up without Plugged: cmd 1012

LIVI v4.1.3 (older, same project): dpi, Open, night, hand, box_name, airplay.conf, charge, 25/24, BoxSettings,
1000, 7/15, 23/22, optional android_work_mode; 120 ms gaps; 1002 after 600 ms; heartbeat after that
(github.com/f-io/LIVI `src/main/carplay/driver/DongleDriver.ts@v4.1.3:211-235`).

**FCP** (`src/protocol/connection.cpp:498-569`): [0xF0 seed if encryption enabled], dpi (default 120),
`/etc/android_work_mode` = 1 (always, before Open), Open, BoxSettings, 0xA2 `{"DayNightMode":n}`, night_mode
(0/1/2), charge_mode (0 or 2), box_name, hand_drive_mode, mic cmd (7/15/21), 25/24, 23/22, cmd 1002
immediately. No cmd 1000, no 1012. Heartbeat only after 3 s of no other writes.

**CXX** (`src/dongle.cpp:124-184`): dpi, Open, night_mode, hand_drive_mode, charge_mode = 1, box_name,
BoxSettings, 1000, 25/24, 7/15, 23/22; heartbeat thread (2 s, first immediately); sleep 1 s; cmd 1002.

**CLK** (Kotlin CCPA host, the most field tested; `CarlinkManager.kt:1051-1170`, `protocol/AdapterDriver.kt:113-157`,
`protocol/MessageSerializer.kt:574-835`): open USB, send 0x0F + 0x15, sleep 200 ms; start heartbeat (first beat at
+2000 ms, fixed rate 2000 ms); then with **120 ms after each**: dpi 160, Open, `HU_VIEWAREA_INFO`,
`HU_SAFEAREA_INFO`, `/etc/android_work_mode` (always), `/tmp/hand_drive_mode` 0, `/etc/box_name`
"carlink", `/tmp/charge_mode` 0, icons (if any), cmd 25/24, BoxSettings, `/etc/airplay.conf` (**after**
BoxSettings: "firmware rewrites airplay.conf during BoxSettings processing"), cmd 7/15, cmd 23/22, and
**cmd 1000 last** ("WIFI_ENABLE MUST be the final message"); 600 ms later cmd 1002; read loop; 15 s pair
timer -> cmd 1012. No night_mode file at init: CLK sends cmd 16/17 at runtime when the theme changes.

**CLF** (Dart; `lib/carlink.dart:207-278`, `lib/driver/adaptr_driver.dart:59-175`): heartbeat first, then no
delays: **Open first**, dpi, box_name, airplay.conf, icons, BoxSettings, 25/24, 15 or 7, 23/22, 1000,
android_work_mode only if enabled; +600 ms cmd 1002 ("ensures wifiConnect is sent even if Opened message
doesn't arrive (cold start issue)"); 15 s -> 1012.

**CLR** (Rust; `crates/carlink/src/source/ccpa.rs:79-135,481-546`): heartbeat and reader first, then 120 ms gaps:
dpi 160; Open; BoxSettings; view area; safe area; android_work_mode 1 + cmd 1000 + cmd 1002; hand_drive 0 +
box_name; charge_mode 0 + cmds 25, 7, 23; airplay.conf. Comment: "If a phone is already mid-session when we
attach, it negotiated against the unconfigured adapter and the firmware rejects it (phase 13), re-plug the
phone to recover" (`ccpa.rs:121-126`).

**CCPA recorded vendor-like host** (CCPA `02_Protocol_Reference/inbound_session_sequence.md:28-45`, times from
USB attach): +0.124 s heartbeat timer started (first beat +2 s); +0.381 dpi; +0.510 Open; +0.636 BoxSettings
(about 280 B); +0.764 `HU_VIEWAREA_INFO`; +0.890 `HU_SAFEAREA_INFO`; +2.364 `/etc/android_work_mode`;
+2.523 cmd 1000 then cmd 1002; +2.81 `/tmp/hand_drive_mode`, `/etc/box_name`; +2.94 `/tmp/charge_mode`, cmd 25,
cmd 7, cmd 23; +3.108 `/etc/airplay.conf`.

**Vendor AutoKit app** (decompiled, CCPA `01_Firmware_Architecture/initialization.md:37-54`): 0xA0 app info,
0xF0 encryption, dpi, android_work_mode, Open, wait for Open reply, BoxSettings, 0xA0, icons, night_mode,
charge_mode, 0x0D/0x0E names, box_name, mic / wifi band / audio commands.

### 4.2 Recommended startup for the plugin (synthesis)

1. Open USB, claim (1.3). Submit IN reads immediately and keep them pending. Send DisconnectPhone (0x0F)
   and CloseDongle (0x15), wait 200 to 300 ms, so a session left over from a previous host run is cleared
   (CLK; the firmware otherwise ignores Open while it holds a stale session).
2. Start heartbeat timer, 2000 ms period, first beat at t = 2 s, runs until close.
3. file `/tmp/screen_dpi` (u32).
4. file `/etc/android_work_mode` = 1 if you want Android Auto support, else 0 (every connection).
5. Open(width, height, fps, 5, 49152, 2, 2 for CarPlay or 4 for Android Auto). Even dimensions.
6. Wait for the IN Open (0x01) echo, or proceed after about 500 ms (some docs say the reply can be 0xFF
   CMD_ACK or 0x1E; accept any of them as "ready", none is strictly required).
7. BoxSettings JSON (seconds `syncTime`, `mediaDelay` >= 500, `mediaSound`, `WiFiChannel`, `wifiName`/`btName`/
   `boxName`, `androidAutoSizeW/H` = AA tier, `autoConn`, optional `naviScreenInfo`, `DashboardInfo` 5 or 7
   for metadata, `GNSSCapability` only if you send GPS).
8. files `HU_VIEWAREA_INFO`, `HU_SAFEAREA_INFO` (optional, CarPlay insets), `/tmp/night_mode`,
   `/tmp/hand_drive_mode`, `/tmp/charge_mode`, `/etc/box_name`, optionally `/etc/airplay.conf` + icons.
9. cmd 7 (car mic), cmd 25 (5 GHz; applies after a dongle reboot) or 24, cmd 23 (audio over USB).
   If you write `/etc/airplay.conf`, do it after BoxSettings (CLK).
10. cmd 1000 (wifiEnable), then after 150 to 1000 ms cmd 1002 (start auto connect).
11. Optional: after 15 s with no Plugged, cmd 1012 (pairing mode).
12. Send messages about 100 to 120 ms apart (LIVI8, CCPA) rather than all at once.

Re-send the whole sequence after every re-enumeration, dongle reboot, or CloseDongle (0x15).

### 4.3 What the dongle sends back

CLK observed: Phase 7 about 1 s after Plugged, Phase 8 about 1.8 s after that, 0xA3 in the same millisecond
as Phase 8 (CLK `protocol/MessageParser.kt:738-770`).

Typical inbound order (CCPA `inbound_session_sequence.md:51-130` (marked "non-definitive"),
`usb_protocol.md:1301-1329`, `04_Implementation/session_examples.md:99-201`):

| Stage | Messages |
|-------|----------|
| Immediately after Open (about 0 to 150 ms) | cmd 1000, 0x0D BT name, 0x0E Wi-Fi name, 0x26, 0x12 paired list, cmd 1001, cmd 7, 0xCC SoftwareVersion, 0x19 box info JSON, 0x01 Open echo |
| Scanning (about +0.6 s) | cmd 1003, 0x23 peer BT address |
| BT connected (+2 to 3 s) | optional 0xBB, 0x19 with empty phone model, 0x24, cmd 1007, 0x12, cmd 1004; or 0x25 + cmd 1005 if the phone is not found |
| Session setup | 0x19 phone info, **0x02 Plugged** (phoneType 3, wifi 1 for wireless CarPlay), **0x03 Phase 7** |
| Streaming (about 2 to 3 s after Plugged) | cmd 4, **cmd 500 RequestVideoFocus**, 0x19 phone info with full model, 0xA3 session token, **0x03 Phase 8**, then 0x06 video, 0x07 audio, 0x2A metadata |
| Wired CarPlay with cluster | cmd 508 from dongle (echo it), then 0x2C |
| Idle | cmd 1010 about every 12 s, 0x77 occasionally: ignore |
| End | 0x04 Unplugged and/or Phase 0 |

Timings: Plugged typically at +3 to 4 s, streaming at +5 to 8 s. A **first** wireless pairing can take 75 s or
more (user must accept "Use This Car" on the iPhone), so do not time out between init and Plugged
(CCPA `inbound_session_sequence.md:164-171`).

Phase values (u32), CCPA `usb_protocol.md:1257-1298`:

| Phase | Meaning | Host action |
|-------|---------|-------------|
| 0 | session terminated (firmware kills its CarPlay/iAP2 processes) | full teardown of decoder/audio/mic, wait for next Plugged |
| 4 | waiting for phone to join the dongle Wi-Fi (AutoKit only) | UI hint |
| 5 | waiting for AirPlay (AutoKit only) | UI hint |
| 7 | connecting / negotiating | show "connecting" |
| 8 | streaming active | start video/audio pipelines |
| 11 | CarLife download (AutoKit only) | ignore |
| 13 | AirPlay negotiation failed (view area / safe area larger than Open size) | fix geometry; loops 7 -> 13 |
| 1001 / 1002 / 1003 / 1004 | hardware error: Bluetooth / Wi-Fi / unknown / unknown (AutoKit only) | report |

Note: LIVI8 defines a `BoxPhase` enum with values 0..25, 116..127, 200..214 (`EVT_*` names, e.g. 116
`EVT_BOX_READY`, 200 `EVT_BOX_SUPPORT_AUTO_CONNECT`) (`messages/dongleEvents.ts:209-285`) but never uses it to
interpret Phase messages; it looks like the vendor app's internal event ids. NCP: "TODO: find correct mapped
values" (`readable.ts:339-347`). Trust the CCPA table.

### 4.4 CarPlay vs Android Auto mode

* Open `phoneWorkMode`: 2 = CarPlay, 4 = Android (LIVI8 `PhoneWorkMode`). CCPA documents only 2 and says the
  Open field only drives the iPhone stack (`usb_protocol.md:2133-2145`); the Android stack is driven by
  `/etc/android_work_mode` (1 = Android Auto), which the dongle resets to 0 on every disconnect.
* LIVI8 strategy: start with the last used mode; on Plugged phoneType 3 use 2, otherwise 4; if the dongle
  reports `MDLinkType` "RiddleLinktype_UNKNOWN?" or "RiddleLinktype_UNKOWN?" flip the mode: DisconnectPhone,
  120 ms, Open with the other mode (mode switches rate limited to one per 800 ms). When the android work mode
  changes it resends the file, cmd 1000 and (150 ms later) cmd 1002 (LIVI8 `dongleDriver.ts:144-190,742-812`).
* Android Auto specifics: video size from `androidAutoSizeW/H` (tiers only), no `frame` requests, no ducking
  commands, mic dt 3 for calls, nav JSON uses `NaviOrderType`, 0x2A subtype 201 icons.

### 4.5 Shutdown and errors

| Action | How | Source |
|--------|-----|--------|
| Drop the phone, keep dongle | send 0x0F (empty) | NCP `sendable.ts:344-348` |
| Stop everything | send 0x0F then 0x15, wait about 150 ms, release interface, close | LIVI8 `dongleDriver.ts:447-462` |
| Phone gone | IN 0x04 or Phase 0: stop pipelines, keep USB and heartbeat running, wait for next Plugged | CCPA `usb_protocol.md:2191-2226` |
| Dongle gone | transfer error `LIBUSB_ERROR_NO_DEVICE` / `LIBUSB_TRANSFER_NO_DEVICE`: close, wait for hotplug | LIVI8 `dongleDriver.ts:245-258`; CXX `src/dongle.cpp:52-86` |
| Parse errors | 5 consecutive bad headers: close and restart (NCP, LIVI8) | 1.4 |
| Never disconnect on | cmd 1000..1013 (especially 1010), cmd 3, 14, 28, 29, types 0x25, 0x77, 0x0B | CCPA `usb_protocol.md:2251-2303` |

---

## 5. Video

Short names below: VP = CCPA `documentation/02_Protocol_Reference/video_protocol.md`.

### 5.1 VideoData (0x06) payload layout

| Payload off | Type | NCP / PYC / LIVI8 name | CCPA meaning (VP) |
|-------------|------|------------------------|-------------------|
| 0 | u32 | width | encoded frame width (e.g. 2400) (VP:75) |
| 4 | u32 | height | encoded frame height (VP:76) |
| 8 | u32 | flags | **EncoderState**: encoder generation / stream id, not flags. Wired CarPlay steady 3, wireless CarPlay steady 7 (other values at startup), Android Auto 3, nav video 1. Not related to frame type (VP:77,87-127) |
| 12 | u32 | length (NCP) / unknown1 (PYC) | **PTS** in milliseconds (1 kHz clock) (VP:78) |
| 16 | u32 | unknown | flags, usually 0 (VP:79) |
| 20 | bytes | H.264 data | Annex B elementary stream with `00 00 00 01` start codes (VP:83,411,423) |

Sources for the implementations: NCP `readable.ts:235-252`; PYC `protocol.py:251-261`
("at least for format==5, self.data is h264"); LIVI8 `protocol/decode.ts:62-71`. NCP's name "length" for
offset 12 is wrong per CCPA. **Do not derive the codec from offset 8**: some host code treats bits 2..3
as a codec selector (1 H.264, 2 MJPEG, else H.265) and misdetects value 3; the stream is always H.264,
parse the SPS (VP:127,195,1067).

Each USB message carries exactly one access unit (one frame), already Annex B: you can feed
`payload[20:]` straight into a decoder (GStreamer caps `video/x-h264, stream-format=byte-stream, alignment=au`
as in CLR `crates/carlink/src/sink/gst_video.rs:29-34`). CLK gates the decoder until the first SPS/IDR and
feeds everything with flags 0 (no separate codec config buffer) (CLK `video/H264Renderer.java:22-34,752-792`).

**Conflict on offset 8:** CLK interprets it as bit 0 = "offScreen", bits 2..3 = encoder (0 H.265, 1 H.264,
2 MJPEG) (CLK `CarlinkManager.kt:3379-3405`) and even echoes `encoderType | (offScreen << 16)` in the Touch
flags field (`MessageSerializer.kt:145-179`). The CCPA video doc (same author, later) says this misdetects
value 3 and that the stream is always H.264. Trust the SPS, ignore offset 8. [UNCERTAIN whether Touch flags
matter; everyone else sends 0 and it works]

### 5.2 Codec facts

| Item | Value | Source |
|------|-------|--------|
| Codec | H.264 only. No HEVC observed through the dongle (the dongle passes the phone's bitstream through untouched, it does not transcode) | VP:127,356,370-379,1376 |
| CarPlay profile | High (100). Level per session: 3.1 at 1280x720, 4.0 at 2400x788@30, 5.0 at 2400x960@60 | VP:421-436,915 |
| Android Auto profile | Constrained Baseline (66), level 3.1 | VP:451-465 |
| B frames | none (I and P only, no reordering, decode order = display order) | VP:445,1290 |
| SPS/PPS | always in the same USB message as the IDR: `[SPS][PPS][IDR]`, never alone (0 of 538 IDRs) | VP:1201-1216 |
| Session start | first video message is SPS (about 22 B) + PPS (4 B) + IDR; no initial keyframe request needed | VP:1186-1195 |
| NAL types | 7 SPS (0x67), 8 PPS (0x68), 5 IDR (0x65), 1 non IDR slice (0x41) | VP:679-684 |
| First CarPlay frame | can be a tiny 800x480 boot frame, the iPhone switches to full size within about 350 ms: **handle resolution change mid stream** (new SPS) | VP:447,686-690 |
| First AA frame | always a nearly empty dark IDR (2735 B); AutoKit decodes but does not render the first 4 frames | VP:692-707 |

### 5.3 Keyframes

* Request an IDR with Command 12 (`frame`) or with message type 0x0C and empty payload; the firmware
  treats both identically (CCPA `usb_protocol.md:134-160`). Response: SPS+PPS+IDR in 100 to 200 ms
  (VP:495-501).
* **CarPlay main video is keyframe starved**: one IDR at stream start, then P frames only for minutes
  (0 IDR in 84 s wireless, 1 in 139 s wired) (VP:193,921-927,1022). So request a keyframe after: decoder
  (re)creation, surface recreation (Flutter texture recreated, app resumed), detected corruption, dropped
  packets.
* Each request makes the iPhone restart its encoder (visible PTS gap), so do not spam it (VP:527-540).
* CLK: CarPlay one request 2500 ms after the first video frame, then every 30000 ms ("conservative defense
  for Intel MediaCodec; host-side FRAME cadence is policy, not firmware/iPhone requirement") plus reactive
  requests after a dropped frame with 500 ms cooldown; AA exactly **one** request on the first video frame
  ("AA must NOT receive periodic FRAME commands (resets phone UI)") (CLK `CarlinkManager.kt:1775,2133-2151,3305-3322`,
  `video/H264Renderer.java:186-191,702-713`). CLR: 2500 ms after Phase 8 ("CarPlay needs ~2.5s for its
  encoder to initialise"), then every 60 video frames; AA once on Phase 8 (CLR `source/ccpa.rs:275-321`,
  `app.rs:244-274`). CLF: every 5000 ms for CarPlay like NCP.
* Periodic requests in implementations: NCP every 5000 ms for phoneType CarPlay, none for AA
  (NCP `DongleDriver.ts:68-75`, `CarplayNode.ts:46-60`); LIVI8 none by default but a watchdog sends `frame`
  every 700 ms while no VideoData arrives (`dongleDriver.ts:420-430`); CCPA's own host now uses 30 s
  (2.5 s initially) (VP:558).
* **Android Auto: never send `frame` periodically** (CLK sends exactly one at stream start). On AA it resets the phone's whole projection UI (324 to 540 ms
  glitch). Wait for the natural IDR (about every 60 to 68 s) (VP:544-557,1199). [Note: NCP sends nothing for AA
  either.]
* Open `format` = 5 makes the dongle responsive to frame requests; format 1 gives far fewer IDRs
  (107 vs 27 IDRs in comparable captures) (VP:513-518,715-727).

### 5.4 Resolution, fps, dpi

* CarPlay: the Open width/height/fps become the CarPlay `viewAreas` in AirPlay SETUP; there is no
  resolution check in the dongle forwarding path, practical limits are dongle RAM (128 MB, fixed H.264
  buffer, log "H264 data buffer overrun!") and USB 2.0. Keep under about 25 to 35 Mbit/s; 4K60 likely fails
  (VP:299-315,816-866,889). Real captures used 2400x960@60, 1920x720, 1280x720, 800x480.
  Use even width and height (LIVI8 `toEven` everywhere).
* fps is a maximum: CarPlay output is variable (1.7 to 60 fps, average 27; about 0.3 fps when the screen is
  static). Do not assume a constant frame rate (VP:199,566-585,1238-1245). Render frames as they come.
* DPI: `/tmp/screen_dpi` (SendFile, u32), used values: NCP 160, CCPA capture 240, LIVI8 computes for AA:
  800x480 140, 1280x720 180, 1920x1080 200, 2560x1440 250, 3840x2160 420, linear by pixel count in
  between, rounded to 10 (LIVI8 `shared/utils/androidAuto.ts:93-133`). BoxSettings `ScreenDPI` /
  `screenPhysicalW/H` exist too (3.6).
* Android Auto: size comes from BoxSettings `androidAutoSizeW/H`, not from Open. AA only encodes
  800x480, 1280x720, 1920x1080 (and portrait 720x1280, 1080x1920) through this dongle; 1440p and 2160p
  throw "Unsupported resolution"; 1080p runs at 30 fps, 480p/720p at up to 60 (practically about 29 to 30)
  (VP:1355-1380). Your surface aspect ratio rarely matches: AA letterboxes into the tier (VP:1330-1425;
  LIVI8 `aaContentArea` `androidAuto.ts:20-31`). Bitrate about 4 Mbit/s fixed (VP:643-654).
* View area / safe area: see 3.5 (`HU_VIEWAREA_INFO`, `HU_SAFEAREA_INFO`). Constraint
  `safeArea <= viewArea <= Open resolution`, otherwise AirPlay negotiation fails with **Phase 13** and a
  7 -> 13 reconnect loop (CCPA `usb_protocol.md:1269`, `inbound_session_sequence.md:173-180`).
* Changing resolution needs a new Open (LIVI8 resends Open after DisconnectPhone for mode switches,
  `dongleDriver.ts:167-190`); in practice hosts restart the whole session (close, reset, reopen).

### 5.5 Host decode policy (CCPA recommendations)

* Decode immediately; drop late frames (older than 30 to 40 ms) but never drop an IDR; keep 100 to 200 ms of
  buffer at most; on corruption reset the decoder and request a keyframe (CarPlay only) (VP:28-40,1256,1268-1278,1314).
* If you must drop P frames, drop everything until the next IDR and request one (PhoneMirrorBox strategy:
  if backlog > 2 x fps, send frame, clear, skip P until IDR) (VP:1280).
* Frame sizes: IDR average 49 KB (29 to 138 KB), P average 23 KB, max seen 164824 B (VP:1260-1264,911).
* There is no audio "silence keepalive"; at idle there may be no 0x06 and no 0x07 for long periods.
  Absence of media is not a disconnect (VP:199).

### 5.6 Cluster / navigation video (0x2C), optional

* Same 20 byte header and H.264 Annex B as 0x06 (EncoderState always 1) (VP:131-160). CarPlay only
  (iOS 13+), not AA (VP:1016).
* Enable by sending `naviScreenInfo` `{width, height, fps, safearea:{width,height,x,y,outside}}` in
  BoxSettings (key spelled `safearea` in the verified capture) (VP:211-236; LIVI8 `sendables.ts:340-362`).
  CCPA video doc additionally says a one time `AdvancedFeatures=1` config unlock is needed on stock units,
  while `usb_protocol.md:1556-1595` says it is not needed when `naviScreenInfo` is present. [UNCERTAIN, docs disagree]
* Handshake with Command 508: host sends 508 about 2 s after Plugged, dongle replies 508, host echoes
  508, then 0x2C flows. Wired: dongle initiates 508; wireless: dongle never initiates, the host must send
  it. Always echo any received 508; repeats are harmless. 509 stops the stream (VP:183-205,758-762;
  CCPA `command_details.md:517-522`). LIVI8 sends `requestClusterStreamFocus` (508) for this
  (`dongleDriver.ts:473-475`).
* Low frame rate when static (one frame every 4 to 6 s), IDR about every 26 s: keep the decoder alive,
  hide instead of destroy (VP:163-199).

---

## 6. Audio

Short names: AP = CCPA `documentation/02_Protocol_Reference/audio_protocol.md`, AF =
`03_Audio_Processing/audio_formats.md`, MP = `03_Audio_Processing/microphone_processing.md`.

### 6.1 AudioData (0x07) payload

| Off | Type | Field |
|-----|------|-------|
| 0 | u32 | decodeType (format, 6.2) |
| 4 | f32 | volume |
| 8 | u32 | audioType (stream, 6.4) |
| 12 | ... | depends on payload length |

Classify by **payload length** (NCP `readable.ts:211-233`; PYC `protocol.py:294-304`; LIVI8
`decode.ts:41-60`; AP:40,139,179):

| Payload length | Meaning |
|----------------|---------|
| 13 | audio command: 1 byte at offset 12 (6.3) |
| 16 | volume ramp: f32 `volumeDuration` at offset 12 (seconds) plus `volume` at offset 4 = target level. PYC reads it as u32 (bug), NCP/LIVI8 as f32. Seen on wireless CarPlay e.g. `vol=1.00 dur=0.5` after nav prompts (AP:168,224) |
| 12 | no data (LIVI8 handles it) |
| other | PCM, signed 16 bit little endian, interleaved, from offset 12, format per decodeType |

Note NCP builds `new Int16Array(data.buffer, 12)` which breaks if the Buffer has a non zero byteOffset;
LIVI8 fixes this (`decode.ts:52-56`). In Dart use `buffer.asInt16List(offsetInBytes + 12, n)` only if
aligned, otherwise copy.

### 6.2 decodeType

| dt | Rate | Ch | Bits | Usage | Source |
|----|------|----|------|-------|--------|
| 1 | 44100 | 2 | 16 | media 44.1k, not seen on the wire. **CLK and CLR map dt 1 to 48000/2** [UNCERTAIN, never observed anyway] | NCP `readable.ts:159-166`; AF:26; CLK `audio/AudioFormats.kt:60-77` |
| 2 | 44100 | 2 | 16 | media when `mediaSound=0`; also used on stop/command packets (CLK maps it to 48000/2, wrong for PCM per CCPA/CLR) | AF:27; AP:53 |
| 3 | 8000 | 1 | 16 | Android Auto phone call (HFP narrowband) audio and mic | AF:28; AP:57-63 |
| 4 | 48000 | 2 | 16 | default media, navigation, alerts (CarPlay and AA) | AF:29; AP:54 |
| 5 | 16000 | 1 | 16 | Siri / Assistant, CarPlay calls, mic, AA navigation TTS | AF:30; AP:55,195 |
| 6 | 24000 | 1 | 16 | voice, not seen on the wire | AF:31 |
| 7 | 16000 | 2 | 16 | stereo voice, not seen on the wire | AF:32 |
| 8 | 48000 | 1 | 16 | LIVI8 only [UNCERTAIN] | LIVI8 `shared/types/AudioDecode.ts:63-69` |

* The decodeType inside a **command** packet is not a reliable format indicator (NAVI_START may carry
  dt 2 while its PCM is dt 4) (AP:166). Take the format from the data packets.
* `mediaSound` in BoxSettings: 0 = 44.1 kHz media, 1 = 48 kHz (default) (AF:194).
* Hosts key their audio sinks by (decodeType, audioType) (NCP example `worker/utils.ts:4-7`, AutoKit
  pre-creates 8 tracks, AP:710-723).

### 6.3 Audio commands (1 byte at offset 12, payload length 13)

Corrected numbering verified by CCPA 2026-04 (AP:141-167), agrees with NCP/LIVI8 for 1..13:

| Id | NCP / LIVI8 name | CCPA name | Typical dt / audioType | Host action |
|----|------------------|-----------|------------------------|-------------|
| 1 | AudioOutputStart | OUTPUT_START | 4 or 5 / 1 or 2 | open / prepare the sink for that stream |
| 2 | AudioOutputStop | OUTPUT_STOP | | may close sink (during calls start/stop pairs repeat every 3 to 10 s, do not tear down expensively) |
| 3 | AudioInputConfig | INPUT_CONFIG | 5 (3 for AA calls) / 1 | **mic format = this packet's decodeType**; restart mic at new rate if running |
| 4 | AudioPhonecallStart | PHONECALL_START | 5 / 1 | call active: start mic, route call audio |
| 5 | AudioPhonecallStop | PHONECALL_STOP | | stop mic (may repeat 2 to 3 times about 170 ms apart) |
| 6 | AudioNaviStart | NAVI_START | 2 or 4 / 2 | duck media |
| 7 | AudioNaviStop | NAVI_STOP | 4 / 2 | unduck |
| 8 | AudioSiriStart / AudioVoiceAssistantStart | SIRI_START | 5 / 1 | start mic |
| 9 | AudioSiriStop / AudioVoiceAssistantStop | SIRI_STOP | 5 / 1 | stop mic **unless a call is active** (call can start 130 ms before SIRI_STOP) |
| 10 | AudioMediaStart | MEDIA_START | 4 / 1 | state |
| 11 | AudioMediaStop | MEDIA_STOP | | state |
| 12 | AudioAlertStart / AudioAttentionStart | ALERT_START | 4 / 1 | ringtone / alert: duck or route |
| 13 | AudioAlertStop / AudioAttentionStop | ALERT_STOP | | restore |
| 14 | AudioAttentionRinging (LIVI8) | INCOMING_CALL_INIT (`PHONECALL_Incoming`) | 5 / 1 | incoming call ringing |
| 15 | AudioTurnByTurnStart (LIVI8) | TBT_START | 4 / 2 | turn by turn pre announcement, precedes 6 |
| 16 | AudioTurnByTurnStop (LIVI8) | NAVI_COMPLETE | 4 / 2 | nav sequence finished, restore |

Sources: NCP `readable.ts:3-17`; PYC `protocol.py:266-277` (1..11); LIVI8 `ProjectionEnums.ts:102-119`.
Older CCPA tables inside the same doc used numbering shifted by one (AP:356-366,697-700): ignore those.

### 6.4 audioType (stream)

| Value | Stream | Direction |
|-------|--------|-----------|
| 1 | main: media, alerts, ringtone, Siri speech, call audio | IN |
| 2 | navigation: prompts, also iMessage / notification sounds (the ducking stream) | IN |
| 3 | microphone | OUT |

(AP:127-129). audioType 1 and 2 can play **simultaneously**: mix them, with media ducked while 2 plays
(AP:247-262). NCP example and LIVI8 also key sinks by audioType.

### 6.5 Ducking

* CarPlay sends either NAVI_START/STOP (6/7) commands or explicit volume packets: `volume` 0.1 to 0.2 =
  duck, 1.0 = restore, often as a 16 byte ramp packet with duration (AP:387-393,339-350). On PCM data
  packets the volume field is 0.0 (unused) (VP:203).
* **Android Auto sends no ducking packets**: the host ducks itself based on audioType 2 activity or
  commands 504/505/506/507 (AP:202,395,680). AutoKit ducks to 0.8 for AA, 0.2 otherwise (AP:704-708).
* CLK: MEDIA_START (10) means un-duck to 1.0, because the "adapter does not send an explicit volume=1.0
  restore packet"; a Siri initiated call sends PHONECALL_START about 130 ms before SIRI_STOP, keep the mic
  open; INCOMING_CALL_INIT (14) has no matching stop; nav warmup noise (mixed 0xFFFF/0x0000) for 200 to
  400 ms after nav start (CLK `CarlinkManager.kt:2540-2625`, `audio/DualStreamAudioManager.kt:225,321-336`).
  CLF drops fully zero filled PCM packets.
* LIVI8 algorithm (`dongleDriver.ts:522-545`): nav active (cmd 6 or 15) -> media level 0.2; voice active
  (cmd 8 or 4) -> media level 0; else 1.0; ramp 500 ms down, 1500 ms up.
* NCP example: on NaviStart set nav player volume to default; on volumeDuration packets call
  `player.volume(volume, volumeDuration)` (NCP `examples/carplay-web-app/src/useCarplayAudio.ts:50-66`).
* FCP: separate output per audioType (1 main, 2 nav), while nav plays main fades to 0.3, fades back after
  10 all zero nav segments (FCP `src/pcm_audio.cpp:116-194`, `src/settings.h:51-55`). CXX: one lane per
  (decodeType, audioType), all lanes mixed and resampled into one fixed 44100 Hz stereo output (avoids
  reconfiguring the device and clicks), duck = gain ramp to `volume` over `volumeDuration`, lanes dropped
  after 3 s idle (CXX `src/audio_alsa.cpp:100-279`, `src/dongle.cpp:218-236`). The CXX "single mixer"
  design maps well onto a Flutter plugin with one native audio output.
* The nav stream ends with a run of `0xFFFF` samples about 60 ms before NAVI_STOP (AP:399-413):
  harmless but can click; consider fading.

### 6.6 Packet sizes (payload length, CarPlay)

| Stream | Payload | PCM bytes | Duration |
|--------|---------|-----------|----------|
| media dt 4 (48k stereo) | 11532 | 11520 | 60 ms |
| voice dt 5 (16k mono) | 972 | 960 | 30 ms |
| AA media dt 4 | 15372 | 15360 | 80 ms |
| AA nav dt 5 | 1932 | 1920 | 60 ms |

(AP:180-196; the AA sizes in the doc include the 16 byte header, corrected here.) Media arrives at about
16 to 17 packets per second. Use a jitter buffer of a few packets (CCPA suggests 100 to 200 ms total;
`mediaDelay` in BoxSettings also sets the dongle side buffer).

### 6.7 Microphone upload (OUT AudioData)

| Field | Value | Source |
|-------|-------|--------|
| type | 0x07 | |
| decodeType | **the decodeType of the last INPUT_CONFIG (cmd 3)**: 5 (16 kHz) for Siri / Assistant / CarPlay calls, 3 (8 kHz) for Android Auto phone calls | MP:268-276; AF:411; AP:368 |
| volume | 0.0 (CLR sends 1.0, apparently also works) | NCP `sendable.ts:136-152`; MP:378; CLR `source/ccpa.rs:375-379` |
| audioType | 3 | NCP `sendable.ts:144`; LIVI8 `sendables.ts:478-488`; MP:379 |
| PCM | s16le mono, 8000 or 16000 Hz only (other rates break the dongle's WebRTC AECM) | AF:109-126; MP:250 |
| Chunk | 20 ms: 640 bytes at 16 kHz, 320 bytes at 8 kHz (CCPA hosts and AutoKit) | MP:276,388,532 |

* NCP hardcodes decodeType 5; NCP web mic captures at 16 kHz in 2048 sample (4096 byte, 128 ms) chunks
  (`src/web/recorder.worklet.ts:2`, `WebMicrophone.ts:5`). LIVI8 passes decodeType through. FCP sends
  1280 samples = 2560 B (80 ms) per message (FCP `src/recorder.cpp:29-32`); CXX 320 samples = 640 B (20 ms)
  (CXX `src/audio_alsa.cpp:303,325-327`). NCP, FCP, CXX and CLF always send decodeType 5, volume 0, audioType 3;
  CLK uses the INPUT_CONFIG decodeType (5, or 3 for AA calls) (CLK `CarlinkManager.kt:2521-2536,2627-2636,2731`). So chunk
  size is not critical; 20 ms is what the CCPA documentation recommends.
* Mic start triggers differ: FCP starts on Command 1 / stops on Command 2 (`connection.cpp:605-617`); CXX
  starts on AudioData cmd 3, 4 or 8 and stops on 5 or 9 (`main_kms.cpp:594-611`); RCP on cmd 4/8, stops 5/9.
* Sending dt 5 during an AA call routes the mic to the assistant path and the caller hears silence (MP:260-266).
* The dongle runs AGC, AEC and NS on the uploaded mic: send raw, unprocessed mic audio
  (AF:90-93,398-403). Requires Command 7 (UseCarMic) at init, the A15W has no onboard mic (MP:14,26,380).

When to run the mic (both signal sources arrive, handle both idempotently):

| Start | Stop | Source |
|-------|------|--------|
| Command 1 (StartRecordMic) from dongle | Command 2 (StopRecordMic) | NCP web example `App.tsx:110-125`; CCPA `command_ids.md:206-207` |
| AudioData cmd 8 (Siri start) or 4 (call start) | cmd 9 / 5 | NCP `CarplayNode.ts:74-86`; AP |

Typical CarPlay Siri: SIRI_START, INPUT_CONFIG(dt 5), Command 1, OUTPUT_START, ..., Command 2, SIRI_STOP,
OUTPUT_STOP. CarPlay call: PHONECALL_START, INPUT_CONFIG (about 425 ms later), Command 1, ..., Command 2,
PHONECALL_STOP. AA call: INCOMING_CALL_INIT(14), ALERT_START/STOP, PHONECALL_START, INPUT_CONFIG(dt 3).
(AP:267-337; MP:280-303,418-445.) NCP also auto stops its mic after 10 s (`NodeMicrophone.ts:28-36`); CCPA
documents no dongle side mic timeout.

---

## 7. Touch input

### 7.1 Touch (0x05, OUT, 16 bytes), single pointer

| Off | Type | Field | Values |
|-----|------|-------|--------|
| 0 | u32 | action | 14 = down, 15 = move, 16 = up |
| 4 | u32 | x | 0..10000, normalized to the **video frame** width (x_norm * 10000) |
| 8 | u32 | y | 0..10000 |
| 12 | u32 | flags | 0 |

Sources: NCP `sendable.ts:47-81` (`clamp(10000 * x, 0, 10000)`); PYC `protocol.py:306-326` ("X/Y are
scaled from 0 to 10000 regardless of device resolution"); LIVI8 `sendables.ts:455-465`; CCPA capture
"Format B" `usb_protocol.md:1191-1223` (values like X=3763, Y=6021). CCPA's "Format A" (actions 0..5,
float coordinates) is a legacy/undocumented guess, ignore it. NCP passes a float into `writeUInt32LE`;
round to an integer yourself.

### 7.2 MultiTouch (0x17, OUT), n x 16 bytes, no count field

| Off | Type | Field | Values |
|-----|------|-------|--------|
| 0 | f32 | x | 0.0..1.0 |
| 4 | f32 | y | 0.0..1.0 |
| 8 | u32 | action | 0 = up, 1 = down, 2 = move |
| 12 | u32 | id | pointer slot 0..4 |

Sources: NCP `sendable.ts:83-134`; PYC `protocol.py:328-350`; LIVI8 `sendables.ts:466-477`; CCPA
`usb_protocol.md:1811-1826` (max 5 points, count = payload length / 16).

* Note the field order differs from single Touch: coordinates first, action third.
* **Conflict on action values:** FCP `docs/multitouch.md:21-25,127,141` (notes from the decompiled vendor
  Android app) says state 1 = down **and** move, 0 = up, pointer id < 5, coordinates clamped to [0,1].
  NCP, PYC, LIVI8 and CCPA (which cites the vendor apps PhoneMirrorBox and AutoKit plus its own working
  host) use 0 up, 1 down, 2 move. Use 0/1/2. [UNCERTAIN, minor]
* No C++ implementation actually sends 0x17 (FCP README says multitouch untested); LIVI8 does.
* CLK (CCPA specific, field tested): **CarPlay uses MultiTouch 0x17**, Android Auto uses single Touch
  0x05; every active pointer is sent on each event, UP entries removed after sending, move deadband 0.003;
  AA MOVE rate limited to one per 17 ms; AA coordinates normalized to the oversized AA surface minus the
  crop offset (CLK `CarlinkManager.kt:197-221,1437-1473`, `ui/MainScreen.kt:347-431`). CLR uses 0x05 for
  everything. So: 0x17 for CarPlay, 0x05 for AA is the safest combination.
* LIVI8 sends **every active pointer in every frame** (full state), the changed pointer with action
  down/up and all others with move, and coalesces moves to one message per display frame; mouse input
  uses single Touch (0x05) instead (LIVI8 `renderer/.../hooks/useProjectionTouch.ts:107-216`). Pointer ids
  are small reusable slots, not OS pointer ids.
* Coordinates are relative to the **video frame** (Open width/height). If you letterbox or crop the
  video in Flutter, map touch into stream space first (LIVI8 `useProjectionTouch.ts:26-77`). For AA,
  coordinates are relative to the AA tier frame including its letterbox.
* Which to use: MultiTouch works for CarPlay (pinch zoom in Maps); for Android Auto both are used by
  implementations. [UNCERTAIN whether AA via the dongle accepts 0x17; LIVI8 sends 0x17 for touch screens
  regardless of phone type.]

---

## 8. Firmware facts, quirks and pitfalls (CPC200-CCPA specific where noted)

### 8.1 Firmware basics

* Hardware: NXP i.MX6UL(L) Cortex-A7, 128 MB DDR3L, 16 MB SPI NOR, jffs2 rootfs, Linux 3.14.52; Wi-Fi/BT
  chips vary (RTL8822BS/CS, IW416, BCM4354, SD8987) (LVR `README.md:8-20`; CCPA `hardware/cpc200-ccpa_spec.md:3-16`).
* The host protocol is implemented by the dongle daemon `ARMadb-driver` on `/dev/usb_accessory`; CarPlay by
  `AppleCarPlay` + `ARMiPhoneIAP2`, Android Auto by `ARMAndroidAuto` (a modified OpenAuto/aasdk build). The
  dongle is a pure tunnel: it forwards the phone's H.264 and PCM untouched, no buffering, pacing or
  decoder recovery (CCPA `documentation/README.md:20-60`).
* Persistent config: JSON `/etc/riddle.conf` (copied from `/etc/riddle_default.conf` if missing), CLI
  `riddleBoxCfg`; web UI (boa) at 192.168.50.2 (2024.09+) or 192.168.43.1 (older) when joined to its Wi-Fi
  (LVR `Updater-App.md:3-60`; CCPA `version_comparison.md:169-174`).
* Version string `YYYY.MM.DD.HHMM` plus suffix (e.g. `CAY`) in SoftwareVersion (0xCC); `/etc/software_version`
  without suffix. `serialNumber` style strings like `2024.01.19.1541f867` are not the firmware version
  (CCPA `version_comparison.md:11-21,205-207`).
* Known A15W versions: 2022.04.25.1317, 2022.06.02.1044, 2022.11.19.1218, 2023.05.29.1924, 2023.09.27.1710,
  2024.01.19.1541, 2024.08.07.2014, 2024.09.03.1028, 2025.02.25.1521, 2025.10.15.1127, 2025.12.29.1746.
  CPC200-Autokit (`Auto_Box`): 2020.10.27.1239 .. 2022.12.08.1636.

### 8.2 Quirks table

| # | Quirk | Impact / workaround | Source |
|---|-------|---------------------|--------|
| 1 | Heartbeat watchdog (15 s in binary, about 10 to 12 s in practice) | 2 s heartbeat from right after claim; never stop it while open | 3.8 |
| 2 | Dongle write timeout 4.5 s | always keep IN transfers pending, even while paused / backgrounded | CCPA `heartbeat_analysis.md:101,233` |
| 3 | Missing ZLP on 512 aligned writes > 16 KB (all observed firmware) | exact length reads or stream reader with 512 multiple buffers | 1.4 |
| 4 | USB reset re-enumerates (1 to 3 s gone, new address, udev permissions reapplied) | close handle, wait for re-arrival; CXX avoids reset on fresh plug | NCP `CarplayNode.ts:116-128`; CXX `src/usb_device.cpp:48-51` |
| 5 | Dongle cycles its gadget on its own when no host app talks to it, and re-enumerates on some phone unplug events; 20 to 30 s boot | treat hotplug arrival/removal as routine, reinitialize every time | CCPA `CPC200-CCPA_SSH_over_USB_NCM.md:31`; github.com/f-io/LIVI/issues/128; github.com/lvalen91/carlink/issues/8 |
| 6 | Host restart while the phone stays connected: firmware keeps the old session, ignores the new Open, no new Plugged, no SPS/IDR, black screen; a phone attached before init gets Phase 13 | on startup send DisconnectPhone (0x0F) + CloseDongle (0x15), wait 200 ms (CLK), then full init; request keyframe | CLK `CarlinkManager.kt:1051-1068`; CLR `source/ccpa.rs:121-126`; github.com/f-io/LIVI/issues/98; github.com/rhysmorgan134/node-CarPlay/issues/25 |
| 7 | Command 1010 (and all 1000..1013) are status only | never disconnect on them; old apps did and broke sessions | CCPA `usb_protocol.md:2278-2303` |
| 8 | `/etc/android_work_mode` reset to 0 on every disconnect | resend on every connection or AA never connects | CCPA `host_app_guide.md:683-707` |
| 9 | Wi-Fi band commands 24/25 only apply after dongle reboot; default AP is 5 GHz channel 36, `/etc/wifi_use_24G` file forces 2.4 GHz channel 6; channels outside 1..14 / 34..165 fall back to 36 | send `WiFiChannel` in BoxSettings; prefer 5 GHz for wireless CarPlay | CCPA `command_details.md:315-341`; LVR `script/start_bluetooth_wifi.sh:51-73` |
| 10 | `mediaDelay`: < 500 clamped to 500, and a "resonance" bug between 375 and 425 ms | use 500 to 1000 (LIVI8 1000, about 700 for wireless) | CCPA `configuration.md:218-228`; LIVI8 `dongleConfig.ts:5-7`; github.com/f-io/LIVI/issues/23 |
| 11 | BoxSettings values persist in riddle.conf and override what users set in the web UI | decide whether your app owns these settings; send them every start | github.com/lvalen91/carlink/issues/1 |
| 12 | `btName`, `wifiName`, `oemIconLabel`, `brand` are passed unescaped to shell commands on the dongle | sanitize to `[A-Za-z0-9 _-]`, max 15/16 chars | CCPA `05_Security_Analysis/security_findings.md:36,61,72-73` |
| 13 | `syncTime` gets +8 h (UTC+8) added by the firmware | cosmetic (dongle logs), send Unix seconds | CCPA `usb_protocol.md:678` |
| 14 | `callQuality` and `autoPlay` do nothing on 2025.10 | do not rely on them; to resume music after reconnect send cmd 201 (play) yourself | CCPA `configuration.md:1519`; `host_app_guide.md:890-938` |
| 15 | CarPlay sends very few IDRs; AA must not receive `frame` | see 5.3 | VP |
| 16 | First CarPlay frames may be 800x480 then switch size; AA first IDR is a dark frame | decoder must handle mid stream SPS change; optionally skip first 4 AA frames | VP:447,686-707 |
| 17 | view/safe area larger than Open size causes Phase 13 loop | validate geometry | 5.4 |
| 18 | CCPA 2022.11.19.1218: Android phones not recognized at all | ask users to update firmware | github.com/rhysmorgan134/node-CarPlay/issues/11 |
| 19 | A CPC200-Autokit firmware in Aug 2023 broke older node-CarPlay versions (LIBUSB_TRANSFER_ERROR); fixed by newer host code or downgrade to 2022.12.08.1636, root cause unpublished | keep the transport tolerant (stream reader, retries) | forums.moderndaymods.com/t/carlinkit-is-not-connecting/87; github.com/rhysmorgan134/react-carplay/issues/42 |
| 20 | Optional USB encryption (0xF0, since about 2024). FCP warns it "can happen that it will become mandatory" | current firmware does not require it; FCP has a working implementation (AES-128-CFB, key rotation of "SkBRDy3gmrw1ieH0", IV from seed) if ever needed | FCP `settings.txt:40-44`, `src/protocol/aes_cipher.cpp:22-68`; CCPA `usb_protocol.md:1682-1809` |
| 21 | Phone "Use This Car" first pairing can take 75 s+ | no aggressive timeouts before Plugged | CCPA `inbound_session_sequence.md:164-171` |
| 22 | `wifiPair` (1012) timer after 15 s can abort an in-progress reconnect of a known phone | cancel it on Plugged (and maybe on cmd 1003/1004 activity) | github.com/f-io/LIVI/issues/326 |
| 23 | Album art may stop arriving after some firmware updates | treat metadata as optional | github.com/lvalen91/carlink/issues/4 |
| 24 | Disabling the gadget with OUT transfers pending wedges the dongle until power cycle (only relevant with root on the dongle) | n/a for the plugin; but cancel your OUT transfers before close | CCPA `hardware_platform.md:252` |
| 25 | Power: weak car/Pi ports and bad cables cause drops; `/tmp/charge_mode` 1 = fast charge draws more | recommend powered hub / good USB 2.0 cable; FCP defaults to slow charge for stability | github.com/lvalen91/carlink/issues/8; FCP `settings.txt:49-52` |
| 26 | Android Auto via the dongle: works but is second class (LIVI maintainer advises against it); switching between iPhone and Android needs phoneWorkMode flip and the dongle re-enumerates | implement LIVI8's MDLinkType based flip (4.4) | github.com/f-io/LIVI/issues/128, issues/285 |
| 27 | GPS (`GNSSCapability`) defaults to 0, and changing it may need a dongle reboot and datastore reset | GPS forwarding is a later feature | CCPA `configuration.md:406-453` |
| 28 | On Linux desktops the dongle's optional mass storage "APK" volume auto mounts | unmount / ignore | CXX `README.md:205-206` |
| 29 | Phase 13 handling differs: CLK shows "Phone rejected configuration" and does not restart; CLR sends 0x0F + 0x15 and re-inits ("on the retry the phone re-attaches cleanly") | fix geometry, then CLR style teardown + re-init | CLK `CarlinkManager.kt:2335-2341`; CLR `source/ccpa.rs:282-293` |
| 30 | CLF treats cmd 1010 as "projectionDisconnected" and restarts: this is the bug the CCPA docs warn about | ignore 1010 | CLF `lib/common.dart:51-53`, `lib/carlink.dart:860-862` |
| 31 | `ForgetBluetoothAddr` (0x22) does not clear the firmware's LastConnectedDevice | auto connect may still pick the old phone | CLK `documents/revisions.txt:288` |
| 32 | Some frames declare a header length larger than the bytes actually delivered | clamp / drop instead of crashing | CLK `protocol/MessageParser.kt:172-207` |
| 33 | Inbound 0xCD: CLK labels it "heartbeat echo every 2 s" while CCPA says the dongle never echoes heartbeats (2025.10) and 0xCD is the OUT reboot command | ignore inbound 0xCD; never send 0xCD unless you want a reboot | CLK `protocol/MessageTypes.kt:287-308`; CCPA `heartbeat_analysis.md:90-92` |
| 34 | `NaviStatus` arrives as a float (e.g. 1.0000002); CallStatus numbering: CLK 3 connected / 4 disconnecting vs CCPA corrected 3 connecting / 4 active | compare with rounding; prefer the CCPA mapping | CLK `navigation/NavigationStateManager.kt:349-395`, `CarlinkManager.kt:2929-2944`; CCPA `usb_protocol.md:326` |
| 35 | Sending a message less than about 500 ms apart: CCPA docs mention a "minimum message spacing 500 ms" constant at 0x18e82 [UNCERTAIN, all hosts send faster without issue] | spacing of 120 ms is proven | CLF `Documents/Firmware/RE_Documention/01_Firmware_Architecture/heartbeat_analysis.md` |

### 8.3 CCPA vs other "cpc200" models

* CCPA (`A15W`) and CPC200-Autokit (`Auto_Box`) speak the same 0x55AA55AA protocol; CCPA is the newer
  lineage (HiCar, Android mirror modules, mic codec modules from 2024.09, newer firmware cadence). Default
  PID is 0x1521 on CCPA and 0x1520 on Auto_Box, but either can appear on either.
* Update image names must match `/etc/box_product_type` (`A15W_Update.img`, `Auto_Box_Update.img`).
* Wired only models (CCPM, Auto-CA) have no Wi-Fi/BT: wireless commands are harmless no ops.
* Converters (U2W/U2AW/U2AC/CP2A) are not head unit dongles at all.

---

## 9. License summary (can I port?)

| Source | License | Port code into an MIT/BSD/Apache plugin? |
|--------|---------|------------------------------------------|
| NCP node-CarPlay | MIT | Yes, keep copyright + license text |
| PYC pycarplay | MIT | Yes, keep notice |
| RCP round-carplay | MIT | Yes, keep notice |
| CLF carlink_flutter (Dart, closest to your stack) | MIT | Yes, keep notice |
| CLK carlink (Kotlin), CLR carlink_linux (Rust), lvalen91/ocbm | Unlicense (public domain) | Yes, no conditions |
| CXX carlinkit-cxx | Apache-2.0 | Yes, keep license and NOTICE, state changes |
| CCPA CPC200-CCPA_resources docs | MIT | Yes (docs; facts are free anyway) |
| LIVI (all versions) | GPL-3.0-or-later + 7(b) attribution term | No (unless your plugin is GPL-3.0 and shows "Based on LIVI by Lasse Heitgres" in About and README). Reading it for protocol facts is fine |
| FCP FastCarPlay | GPL-3.0 | No (documentation only) |
| LVR ludwig-v, V2K Veyron2K | GPL-3.0 | Docs only; they mostly contain firmware dumps, which are Carlinkit's copyright anyway |
| MAH mahnat/ccpa-200-re | no license | Read only |

Protocol facts (ids, offsets, sequences) are not copyrightable, so a clean room implementation from this
document is fine regardless of the source licenses.

---

## 10. Minimal implementation checklist

1. Match 0x1314:0x1520/0x1521; claim interface with class 0xFF and two bulk endpoints (normally 0); discover endpoints;
   send 0x0F + 0x15 and wait 200 ms to clear a stale session.
2. Reader: keep IN transfers pending (multiples of 512), stream parse 16 byte headers, validate magic and
   `type ^ 0xFFFFFFFF`, cap length, resync by scanning for magic.
3. Writer: single serialized queue; header + payload.
4. Heartbeat 0xAA every 2 s from claim to close.
5. Init (4.2). Handle IN Open echo, 0xCC version, 0x19 JSON, 0x0D/0x0E names.
6. Plugged -> Phase 7 -> Phase 8 -> video. Unplugged or Phase 0 -> teardown, keep USB.
7. Video: strip 20 byte header, Annex B H.264 to decoder; handle mid stream SPS / size changes; request keyframe
   (cmd 12) on decoder (re)start for CarPlay (and at most once at AA stream start, never periodically for AA).
8. Audio: per (decodeType, audioType) PCM s16le; mix main (1) and nav (2); duck on cmd 6/7 or volume packets; commands by payload length 13.
9. Mic: on cmd 1 or audio cmd 8/4 start, on cmd 2 or audio cmd 9/5 stop; upload s16le mono at the INPUT_CONFIG rate (16 kHz, 8 kHz for AA calls) in 20 ms chunks with decodeType from INPUT_CONFIG, volume 0, audioType 3.
10. Touch: 0x17 (floats 0..1, all active pointers each event) for CarPlay; 0x05 (ints 0..10000) for Android Auto and mouse.
11. Metadata: 0x2A subtype 1 JSON (strip trailing NUL), 3 JPEG, 200 nav JSON (merge partial updates).
12. Ignore 1000..1013, 0x25, 0x26, 0x77, 0xA3, 0x0B unless you want them.
