# Native CarPlay backend: design

The plan for milestone M13: a second backend beside `src/dongle/` in which this plugin is the
CarPlay receiver itself, the way LIVI has been since July 2026. iAP2 and AirPlay run on the host,
over the iPhone's own USB cable or over the host's Wi-Fi and Bluetooth, and the only thing that
comes from extra hardware is the MFi authentication signature.

Nothing here is written yet. This file is the design a later session implements milestone by
milestone (section 7), and it is meant to be enough to start without rereading LIVI. It was
written on 2026-11-05 from a read of LIVI at commit `7fa9f8020d6422c66cdae92c3ab019a06e91b3a2`
(version 10.4.0, 2026-11-05), no code from it was run. Paths starting with `native/`, `scripts/`,
`assets/` or `android/` are in that LIVI tree (github.com/f-io/LIVI), paths starting with
`packages/` are in this repository. **[unconfirmed]** marks a claim that is my reading or
general knowledge rather than something LIVI's code or documentation states; each one is a thing
to check on hardware.

## Contents

1. [The shape in one picture](#1-the-shape-in-one-picture)
2. [How LIVI does it](#2-how-livi-does-it)
3. [Libraries for each piece, and the C++ equivalents](#3-libraries-for-each-piece-and-the-c-equivalents)
4. [The MFi signer](#4-the-mfi-signer)
5. [Port to C++ or link the Rust: the recommendation](#5-port-to-c-or-link-the-rust-the-recommendation)
6. [The backend inside this plugin](#6-the-backend-inside-this-plugin)
7. [Milestones](#7-milestones)
8. [Risks and open questions](#8-risks-and-open-questions)

---

## 1. The shape in one picture

```
 iPhone                                         this plugin (native backend)
 ──────                                         ────────────────────────────
 USB:  usbmux iface 1 ─ lockdown ─ com.apple.carkit.service ┐
 BT:   RFCOMM ch 3, UUID ...deca-deafdecacaff ──────────────┼─> iAP2 link layer (FF 5A packets)
 AirPlay: iAP data stream / iAPSendMessage (later) ─────────┘      │
                                                                   ▼
                                       iAP2 control session (40 40 messages)
                                         identification, authentication ──> MfiSigner
                                         now playing, route guidance, calls ──> metadata hook
                                         CarPlayAvailability -> CarPlayStartSession
                                             (our IPv6 link local address and TCP port)
                                                                   │
 USB:  CDC-NCM function in config 6 ─┐                             ▼
 Wi-Fi: the head unit's access point ┴─> IPv6 link local ─> AirPlay receiver, phone connects
                                         RTSP-like control, binary plists
                                         pair-setup (SRP), pair-verify (X25519, Ed25519)
                                         auth-setup or MFi 4.0 mutual auth ──> MfiSigner
                                         ChaCha20-Poly1305 everywhere after pair-verify
                                           screen stream (TCP) ─> avcC to Annex B ─> VideoDecoder
                                           audio (RTP over UDP) ─> PCM ─> AudioOutput
                                           microphone (RTP out) <─ AudioInput
                                           event channel: HID reports, commands, forceKeyFrame
```

Everything right of the AirPlay receiver is what `cp_core.cc` already owns for the adapter
backend: the decoder, the three audio streams, the microphone and the hooks. A phone through the
native backend and a phone through a Carlinkit adapter look the same from Dart.

---

## 2. How LIVI does it

### 2.1 Processes, and why they do not carry over

LIVI is an application, not a library, and is split into processes by privilege:

- `livi-core` (unprivileged) owns the UI, the AirPlay receiver (`livi-cp`) and the media. It
  binds the AirPlay listener (`native/crates/livi-core/src/services.rs:147-149`).
- `livi-helperd` runs **as root** through `sudo -n -E` and a sudoers grant
  (`native/crates/livi-core/src/helper.rs:214-219`,
  `assets/linux/99-LIVI-helper.sudoers.template`). It owns USB, the iAP2 sessions, the MFi chip,
  Bluetooth and the Wi-Fi access point, and talks to `livi-core` over a Unix socket with a line
  protocol (`native/crates/livi-runtime/src/livi_sock.rs:82-300`: `subscribe`, `tunnel`,
  `certificate`, `sign`, `start-wired`, `reset-wired` and others). Even the MFi signature crosses
  that socket: `livi-cp` asks for it with `sign <base64 digest>`
  (`native/crates/livi-cp/src/helper_sock.rs:62-92`).
- `livi-gst-host` decodes and plays through GStreamer.

A Flutter plugin lives inside the host app's process and cannot ask for root. So the split is
not reused; what is reused is the protocol knowledge. Section 6.4 lists what needs privilege on
Linux and how to get it without a root process.

### 2.2 The crates that matter

Line counts are non-test lines (code before the first `#[cfg(test)]`), counted by hand.

| Crate | Lines | What it is | Carries over |
|---|---|---|---|
| `iap2-link` | 724 | the iAP2 link layer as a sans-IO engine: detect marker, SYN negotiation, sequence numbers, acks, retransmission, session demux. No dependencies | yes, all of it |
| `iap2-csm` | 1227 | the control session codec: `40 40` messages, parameters, every message LIVI sends or receives. No dependencies | yes, all of it |
| `iap2-mfi` | 759 | the signer trait and four implementations: I2C chip, files, network client, none; plus the dongle side server | the trait, I2C, network client, files |
| `iap2-usbmux` | 2424 | finding iPhones, the CarPlay USB configuration, an in-process usbmux with a minimal TCP, CDC-NCM in userspace (NTB16, an Ethernet shim with NDP for Android) | yes, the Android parts later |
| `iap2-wired` | 168 (plus the `idevice` crate) | lockdown pairing and starting `com.apple.carkit.service` | yes, reimplemented on our own lockdown client |
| `livi-runtime` | 2319 core, 2506 wireless | the iAP2 bring-up state machine (`bringup.rs`), the identification contents (`ident.rs`), iAP2 to JSON (`events.rs`), vehicle and location (`vehicle.rs`), file transfer for artwork, BlueZ (`bt.rs`), Bonjour (`bonjour.rs`), the access point (`wifi_ap.rs`), reconnect | the core yes, wireless partly |
| `livi-cp` | about 5500 core, 1231 MFi 4.0, about 8800 extras | the AirPlay receiver: RTSP, bplist, TLV8, SRP, pair-setup, pair-verify, auth-setup, MFi 4.0, `/info`, SETUP, the event channel, HID, timing, the iAP tunnel. The extras are CarPlay Ultra vehicle state and climate, uiSync, AirPlay video playback, HLS, the multi phone manager | core and MFi 4.0 yes, extras no |
| `livi-screen-stream`, `livi-audio` (`stream.rs`, `uplink.rs`), `livi-video/src/nal.rs` | 762 | decrypting the screen and audio streams, building the microphone packets, NAL classification. The rest of these crates is GStreamer | the protocol parts |
| `livi-crypto` | 117 | ChaCha20-Poly1305 for the media streams, through aws-lc-rs | replaced by OpenSSL |
| `livi-link-host` | 1828 | finding a LIVI Link dongle and using its Wi-Fi AP and Bluetooth (vhci) | the discovery, later |
| `livi-mdns` | 206 responder | a tiny IPv6 mDNS responder, used only on Android | for Android |
| `livi-bt-host` | about 3350 | LIVI's own userspace Bluetooth host, only for macOS and Android with a LIVI Link | no |
| `livid`, `livi-wifid`, `livi-btd`, `livi-accessoryd` | | the firmware daemons that run **on** a LIVI Link dongle | no, but they define the protocol in 4.5 |

So the wired path is roughly 15 thousand lines of Rust that matter (link and codec 2000, signer
800, USB and NCM 2400, bring-up 2300, AirPlay core and MFi 4.0 6700, media protocol 800, plus a
lockdown client that LIVI takes from a crate), and wireless adds about 2500.

### 2.3 Wired, step by step

The order is LIVI's (`native/crates/livi-helper/src/wired.rs:39-139`), with the protocol at each
step.

#### Step 1: find the iPhone and switch it to the CarPlay configuration

- LIVI polls every 2 s rather than using hotplug, and only while a signer is present
  (`wired.rs:33,67-69`). On Linux it scans `/sys/bus/usb/devices/*` for `idVendor == 05ac` with a
  serial, no product id filter (`native/crates/iap2-usbmux/src/linux.rs:31-66`).
- If the phone offers fewer than 6 configurations, LIVI sends a vendor control request
  (`native/crates/iap2-usbmux/src/usb_pipe.rs:77-93`): `bmRequestType 0xC0`, `bRequest 0x52`,
  `wValue 0`, `wIndex 4`, `wLength 1`. The phone re-enumerates; LIVI polls every 200 ms for up to
  5 s until there are at least 6 (`usb_pipe.rs:20-40`). **[unconfirmed]** usbmuxd's source calls
  0x52 `APPLE_VEND_SPECIFIC_SET_MODE` and mode 4 "USB Ethernet plus CDC-NCM".
- It then selects **configuration 6** (`CP_CONFIG`, `native/crates/iap2-usbmux/src/lib.rs:9`), by
  writing to sysfs `bConfigurationValue` on Linux (`linux.rs:83-86`, which is why it needs root)
  and with `set_configuration(6)` on Android. Configuration 4 is put back when the session ends
  or the helper exits (`lib.rs:11`, `native/crates/iap2-usbmux/src/device.rs:34-38`).
- **There is no USB role swap.** The head unit stays USB host and the phone stays device
  (`README.md`: "no OTG required"). Identification says so with a `USBHostTransportComponent`
  (step 4).

#### Step 2: usbmux, lockdown, and the carkit service

The iAP2 bytes do not come from a dedicated USB endpoint. They come from a lockdown service.

- **usbmux on interface 1**, bulk `EP_OUT 0x04`, `EP_IN 0x85` (`native/crates/iap2-usbmux/src/lib.rs:28-29`).
  LIVI implements usbmux itself (`native/crates/iap2-usbmux/src/mux.rs`): a version packet
  (protocol 0, major 2), a `SETUP` packet with the byte `0x07`, then 16 byte mux headers with
  magic `0xFEEDFACE` around a minimal TCP (SYN, ACK, FIN, RST, 16384 byte payloads, no
  retransmission because USB is reliable, `mux.rs:14-26,154-280`). Writes end with a zero length
  packet when they are a multiple of the packet size (`usb_pipe.rs:45-63`).
- **lockdown** on mux port 62078. If no pair record exists, LIVI pairs, which makes the phone
  show **"Trust This Computer?"** (`native/crates/iap2-wired/src/carkit.rs:84-131`). The record
  is `<UDID>.plist` in `/var/lib/lockdown` on Linux, the same place usbmuxd keeps them
  (`native/crates/livi-helperd/src/main.rs:33-43`, `carkit.rs:16-34`).
- **`StartService("com.apple.carkit.service")`** (`carkit.rs:14,133-156`) returns a port and
  whether TLS is wanted. LIVI connects to that port over the mux and, if asked, runs TLS with the
  pair record's certificates. The resulting byte stream is the iAP2 link.
- The lockdown protocol itself (plist requests with a 4 byte big endian length, `Pair`,
  `StartSession`, `StartService`) lives in the `idevice` crate 0.1.68, not in LIVI
  (`native/Cargo.lock:3371`). libimobiledevice implements the same protocol in C.
- **usbmuxd conflict [unconfirmed]**: LIVI claims interface 1 itself with no detach and 12 retries
  300 ms apart (`usb_pipe.rs:95-111`). On a desktop where the system usbmuxd has already claimed
  it, that claim fails, and LIVI has no code to resolve it. This machine has usbmuxd 1.1.1
  installed (inactive until a phone is plugged in, it is udev activated).

#### Step 3: the iAP2 link layer

From `native/crates/iap2-link/src/lib.rs`:

- The accessory first writes the detect marker `FF 55 02 00 EE 10` (`lib.rs:11`), resent every
  1000 ms while detecting.
- Packets: `FF 5A`, length u16 big endian (whole packet, payload plus 10 when there is a payload),
  control byte (`SYN 0x80`, `ACK 0x40`, `EAK 0x20`, `RST 0x10`; LIVI never uses `SLP 0x08`), seq,
  ack, session id, header checksum. The payload has its own one byte checksum. Both checksums are
  the two's complement of the byte sum (`lib.rs:13-29,39-75,446-461`).
- The SYN payload (link synchronization parameters, `lib.rs:84-135`): version 1, max outstanding,
  max packet length, retransmit timeout ms, cumulative ack timeout ms, max retransmissions, max
  cumulative acks, then three byte session entries `[id, type, version]`. LIVI offers control 10
  (type 0), external accessory 11 (type 2), file transfer 12 (type 1) (`lib.rs:7-9,222-250`).
- Over the cable LIVI is the **initiator** and runs with `zero_ack`: max outstanding 4, control
  version 2, and all four timer and retry fields 0, so packets count as acknowledged at once
  (`native/crates/livi-helper/src/wired.rs:284-290`, `lib.rs:517-522`). Over Bluetooth it is
  passive, waits for the phone's marker, uses 4000 ms retransmit, 500 ms ack timeout, 4
  retransmissions and 3 cumulative acks.
- The first SYN has seq 99; when the phone's SYN arrives LIVI adopts the phone's parameters
  wholesale and acks; any ACK in negotiation makes the link writable (`lib.rs:252,568-583`).
- Each control message goes out as exactly one session 10 packet, no fragmentation
  (`native/crates/livi-runtime/src/driver.rs:98-101`); inbound control bytes are reframed, since a
  message can span packets (`driver.rs:136-143`). File transfer (session 12) carries album art:
  `[ftid, ctrl, data]` with SETUP 0x04, FIRST 0x80, DATA 0x00, LAST 0x40, FIRST_AND_ONLY 0xC0
  (`native/crates/livi-runtime/src/file_transfer.rs:7-70`).

#### Step 4: control session messages and identification

A control message is `40 40 | u16 length | u16 message id | parameters`, a parameter
`u16 length (with its 4 byte header) | u16 id | data`, integers big endian, strings UTF-8 with a
trailing NUL, groups nested (`native/crates/iap2-csm/src/lib.rs:7,51-101,153-197`).

**LIVI identifies before it authenticates.** It waits for `StartIdentification 0x1D00`, sends
`IdentificationInformation 0x1D01`, and waits for `IdentificationAccepted 0x1D02`; any other
message in that phase is fatal (`native/crates/livi-runtime/src/bringup.rs:168-211`). On
`IdentificationRejected 0x1D03` it drops the optional location, vehicle information and vehicle
status components and tries again (`bringup.rs:184-230`). **[unconfirmed]** iAP2 is commonly
described as authenticating first; a port should accept either order.

What LIVI puts in `IdentificationInformation` (`native/crates/livi-runtime/src/ident.rs:69-213`):

| Param | Value |
|---|---|
| 0 to 5 | name (the car name, default "LIVI"), model "LIVI", manufacturer "LIVI", serial "0123456", firmware "1.0.0", hardware "1.0" |
| 6 sent, 7 received | message ids, all u16s concatenated into one parameter (`ident.rs:46-58`). Sent: A101, 5703, 5000, 5002, 5003, 5200, 5203, FFFB, AE00, AE02, 4157, 4159, 4154, 4156, 4301, and AE03 when wired. Received: EA00, EA01, A100, A102, 4E0D, 4E0E, 5702, 5001, 5201, 5202, 5204, FFFA, FFFC, AE01, 4158, 4155, 4E0B, 4300 |
| 8 power providing capability | 2 (advanced) wired, 0 wireless |
| 9 max current drawn from the device | 20 |
| 10 external accessory protocol | id 1, "dev.f-io.livi", match action 4 (no action) |
| 12, 13 | current language "en", supported languages "en\0de\0" packed into one parameter |
| 16 USB host transport (wired) | id 0, "USBHostTransport", supports iAP2, **CarPlay interface number 3** (env `LIVI_CP_USB_IFACE`), supports CarPlay |
| 17 Bluetooth transport (wireless) | id 0, "blue", supports iAP2, the adapter's MAC as 6 bytes |
| 20, 21, 22 | vehicle information, vehicle status, location information (GPGGA, GPRMC); droppable |
| 24 wireless CarPlay transport (wireless) | id 1, name the SSID, supports iAP2, supports CarPlay |
| 30 route guidance display | max lengths 128, max capacities 50 |

Two encoding quirks to copy byte for byte until proven unnecessary: a `list` parameter packs
every element into one parameter (`iap2-csm/src/lib.rs:292-353`), and transport components
repeat their id, name and supports-iAP2 fields in unused slots 3 to 5
(`native/crates/iap2-csm/src/messages/identification.rs:59-107`, golden vector in
`native/crates/iap2-csm/tests/vectors.txt:31`). No comment says whether iOS needs either.

#### Step 5: authentication

`native/crates/livi-runtime/src/bringup.rs:232-271`, message ids in
`native/crates/iap2-csm/src/messages/authentication.rs`:

```
phone  RequestAuthenticationCertificate        0xAA00
acc    AuthenticationCertificate               0xAA01  {0: certificate bytes from the signer}
phone  RequestAuthenticationChallengeResponse  0xAA02  {0: challenge}
acc    AuthenticationResponse                  0xAA03  {0: signer.sign(challenge)}
phone  AuthenticationSucceeded 0xAA05, or AuthenticationFailed 0xAA04
```

The challenge goes to the signer untouched. Section 4 has what the signer does with it.

#### Step 6: power, subscriptions, and the start

- Wired only: `PowerSourceUpdate 0xAE03 {available current 500 mA, should charge true}`
  (`bringup.rs:449-458`, `native/crates/livi-helper/src/lib.rs:27`).
- Subscriptions, in this order (`bringup.rs:100-162`): `StartNowPlayingUpdates 0x5000`,
  `StartRouteGuidanceUpdates 0x5200`, `StartPowerUpdates 0xAE00`,
  `StartCommunicationsUpdates 0x4157`, `StartCallStateUpdates 0x4154`.
- **The phone's `CarPlayAvailability 0x4300` is what starts CarPlay**, not Bonjour. LIVI answers
  with `CarPlayStartSession 0x4301` (`bringup.rs:312-356`,
  `native/crates/iap2-csm/src/messages/car_play.rs:41-90`):

  | Param | Wired | Wireless |
  |---|---|---|
  | 0 wired group | `{0: ip addresses}`, our fe80 address on the phone's NCM interface | |
  | 1 wireless group | | `{0 ssid, 1 passphrase, 2 channel, 3 ip addresses, 4 security}` (security omitted for WPA2, 3 WPA2/WPA3, 4 WPA3) |
  | 2 port | the AirPlay TCP port, u32 | same |
  | 3 device identifier | a MAC (configured, else the AP or interface MAC) | same |
  | 4 public key | **the pairing id**, a UUID string, in LIVI (`native/crates/livi-helper/src/lib.rs:56`) | same |
  | 5 source version | "950.7.1" (`native/crates/livi-helper/src/lib.rs:26`) | same |
  | 6, 7 | SDK version and theme asset, only with a car maker theme | same |
  | 8 supports mutual authentication | true only for a 3.0 signer (`bringup.rs:308-310`) | same |

  Wired waits up to 5 s for the NCM interface before answering (`bringup.rs:417-426`). After
  sending, livi-cp gives the phone 8 s to connect over AirPlay and otherwise asks the helper to
  reset the phone's USB once (`native/crates/livi-cp/src/manager.rs:24,599-628`).

#### Step 7: the network, CDC-NCM over the same cable

Configuration 6 exposes a CDC-NCM function next to usbmux (control class 0x02 subclass 0x0D,
data class 0x0A, `native/crates/iap2-usbmux/src/ncm/mod.rs:36-40`). **[unconfirmed]** Its control
interface is number 2 and its data interface 3, matching the "CarPlay interface number 3" in
identification, judging from a macOS ioreg fixture (`native/crates/iap2-wired/src/mac_network.rs:84-105`).

- LIVI drives NCM in userspace with nusb (`ncm/mod.rs:213-386`): claim both interfaces (detaching
  `cdc_ncm`), `GET_NTB_PARAMETERS`, `SET_CRC_MODE 0`, `SET_NTB_FORMAT 0` (NTB16),
  `SET_NTB_INPUT_SIZE 16384`, data interface alt setting 1, `SET_ETHERNET_PACKET_FILTER 0x000E`.
  Frames travel in NTB16 blocks (`NCMH`, `NCM0` signatures, one Ethernet frame per block outbound,
  a zero byte appended when the block is a multiple of 512, `native/crates/iap2-usbmux/src/ntb.rs`).
- **On Linux the frames go into a TAP device** `cpusbN` (`TUNSETIFF`, `IFF_TAP | IFF_NO_PI`),
  given the MAC from the NCM functional descriptor's `iMACAddress`, unmanaged in NetworkManager,
  `accept_dad 0`, up (`native/crates/iap2-usbmux/src/ncm/linux.rs:56-79,176-199`). The kernel
  then does IPv6, neighbour discovery and TCP. If that fails LIVI falls back to the **kernel's own
  `cdc_ncm` netdev** with a NetworkManager link local profile (`linux.rs:115-174`).
- The address is the kernel's fe80 link local, read back with `ip -6 addr show dev X scope link`
  (`native/crates/livi-runtime/src/net.rs:53-63`), and it goes into `CarPlayStartSession`.
- **On Android** there is no netdev to make, so LIVI opens a `VpnService` TUN (layer 3) with a
  random fe80 address and runs a small layer 2 shim that answers neighbour solicitations itself
  (`native/crates/iap2-usbmux/src/ether.rs`, `ncm/tunnel.rs`,
  `android/app/src/main/java/dev/fio/livi/LiviVpn.java`). The VPN needs the user's consent.
- Whether wired also needs Bonjour is not settled **[unconfirmed]**: the phone gets the address
  and port in `0x4301`, but LIVI on Android announces `_airplay._tcp` on the TUN anyway
  (`native/crates/livi-helper/src/wired.rs:387-414`), and on Linux avahi publishes on every
  interface.

#### Step 8: AirPlay control

**The phone is the client.** LIVI listens on a dual stack `[::]:0`, so the port is ephemeral and
only travels in `0x4301` and Bonjour (`native/crates/livi-cp/src/net.rs:11-23`). Every accepted
connection is one session.

- Framing is RTSP/1.0 style text with `CSeq` and `Content-Length`
  (`native/crates/livi-cp/src/rtsp.rs`), bodies `application/x-apple-binary-plist`,
  `application/pairing+tlv8` or `application/octet-stream`. The bplist codec is LIVI's own
  (`native/crates/livi-cp/src/bplist.rs`; integers are unsigned only, so negative numbers go as
  reals).
- Dispatch (`native/crates/livi-cp/src/stack/control.rs:72-229`): `GET /info`, `POST /pair-setup`,
  `POST /pair-verify`, `POST /auth-setup`, `SETUP`, `RECORD`, `TEARDOWN`, `POST /command`,
  `POST /feedback`. Everything else (OPTIONS, GET_PARAMETER, SET_PARAMETER, `/fp-setup`) gets an
  empty 200. **There is no FairPlay.**
- **`/info`** (`native/crates/livi-cp/src/info.rs:285-373`) answers unencrypted, with
  `sourceVersion`, `features 0x615653aee2`, `statusFlags 4`, model and manufacturer, `deviceID`,
  `bluetoothIDs`, `name`, `rightHandDrive`, `modes` (screen resource 1 and audio resource 2, both
  taken by the phone, app states for phone call, speech and turn by turn), `audioLatencies`,
  `audioFormats`, `extendedFeatures ["vocoderInfo", "enhancedRequestCarUI"]`, `displays` and
  `hidDevices`, optional OEM icons and `hevcInfo`. A display (`info.rs:227-272`) carries a uuid
  (`b7e6c5a0-1111-4000-8000-000000000001` main, `...2222...0002` cluster), type 110 or 111,
  `maxFPS`, `widthPixels`, `heightPixels`, physical size in mm (200 dpi assumed when unknown),
  `features 0x0A` (high fidelity touch and knob), appearance modes and optional view and safe
  areas. **CarPlay lays its screen out at exactly `widthPixels` by `heightPixels`**, which is how
  this plugin's "the frame is the view's size" rule carries over.
- **pair-setup** (`native/crates/livi-cp/src/pair_setup.rs`, `srp.rs`) is HomeKit style SRP-6a,
  3072 bit RFC 5054 group, SHA-512, user "Pair-Setup" and the **fixed setup code "3939"**
  (`pair_setup.rs:24`). M5 and M6 exchange Ed25519 long term keys sealed with ChaCha20-Poly1305
  under HKDF-SHA512 keys ("Pair-Setup-Encrypt-Salt" and "-Info", nonces "PS-Msg05", "PS-Msg06").
  The phone's long term key is stored per controller id in `pairings.json`.
- **pair-verify** (`native/crates/livi-cp/src/pair_verify.rs`) is X25519 plus Ed25519
  ("Pair-Verify-Encrypt-Salt", nonces "PV-Msg02", "PV-Msg03"). Afterwards the control channel is
  encrypted with HKDF-SHA512(shared, "Control-Salt", ...): LIVI reads with
  "Control-Write-Encryption-Key" and writes with "Control-Read-Encryption-Key", the names being the
  phone's point of view (`pair_verify.rs:158-161`). The cipher switches on right after M4 is
  written (`control.rs:28-38`).
- **auth-setup**, the legacy MFi exchange (`native/crates/livi-cp/src/auth_setup.rs`): the phone
  sends `0x01` and an X25519 key (33 bytes). LIVI makes its own, takes SHA-256 (or SHA-1 for a 2.0
  chip) of `our_pub || peer_pub`, has the signer sign that digest, and answers
  `our_pub || u32 cert_len || cert || u32 sig_len || AES-128-CTR(sig)` with key and IV from
  SHA1("AES-KEY" or "AES-IV" || shared).
- **MFi 4.0 mutual authentication** replaces it when the request has `X-Apple-AT: 0x20`
  (`native/crates/livi-cp/src/mfi4/`): a SIGMA-I exchange with P-256 ECDH, AES-CMAC key
  derivation, AES-128-CCM messages, Apple's compressed certificate format for the phone's chain,
  and the accessory's signature from the same signer (64 bytes, so a 3.0 chip only,
  `mfi4/session.rs:178-180`). Once proven, pair-setup travels through its tunnel. The phone's chain
  is only checked if `accessory-root-ca.pem` is provided (`mfi4/cert.rs:16-20`). Section 4.1 has
  what it signs.
- **Session SETUP** (no `streams`, `native/crates/livi-cp/src/stack/setup.rs:49-153`) reads the
  phone's `timingPort`, name and MACs and answers `timingPort`, `eventPort` and
  `enabledFeatures` (`"iAPChannel"`, `"viewAreas"`, `"altScreen"` with a cluster, `"hevc"` if
  offered, and the video, vehicle state and uiSync features the phone asked for).
- **Stream SETUP** (`setup.rs:16-47`): type 110 main screen, 111 cluster screen, 100 main audio,
  101 alternate audio, 102 main high audio, 130 data stream (`native/crates/livi-cp/src/stack.rs:54-60`).
  Each answer names our `dataPort` (and `controlPort` for audio).
- **RECORD** starts the session: LIVI replays night mode and appearance and opens the iAP tunnel
  (`control.rs:91-116`).

Every channel after pair-verify uses ChaCha20-Poly1305 with keys from HKDF-SHA512 and a 32 byte
output (`native/crates/livi-cp/src/crypto.rs:55-87`):

| Channel | Salt | Info | Framing |
|---|---|---|---|
| control | `Control-Salt` | read `Control-Write-Encryption-Key`, write `Control-Read-Encryption-Key` | `u16 LE length || ciphertext || 16 byte tag`, AAD the 2 length bytes, nonce `0000 || u64 LE counter` per direction, at most 0x4000 plaintext per frame (`native/crates/livi-cp/src/control_cipher.rs`) |
| events | `Events-Salt` | write `Events-Write-Encryption-Key`, read `Events-Read-Encryption-Key` | same framing (`native/crates/livi-cp/src/stack/events.rs:13-19`) |
| screen | `DataStream-Salt` followed by the `streamConnectionID` in decimal | `DataStream-Output-Encryption-Key` | below |
| audio in | same | `DataStream-Output-Encryption-Key` | below |
| microphone | same | `DataStream-Input-Encryption-Key` | below |
| data stream 130 | `DataStream-Salt` followed by the `seed` | read Output, write Input | as control (`native/crates/livi-cp/src/iap_tunnel.rs:47-107`) |

#### Step 9: the screen stream

`native/crates/livi-screen-stream/src/lib.rs`. The phone connects to our screen `dataPort` over TCP.

- Each message has a **128 byte header**: bytes 0 to 3 the body size (u32 little endian), byte 4
  an opcode (0 video frame, 1 video config). LIVI reads nothing else from the header, so
  timestamps and the rest of it are **[unconfirmed]**. Bodies are capped at 8 MiB.
- Frames are ChaCha20-Poly1305 with the **whole 128 byte header as AAD** and nonce
  `0000 || u64 LE frame counter` (frames only, from 0, reset on reconnect). Config messages are
  not encrypted and do not advance the counter (`lib.rs:72-92`).
- The config body contains an `avcC` (or `hvcC`) atom, found by scanning for the ASCII marker
  (`native/crates/livi-video/src/nal.rs:20-48`). Frames are AVCC, 4 byte big endian NAL lengths.
- LIVI hands both to GStreamer's `h264parse`. **This plugin's decoder takes Annex B**, so the port
  parses avcC (SPS count in `byte5 & 0x1f`, u16 length per entry, then PPS count and entries),
  emits each parameter set behind `00 00 00 01`, and replaces every length prefix in a frame with
  a start code. Prepending the parameter sets to each IDR keeps the decoder's "SPS and PPS come
  inline" assumption (`docs/architecture.md`) true.
- Asking for a keyframe is the event command `forceKeyFrame {uuid: display uuid}`
  (`native/crates/livi-cp/src/stack/events.rs:151-157`), which is what
  `VideoDecoder::SetKeyframeRequest` maps to.

#### Step 10: audio and the microphone

- Audio types (`native/crates/livi-cp/src/stack.rs:97-123`): `media`, `telephony`,
  `speechRecognition`, `alert`, `default`, `compatibility`, empty (navigation, labelled "nav").
- **Formats LIVI offers** (`info.rs:152-187`) are a bitmask per stream: PCM at 8, 16, 24, 32, 44.1
  and 48 kHz mono and stereo (`0x4` to `0x8000`, table in `stack.rs:68-95`), Opus (`0x7000_0000`)
  and AAC-LC (`0x80_0000` at 48 kHz) for type 102 media only. **LIVI offers neither AAC-ELD nor
  ALAC** (no match for either anywhere in `livi-cp` or `livi-audio`).
- **Transport** (`native/crates/livi-audio/src/stream.rs:36-147`): RTP over UDP to our `dataPort`.
  A packet is `12 byte RTP header || ciphertext || 16 byte tag || 8 byte nonce`, the nonce placed
  at bytes 4 to 11 of the 12 byte ChaCha nonce, AAD the RTP timestamp and SSRC (header bytes 4 to
  11). **PCM is signed 16 bit big endian.** AAC is re-framed as RFC 3640 before `faad` or
  `avdec_aac`; Opus goes to `opusdec`. The control port is drained and ignored.
- **Ducking** arrives as `duckAudio {volume dB, durationMs}` and `unduckAudio`
  (`native/crates/livi-cp/src/stack/commands.rs:59-78`). Siri and calls are read from
  `modesChanged.appStates`: app state 2 with entity 1 is a call, app state 1 with speech mode 1 or
  2 is Siri (`commands.rs:108-141`).
- **Microphone** (`setup.rs:271-318`, `native/crates/livi-audio/src/uplink.rs`,
  `native/crates/livi-audio/src/player/uplink.rs:65-187`): for a type 100 stream that names the
  phone's `dataPort`, LIVI sends RTP back to the phone: header `0x80, PT 100, seq, timestamp,
  SSRC 0`, then ChaCha20-Poly1305 with the Input key, then the 8 byte nonce counter. PCM big
  endian, 20 ms frames by default; it opens when that stream's first packet arrives.
- **Timing** is NTP like, not PTP (`native/crates/livi-cp/src/timing.rs`, `timing_sync.rs`): 32
  byte UDP packets, payload type 210 request and 211 response, NTP64 timestamps. LIVI answers the
  phone and also asks every second. Ten seconds of silence on timing ends the session
  (`stack.rs:62,632-640`). `POST /feedback` reports per stream sample times
  (`native/crates/livi-cp/src/stack/feedback.rs`).

Mapping onto this plugin's three streams (my proposal, not LIVI's): `media` to media, empty and
`alert` and `default` to navigation, `telephony` and `speechRecognition` to voice.

#### Step 11: input and commands

- HID devices are declared in `/info.hidDevices` (`native/crates/livi-cp/src/hid.rs:16-112`): a
  touch screen (two contacts, `[contact id, tip, x u16 LE, y u16 LE]` each, logical maximum the
  display's pixel size), a knob (`[buttons, x, y, wheel]`), media keys (one byte index: play,
  pause, play/pause, next, previous) and telephony keys. A report goes out on the event channel as
  `{type: "hidSendReport", uuid, hidReport}` (`native/crates/livi-cp/src/stack/events.rs:101-108`).
  Touch is in **display pixels**, which matches the plugin's `sendTouch` units directly.
- Commands LIVI sends (`events.rs:110-174`, `commands.rs:235-315`): `forceKeyFrame`,
  `setNightMode`, `uiAppearanceUpdate`, `mapAppearanceUpdate`, `setLimitedUI`, `showUI` and
  `stopUI` (cluster), `requestSiri {siriAction 2, then 3}`, `requestUI {url}`, `iAPSendMessage`,
  `changeModes` (only to borrow the screen for video).
- Commands the phone sends (`commands.rs:8-106`): `requestUI` (any url other than video playback
  is the "back to the car" icon, which is this plugin's `CarPlayRequest.showHostUi`),
  `disableBluetooth`, `modesChanged`, `duckAudio`, `unduckAudio`, `iAPSendMessage` and a few
  logged only.

#### Step 12: iAP2 inside AirPlay

Once AirPlay runs, iAP2 can also travel inside it (`native/crates/livi-cp/src/iap_tunnel.rs`):
a type 130 data stream with client type `E9459FD0-BCAD-4C45-820F-1E72447EF2F2`, and the
`iAPSendMessage` command, both carry iAP2 bytes in 32 byte header packages (`'comm'` bodies). LIVI
relays them into the same iAP2 bring-up code. This is what keeps now playing and route guidance
alive after the phone sends `disableBluetooth` on a wireless session
(`native/crates/livi-cp/src/manager.rs:524-538`).

### 2.4 Wireless, where it differs

- **Bluetooth is BlueZ over D-Bus** on Linux (`native/crates/livi-runtime/src/bt.rs`). LIVI
  registers three profiles (`bt.rs:355-415`): the iAP2 server
  `00000000-deca-fade-deca-deafdecacaff` on **RFCOMM channel 3** with an explicit SDP record
  ("Wireless iAP", `bt.rs:45-58`), a client profile for the phone's
  `00000000-deca-fade-deca-deafdecacafe` with AutoConnect (used to wake a known phone), and a
  "CarPlay" record `ec884348-cd41-40a2-9727-575d50bf1fd3` claiming channel 4 (with no Channel
  option, so BlueZ may pick another **[unconfirmed]**). Its agent is KeyboardDisplay and accepts
  every confirmation (`bt.rs:107-134`). It writes `Class = 0x200418` into
  `/etc/bluetooth/main.conf` and restarts bluetoothd with `--noplugin=midi`
  (`native/crates/livi-runtime/src/bluetoothd.rs`); LIVI's own host stack uses `0x200420` (car
  audio) instead, and which one an iPhone wants is **[unconfirmed]**. HFP is not needed for
  CarPlay calls, which come over iAP2 and AirPlay audio (inferred from `bringup.rs:146-159`).
- **iAP2 over RFCOMM** is the same byte stream as over the cable, with LIVI passive and normal
  acks. Identification declares the Bluetooth and wireless CarPlay components instead of USB host,
  and power capability none.
- **The phone asks for the Wi-Fi network** with `RequestAccessoryWiFiConfigurationInformation
  0x5702`; LIVI answers `AccessoryWiFiConfigurationInformation 0x5703 {1 ssid, 2 passphrase,
  3 security 2 (WPA2/WPA3 transition), 4 channel}` from the live access point
  (`bringup.rs:291-299,559-569`), waits up to 30 s for the AP, and sends `0x4301` with the wireless
  group and the AP interface's fe80 address.
- **The access point** is hostapd plus dnsmasq under a root systemd unit
  (`native/crates/livi-runtime/src/wifi_ap.rs`, `native/crates/livi-wifi/src/ap_config.rs`): 5 GHz
  channel 36 by default, HT, VHT and HE where the radio can, WPA2 or WPA2/WPA3 transition with
  CCMP, IPv4 10.10.0.1/24 for DHCP only (`dhcp-range 10.10.0.10,10.10.0.50`, no DNS), and an IPv6
  link local address forced onto the interface because "CarPlay needs one". Beacons and
  association responses carry an Apple vendor element `dd 08 00 a0 40 00 00 02 00 21` (`22` at
  2.4 GHz) whose meaning the code does not explain (`ap_config.rs:139-142,304-307`).
- **Bonjour**: `avahi-publish-service LIVI _airplay._tcp <port>` with TXT
  `deviceid, features=0x44540380,0x61, flags=0x4, model=LIVI, srcvers=950.7.1, protovers=1.1, pi,
  pk` (`native/crates/livi-runtime/src/bonjour.rs:58-74,108-137`). LIVI also browses the phone's
  `_carplay-ctrl._tcp` and, for each phone found, sends `GET /ctrl-int/1/connect` with
  `AirPlay-Receiver-Device-ID: <our MAC as a decimal integer>` to nudge it to connect
  (`bonjour.rs:235-399`).
- **Reconnect** on a later start pages known phones with `Device1.ConnectProfile(...cafe)` once a
  second, 15 fast tries then every 30 s, and disconnects a phone that connects but opens no iAP2
  link within 10 s (`native/crates/livi-runtime/src/reconnect.rs:14-101,248-335`).
- **Android** uses Java for both radios: `BluetoothServerSocket` on the iAP2 UUID (channel chosen
  by Android) and a Wi-Fi Direct group owner instead of hostapd, with `livi-mdns` announcing
  `_airplay._tcp` on that interface (`android/app/src/main/java/dev/fio/livi/LiviBluetooth.java`,
  `LiviHotspot.java`, `native/crates/livi-helper/src/wireless.rs`). Wireless works there without a
  LIVI Link.
- **A LIVI Link can bring the radios**: its Wi-Fi AP is bridged to its USB network, and its
  Bluetooth controller is tunnelled to the host (TCP 5002) and appears as a `/dev/vhci` adapter
  that BlueZ drives normally (`native/crates/livi-link-host/src/bt.rs:15-77`).

### 2.5 Metadata over iAP2

LIVI turns iAP2 into JSON lines (`native/crates/livi-runtime/src/events.rs`). This plugin instead
has to produce the JSON its Dart models already parse, which is the adapter's
(`packages/carplay_platform_interface/lib/src/metadata.dart`: `MediaSongName`,
`MediaPlayStatus`, `NaviStatus`, `NaviManeuverType`, `CallStatus` and so on). The adapter's JSON
is itself a translation of these same iAP2 messages **[unconfirmed]**, so the mapping should be
close to one to one.

| iAP2 | Ids and fields LIVI decodes | Plugin hook |
|---|---|---|
| NowPlayingUpdate 0x5001 | media item: 0 persistent id, 1 title, 4 duration ms, 6 album, 12 artist, 14 album artist, 16 genre, 26 artwork file transfer id; playback: 0 status (stopped, playing, paused, seeking), 1 elapsed ms, 7 app name, 16 bundle id (`native/crates/iap2-csm/src/messages/now_playing.rs`) | `kNowPlaying` |
| file transfer, session 12 | the artwork blob, not matched to the ftid by LIVI (`native/crates/livi-runtime/src/livi_sock.rs:386-401`) | `kAlbumArt` |
| RouteGuidanceUpdate 0x5201, ManeuverUpdate 0x5202, LaneGuidance 0x5204 | state, maneuver state, road, destination, eta, time and distance remaining with display strings and units, current maneuver list, maneuver type, driving side, junction, exit angle (`route_guidance.rs`) | `kNavigation` |
| CallStateUpdate 0x4155 | remote id, display name, status, direction, call uuid, disconnect reason (`communications.rs`) | `kCall` |
| CommunicationsUpdate 0x4158, PowerUpdate 0xAE01 | signal, carrier, battery level, charging | `kPhone` |
| DeviceTimeUpdate 0x4E0B | time and zone; LIVI treats the seconds as Unix time, which may be the 2001 epoch **[unconfirmed]** | news only |
| StartLocationInformation 0xFFFA | the phone asks for GPGGA, GPRMC, GSV; answered with one `LocationInformation 0xFFFB` per NMEA sentence until 0xFFFC (`native/crates/livi-runtime/src/vehicle.rs:178-282`) | from `setLocation`, reusing `src/dongle/nmea.*` |
| StartVehicleStatusUpdates 0xA100 | answered with `VehicleStatusUpdate 0xA101` (range, temperature, battery) | not offered at first |

`SetNowPlayingInformation 0x5003 {elapsed ms}` seeks, if the plugin ever wants a scrubber.

### 2.6 What LIVI does not do

Worth stating, because the M13 brief and `docs/research.md` assumed some of it:

- No AAC-ELD and no ALAC: LIVI offers PCM, Opus and AAC-LC only (2.3, step 10).
- No FairPlay: `/fp-setup` gets an empty 200.
- No CH341: no code or documentation mentions it. Its I2C path is the kernel's `/dev/i2c-N`
  only, and its power GPIO path hard codes `/dev/gpiochip0` (`native/crates/iap2-mfi/src/linux.rs:15,113`).
- No warning about leaked keys: the file signer exists in code and in the settings UI, but the
  README describes only the chip and LIVI Link, and nothing in LIVI ships or names a key
  (`README.md`, "MFi Authentication").
- No root free mode on Linux.

---

## 3. Libraries for each piece, and the C++ equivalents

| Piece | LIVI uses | Linux C++ | Android NDK |
|---|---|---|---|
| USB | nusb 0.2.9; a fork of nusb with JNI on Android (`native/Cargo.toml:30-31`) | libusb 1.0, already linked: `libusb_control_transfer(0xC0, 0x52, 0, 4)`, `libusb_set_configuration(6)`, `libusb_detach_kernel_driver`, `libusb_set_interface_alt_setting`, `libusb_reset_device` | libusb from the existing submodule, `libusb_wrap_sys_device` on the fd from `UsbManager`, as `src/dongle/usb_device_source_android.cc` already does |
| usbmux | own (`mux.rs`) | port it, about 300 lines | same |
| lockdown, pairing, TLS to the service | the `idevice` crate (rustls, rsa) | own lockdown client on OpenSSL: plist requests, host key and certificate generation, pair records, TLS with the pair record's certificate. libimobiledevice (LGPL-2.1, installed here) is the reference, but it insists on talking through usbmuxd | the same own client |
| plists | `plist` for lockdown, own bplist for AirPlay | own bplist (port of `bplist.rs`) plus a minimal XML plist for lockdown; libplist (LGPL-2.1, 2.7.0 installed here) is the fallback | same, vendored if libplist |
| NCM, IPv6 | userspace NCM into a TAP, kernel `cdc_ncm` as fallback | the kernel `cdc_ncm` netdev first (no privilege at run time, see 6.4), LIVI's userspace NCM into a TAP as the fallback | userspace NCM (port of `ncm/*`, `ntb.rs`, `ether.rs`) into a `VpnService` TUN from Java |
| iAP2 | own | port | same |
| crypto: ChaCha20-Poly1305, X25519, Ed25519, HKDF-SHA512, SHA-1/256/512, AES-CTR, AES-CCM, AES-CMAC, P-256 ECDH and ECDSA, big numbers for SRP, PKCS#7 and X.509 | chacha20poly1305, x25519-dalek, ed25519-dalek, hkdf, sha1, sha2, aes, ctr, num-bigint, openssl, aws-lc-rs | **OpenSSL 3** (libssl-dev 3.5.5 here); every primitive is in libcrypto | OpenSSL built with the NDK and linked statically (LIVI vendors OpenSSL on Android too, `native/crates/livi-cp/Cargo.toml`). BoringSSL lacks some of these modes **[unconfirmed]** |
| video | GStreamer `h264parse` and decoders | the existing `LibavDecoder`, fed Annex B | the existing `MediaCodecDecoder` |
| PCM | GStreamer | the existing `AudioOutput` (PulseAudio), after a byte swap | the existing AAudio sink |
| AAC-LC, Opus (only if offered) | GStreamer `faad` or `avdec_aac`, `opusdec` | libavcodec, already linked (its native `aac` and `opus` decoders; the `aac` decoder also handles ELD should it ever be needed **[unconfirmed]**) | MediaCodec `audio/mp4a-latm` and `audio/opus` |
| microphone | GStreamer, `opusenc` for Opus | the existing `AudioInput`, PCM only | AAudio source |
| mDNS publish and browse | `avahi-publish-service` and `avahi-browse` as subprocesses, `dns-sd` on macOS, own IPv6 responder on Android | libavahi-client over D-Bus (avahi-daemon is running here), no subprocesses | port of `livi-mdns` responder (about 200 lines); `NsdManager` is the alternative **[unconfirmed]** for link local |
| Bluetooth | BlueZ over zbus | BlueZ over GDBus, extending android_auto's `bluez_client` (GTK already brings GIO) | Java `BluetoothServerSocket`, fd over JNI, as android_auto's `bluez_client_android.cc` |
| Wi-Fi AP | hostapd, dnsmasq, iw, nmcli as root | the machine's business, as in android_auto (see 6.5); the plugin reads SSID and channel through nl80211 | Wi-Fi Direct group owner from Java |
| async runtime | tokio | threads with blocking I/O and one poll loop where needed, the same style as `src/dongle/` | same |

The new Linux build dependencies are OpenSSL and libavahi-client. Both are in every distribution
and both are compatible with GPL-3.0-or-later (OpenSSL 3 is Apache-2.0).

---

## 4. The MFi signer

### 4.1 What is signed

There is one primitive: **sign a digest sized input with the accessory's private key and return
the raw signature**, plus reading the certificate and the chip's generation. LIVI's trait is
exactly that (`native/crates/iap2-mfi/src/lib.rs:61-65`):

```rust
fn protocol_major(&mut self) -> Result<u8>;
fn read_certificate(&mut self) -> Result<Vec<u8>>;
fn generate_challenge_response(&mut self, challenge: &[u8]) -> Result<Vec<u8>>;  // 1..=128 bytes
```

It is used in three places, each of which computes its own input:

| Where | What goes to the signer | What happens to the signature |
|---|---|---|
| iAP2 `0xAA02` (`native/crates/livi-runtime/src/bringup.rs:250-257`) | the phone's challenge, untouched | returned in `0xAA03` |
| AirPlay `/auth-setup`, legacy (`native/crates/livi-cp/src/auth_setup.rs:41-45`) | SHA-256 of `our X25519 public || phone's X25519 public`, or SHA-1 for a 2.0 chip | AES-128-CTR encrypted, then returned |
| AirPlay MFi 4.0 (`native/crates/livi-cp/src/mfi4/session.rs:340-366`) | SHA-256 of `0x01 || our ephemeral || phone ephemeral || AES-CMAC(our MAC key, our_hash)`, with `our_hash = SHA-256(16 random bytes || our certificate)` | sent in `0x5214`, must be 64 bytes |

Sizes:

| Generation | Key | Challenge | Response | Certificate |
|---|---|---|---|---|
| 2.0 (2.0C) | RSA-1024, PKCS#1 v1.5 over a SHA-1 DigestInfo | 20 bytes (the RSA file path refuses anything else, `native/crates/iap2-mfi/src/file.rs:122-133`) | 128 bytes | **[unconfirmed]** about 900 to 1000 bytes |
| 3.0 | ECDSA P-256, signing the input as a prehash | 32 bytes **[unconfirmed]** for iAP2 | 64 bytes raw `r || s` (`file.rs:116-121`) | **[unconfirmed]** about 600 bytes |

The certificate is opaque to LIVI: a PKCS#7 (DER) blob passed through as read. The generation
comes from register 0x02 on a chip, from the key type for files, and on the dongle from the
certificate length when the register reads oddly (under 768 bytes means 3,
`native/crates/iap2-mfi/src/server.rs:18,130-141`). Only a 3.0 signer enables MFi 4.0 mutual
authentication.

### 4.2 An MFi coprocessor on I2C, as LIVI drives it

`native/crates/iap2-mfi/src/lib.rs:10-24` and `linux.rs`:

| Register | Name | Use |
|---|---|---|
| 0x00 | device version | 1 byte, the probe |
| 0x02 | protocol major | 1 byte, the generation |
| 0x05 | error code | read after a failed signature |
| 0x10 | authentication control and status | write 0x01 to start; poll until it reads exactly 0x10 |
| 0x11 | signature length | u16 big endian |
| 0x12 | signature data | |
| 0x20 | challenge length | u16 big endian |
| 0x21 | challenge data | |
| 0x30 | certificate length | u16 big endian |
| 0x31 | certificate data | read in one go from 0x31 |

- 7 bit address 0x10 or 0x11 (8 bit 0x20 or 0x22), probed in that order by writing `[0x00]` and
  reading a byte (`linux.rs:135-158`).
- Plain i2c-dev: `open("/dev/i2c-N")`, `ioctl(I2C_SLAVE = 0x0703, addr)`, then a separate write of
  the register number and a read; **no repeated start and no `I2C_RDWR`** (`linux.rs:177-213`).
- **The chip sleeps and NACKs**, so every write and read is retried every 500 us for up to 2 s
  (`linux.rs:160-175`).
- Signing: write 0x20 the length, write 0x21 the challenge, write 0x10 the value 0x01, sleep
  10 ms, poll 0x10 every 10 ms for up to 3 s until it reads 0x10, then read 0x11 and 0x12. On a
  timeout read 0x05 (`linux.rs:232-258`).
- Power: optionally a GPIO through the v2 character device on `/dev/gpiochip0`, driven low 50 ms
  then high and 100 ms settle on every open (`linux.rs:15-98`). Defaults bus 2, GPIO 21
  (`native/crates/livi-helperd/src/linux_main.rs:257-258`).
- **[unconfirmed]** On 2.0C chips the certificate is paged in 128 byte blocks across registers
  0x31 to 0x3A; LIVI's single read relies on the chip advancing across them.

### 4.3 The same chip over a CH341, which LIVI does not do

A laptop has no I2C header, so on this machine a chip needs a USB to I2C bridge. The usual one is a
CH341A module. Everything in this subsection is from outside LIVI and **[unconfirmed]**:

- In I2C mode the CH341A enumerates as `1a86:5512` and takes I2C as a command stream on bulk
  endpoint 0x02 (replies on 0x82): `0xAA` (stream command), then sub-commands `0x74` start,
  `0x80 | n` write n bytes, `0xC0 | n` read n bytes, `0x75` stop, `0x60 | speed` set the clock,
  `0x00` end, in packets of at most 32 bytes. Check against WCH's CH341 datasheet and an open
  driver (the out of tree `i2c-ch341-usb` kernel module, or flashrom and ch341eeprom).
- Mainline Linux has no I2C driver for it, only the serial driver for its UART mode, so
  `/dev/i2c-N` is not available without an out of tree module.
- **Recommendation: drive the CH341 from userspace through libusb.** That needs no kernel module,
  no root (a udev rule for `1a86:5512`), and works unchanged on Android through `UsbManager` and
  `libusb_wrap_sys_device`, which the plugin already does for the Carlinkit adapter. The register
  sequence of 4.2 sits on top unchanged, through an `I2cBus` interface with two implementations
  (i2c-dev and CH341).
- The NACK behaviour matters more here: a CH341 reports ACKs per byte in its read back, and the
  retry loop of 4.2 has to look at that rather than at an `errno`.
- The module must be set to 3.3 V (most have a jumper). A chip on a breakout needs its supply,
  ground, SDA and SCL; LIVI's community PCB for the MFI337S3959 (2.0) and MFI343S00177 (3.0) is in
  LIVI discussion #346 (`docs/research.md`, 2c).

### 4.4 A user supplied certificate and key

`native/crates/iap2-mfi/src/file.rs`: two files, `certificate.p7b` (DER or PEM PKCS#7) and
`identity.pk8` (an unencrypted PKCS#8 P-256 or RSA key). The key type gives the generation; EC
signs the input as a prehash, RSA requires 20 bytes and pads PKCS#1 v1.5 with the SHA-1
DigestInfo. LIVI checks only that the certificate starts with 0x30 and does not check that key and
certificate match. Its UI imports them into `~/.config/LIVI/mfi/` with mode 0600 in a 0700
directory (`native/crates/livi-core/src/mfi.rs:60-77`).

This is the route someone would use with an identity they own: an MFi licensee's own accessory
identity, for instance. It is also the route a leaked identity would take, which is why 4.7
exists.

### 4.5 LIVI Link, a dongle as a network signer

A Carlinkit dongle reflashed with LIVI Link firmware keeps its MFi chip and serves it over the
network (`LIVI-LINK.md`).

- **Finding it.** It enumerates as a USB NCM gadget, VID `0x0525` PID `0xa4a1`, product string
  **"LIVI Link"**, which is what the host matches (`native/crates/livi-link-host/src/lib.rs:27,41-43`,
  `scripts/livi-link/common/initramfs/net-up:14-22`). On Linux the kernel's `cdc_ncm` gives a
  netdev; LIVI finds it through sysfs by that product string and has NetworkManager take a DHCP
  address on it (`native/crates/livi-link-host/src/link.rs:56-144`). The dongle is
  `10.10.10.1/24` and leases 10.10.10.100 to 199 (`native/crates/livid/src/netd.rs:27-49`), and
  answers mDNS for `livi-link.local`. On Android, with no netdev, LIVI runs NCM and a DHCP client
  in userspace (`native/crates/livi-link-host/src/usb_net.rs`, `net4.rs`).
- **The signer protocol**, TCP port 5000 (`native/crates/livi-net/src/port.rs:5`,
  `native/crates/iap2-mfi/src/server.rs:11-66`, client `native/crates/iap2-mfi/src/ncm.rs`):

  ```
  request   0x01                       get certificate
            0x02 | u16 BE n | n bytes   sign (n in 1..=128, otherwise an error and a close)
            0x03                       protocol major
  response  status u8 (0 ok, 1 error) | u16 BE length | data
  ```

  One persistent connection, many requests; the chip is behind a mutex on the dongle. The client
  connects in 2 s, reads with a 5 s timeout and retries once after 500 ms.
- **No authentication and no encryption.** The dongle only refuses peers that are not on its USB
  side, by an ARP and bridge table check (`native/crates/livi-net/src/bridge.rs:11-46`), because
  its Wi-Fi AP shares the bridge. Any process on the host can use it as a signing oracle. That is
  acceptable for a device plugged into the head unit, and worth saying in the plugin's docs.
- Other ports on the dongle: 5001 a text control protocol for its Wi-Fi AP, 5002 an HCI tunnel for
  its Bluetooth, 5003 iAP2 from phones it paired itself, 5004 its accessory control
  (`native/crates/livi-net/src/port.rs:5-9`).
- **Which dongles.** i.MX6UL boards (the CPC200-CCPA among them), Allwinner V821B and Axera AX520CE
  (Carlinkit Mini Ultra 3, C2Air), per the firmware table in `LIVI-LINK.md`. A CCPA on stock
  firmware 2025.10.15.1127 needs a downgrade to 2025.02.25.1521 or a newer provisioning tool
  (`docs/research.md`). Provisioning runs LIVI's `livi-link-provision` against the dongle; this
  plugin does not flash anything.

### 4.6 The interface in this plugin

`packages/carplay_native/src/native/mfi/`:

```cpp
namespace cp::native {

// What every signer can say about itself, for the example's bench and for logs.
struct SignerInfo {
  std::string backend;     // "i2c /dev/i2c-2 @0x11", "ch341", "livi-link 10.10.10.1", "files"
  int generation = 0;      // 2 or 3, 0 until known
  size_t certificate_size = 0;
};

// The accessory's identity: a certificate and a key that never leaves its holder. Every
// call may block (an I2C signature takes up to 3 s), so none is ever made on a thread
// that must not block. Implementations serialise their own calls.
class MfiSigner {
 public:
  virtual ~MfiSigner() = default;
  virtual SignerInfo Info() = 0;
  virtual Result<int> Generation() = 0;                       // 2 or 3
  virtual Result<std::vector<uint8_t>> Certificate() = 0;     // DER PKCS#7, as is
  // `input` is 1 to 128 bytes, already hashed by the caller (section 4.1). Returns the
  // raw signature: 64 bytes r || s for 3, 128 bytes for 2.
  virtual Result<std::vector<uint8_t>> Sign(std::span<const uint8_t> input) = 0;
};

// The byte level bus a coprocessor sits on.
class I2cBus {
 public:
  virtual ~I2cBus() = default;
  virtual Result<void> Write(uint8_t address, std::span<const uint8_t> bytes) = 0;
  virtual Result<void> Read(uint8_t address, std::span<uint8_t> into) = 0;
};

std::unique_ptr<I2cBus> OpenI2cDev(int bus);                 // /dev/i2c-N, Linux
std::unique_ptr<I2cBus> OpenCh341(UsbDeviceSource& usb);     // libusb, Linux and Android
std::unique_ptr<MfiSigner> CoprocessorSigner(std::unique_ptr<I2cBus> bus,
                                             std::optional<GpioLine> power);
std::unique_ptr<MfiSigner> LiviLinkSigner(std::string host = "livi-link.local", int port = 5000);
std::unique_ptr<MfiSigner> FileSigner(std::string certificate_path, std::string key_path);

}  // namespace cp::native
```

`std::span` means C++20; the plugin is C++17 today, so either raise the standard for `native/` or
use pointer and length pairs. `Result` is whatever error type `native/` settles on.

From Dart, a `CarPlayNativeSigner` in `CarPlayConfig`, one of:

```dart
CarPlayNativeSigner.coprocessor(i2cBus: 2, powerGpio: 21)   // a board with I2C
CarPlayNativeSigner.ch341()                                 // first CH341 on USB
CarPlayNativeSigner.liviLink(host: 'livi-link.local')
CarPlayNativeSigner.files(certificate: '/path/cert.p7b', key: '/path/key.pk8')
```

A signer that appears and disappears (a LIVI Link unplugged) is swapped at run time behind the
same object, as LIVI's `SharedCoprocessor::replace` does (`native/crates/livi-runtime/src/mfi_async.rs:23-25`).
The bring-up asks for the signer before it wants one, and the state hook says "No MFi signer" the
same way the adapter backend says "No Carlinkit adapter on USB".

A one shot check belongs with it, and is the first milestone that needs a signer: read the
certificate, pull the public key out of the PKCS#7 with OpenSSL, sign a random 32 byte digest, and
verify the signature against that key. A signer that passes this will pass iAP2 authentication,
since the phone does the same check.

### 4.7 What this plugin never does with keys

**The plugin never bundles, downloads, fetches, generates or points to an accessory private key
or certificate it does not own**, and in particular not the Carlinkit identity recovered from
firmware in 2026 (`docs/research.md`, 2a). Concretely:

- No key or certificate in the repository, the pub.dev package, the example, CI, test fixtures or
  release artefacts. Tests sign with a key generated in the test itself, clearly not Apple's.
- No URL, file name, hash or hint in code, docs, issues or logs that helps find one.
- `CarPlayNativeSigner.files` reads only paths the host app passes, never searches for files,
  never copies them, never logs their contents or fingerprints, and refuses a key file that other
  users can read.
- The plugin's README says the files route is for an identity the user is entitled to use, and
  that the user is responsible for it.

This restates the rule already in `AGENTS.md` ("Things this project does not do").

---

## 5. Port to C++ or link the Rust: the recommendation

### 5.1 The two options

1. **Port** the relevant crates to C++ in `packages/carplay_native/src/native/`, behind the
   existing seams, using LIVI as the reference and its tests as golden vectors.
2. **Link**: a new Rust crate with a C ABI facade over `iap2-*`, `livi-runtime` and `livi-cp`,
   built as a static library for each target and linked into the plugin's shared library.

### 5.2 What decides it

| Factor | Port to C++ | Link the Rust |
|---|---|---|
| Toolchain | none new. CMake and the NDK as today | LIVI pins a **beta** toolchain, `beta-2026-11-04` (1.100.0-beta.9), raised by hand (`rust-toolchain.toml`), edition 2024, and every crate takes `libc 1.0.0-alpha.5` (`native/Cargo.toml:21`). Whether stable builds it is **[unconfirmed]**. Every developer of every host app would need rustup with that toolchain and the Android targets (cargo-ndk, `native/xtask/src/package_android.rs:96`); this machine has no Rust at all today |
| pub.dev packaging | the plugin builds from source with the CMake Flutter already drives | needs cargokit or Dart build hooks to call cargo from CMake and Gradle, or prebuilt static libraries per ABI checked into the package (and their source offered, GPL section 6) |
| Dependency tree | OpenSSL and avahi-client | LIVI's crates are path dependencies, not on crates.io, so the whole LIVI workspace comes along as a submodule. The lock file closure of `iap2-*`, `livi-cp`, `livi-runtime`, `livi-link-host` and `livi-mdns` is 285 crates (counted from `native/Cargo.lock`, all targets, so it overstates one platform), among them tokio, zbus, openssl with `openssl-src` vendored on Android, aws-lc-rs (a C and assembly build), rustls, ring, idevice, nusb and the nusb fork |
| Build time | incremental C++, ccache already wired | a clean build of that tree (aws-lc-sys, openssl-src, tokio) is minutes per target; each ABI is another full build **[unconfirmed]** for the exact figure |
| Fit with the seams | written against `VideoDecoder`, `AudioOutput`, `AudioInput`, libusb, JNI as they are | `livi-cp`'s media path is GStreamer (`livi-screen-stream` needs glib, `livi-audio` needs gstreamer); it owns USB through nusb, which on Android is a fork with its own JNI that would compete with the plugin's `UsbManager` handover; it reaches its signer and iAP2 over Unix sockets to a root helper. All of that would be rewritten or stubbed in Rust anyway |
| Process model | in process, threads | LIVI's code assumes the helper split (2.1); the facade would have to reassemble it in one process |
| Upstream tracking | manual: a fix in LIVI is re-done here | in principle a `git pull`, but LIVI's internal APIs move with every release (10.4.0 on 2026-11-04, commits daily), and the facade and stubs break with them |
| Size of the work | about 15k lines of Rust become C++ for wired, 2.5k more for wireless (2.2) | the facade, the GStreamer replacement for media, the in process replacement for the helper socket, the USB integration, and the build plumbing, which is a large part of the same work |
| Readability for the next session | one language, the style of `src/dongle/` | two languages, two build systems, two async models |

### 5.3 Recommendation: port to C++

**Port the protocol to C++ under `src/native/`, and treat LIVI as the specification and its tests
as the oracle.** The reasons, in order of weight:

1. **The parts worth reusing as they are, are the small dependency free ones.** `iap2-link` and
   `iap2-csm` have no dependencies and ship byte level golden vectors; they port in days and the
   vectors prove the port. The large crate, `livi-cp`, is the one most entangled with LIVI's own
   architecture (tokio, the helper socket, GStreamer for media, CarPlay Ultra features), so
   linking it would not save the work it appears to save.
2. **The toolchain cost lands on every user of the package**, not on this project once. A beta
   Rust pinned by hand, cargo-ndk and a 285 crate build inside `flutter build` is a lot to ask of
   someone who added a pub.dev dependency, and the plugin's Android build currently compiles
   libusb from a submodule and nothing else.
3. **The plugin already has every seam the receiver needs**, in C++: libusb on both platforms
   (with the Android fd handover), libav and MediaCodec decoders that take Annex B, PulseAudio and
   AAudio, JNI, the event bus. A C++ receiver feeds them directly. A Rust one brings its own
   versions of the first three.
4. **One language keeps the shared pipeline rule workable.** `AGENTS.md` asks that a fix to the
   copied files probably goes into android_auto too; a C++ backend can share helpers (BlueZ, the
   Wi-Fi network reader, the NMEA writer) with android_auto in the same way.

What is lost is cheap upstream tracking. To keep it as cheap as possible:

- Each ported file starts with a comment naming the LIVI file and commit it follows, so `git log`
  on that LIVI path shows what changed since.
- LIVI's tests are ported as native tests first, before the code: `iap2-link/tests/engine.rs`,
  `iap2-csm/tests/vectors.txt` and `codec.rs`, `livi-cp/src/ts_vectors.json`, the scripted phone
  sessions in `livi-cp/src/stack/tests.rs`, `iap2-mfi/src/file.rs` signature sizes, and the
  framing tests in `livi-audio/src/stream.rs` and `livi-screen-stream/src/lib.rs`. A protocol fix
  upstream usually comes with a changed or new vector, which then fails here.
- A short table in `src/native/LIVI.md` (written with M13.0) of LIVI file, LIVI commit and plugin
  file, updated when a file is brought up to date.

Linking would become the better choice if LIVI published its iAP2 and AirPlay crates as a
library on crates.io with a stable API, a stable toolchain and no GStreamer in the protocol path.
Worth asking upstream; nothing suggests it is planned.

### 5.4 The licence, and LIVI's attribution term

LIVI is GPL-3.0-or-later with one additional term under section 7(b) (`NOTICE`):

> Any work that contains LIVI or a part of it, in source or object form, whether modified,
> unmodified or translated into another programming language, must retain the following
> attribution:
>
>     Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI)
>
> The attribution must appear in the work's Appropriate Legal Notices, such as an About screen,
> and in its documentation, for example its README. Where LIVI's own About screen is kept, its
> copyright notice satisfies this term.

**A C++ port is a translation, so the term applies exactly as it would to linking.** This plugin
is GPL-3.0-or-later already (`docs/research.md`), so the code combines; what changes is that the
work now carries an additional term. What that means in practice (this is a reading of the
licence, not legal advice):

| Where | What |
|---|---|
| `packages/carplay_native/NOTICE` (new) | the attribution line verbatim, LIVI's copyright line `Copyright (C) 2025-2026 Lasse Heitgres`, and the 7(b) term itself, so it travels with every copy (GPL section 7: additional terms must be stated in the source files or a notice pointing to them) |
| `packages/carplay_native/LICENSE` | the GPL text stays; add a first paragraph pointing to NOTICE. Flutter's `LicenseRegistry` collects every package's `LICENSE` into a host app's `showLicensePage`, which makes the attribution appear in the host app's legal notices without the host doing anything |
| `README.md` at the root and `packages/carplay_native/README.md` | the attribution line verbatim, in a "Credits" or "Licence" section, next to the existing "Not affiliated with or endorsed by Apple" line |
| every ported source file | `SPDX-License-Identifier: GPL-3.0-or-later`, LIVI's copyright line beside this project's, `Additional terms: see NOTICE` (LIVI's own header convention), and a line saying it was translated from LIVI's named file and when (GPL section 5(a), "prominent notices stating that you modified it, and giving a relevant date") |
| the example app | an About entry showing the attribution, since the example is the reference integration |
| the plugin's integration docs | a sentence telling host app authors that an app shipping the native backend contains a part of LIVI and must show the attribution in its own About screen and documentation. The licence page above covers the About screen; their README is theirs to update |

Two further consequences:

- **Code moving to android_auto carries the term.** A file derived from LIVI keeps the
  attribution wherever it goes, so if android_auto borrows (say) the NDP shim, it needs the same
  NOTICE. Files that are not derived from LIVI (the plugin's own pipeline) are unaffected.
- **LIVI's contributor agreement** (`CLA.md`, an exclusive licence to the maintainer) matters
  only for contributing fixes back upstream, not for using the code here.

---

## 6. The backend inside this plugin

### 6.1 Directory layout

```
src/backend.h                    the Hooks, SessionState and MetadataKind moved out of
                                 dongle::, plus a small Backend interface both sessions implement
src/native/
  native_session.*               the backend: owns transports, iAP2 sessions, the receiver,
                                 the signer; same shape as DongleSession
  LIVI.md                        the LIVI file to plugin file map with commits (5.3)
  iap2/link.*                    sans-IO link engine (iap2-link)
  iap2/csm.*, iap2/messages.*    the control session codec (iap2-csm)
  iap2/bringup.*                 identification, authentication, subscriptions, start
  iap2/to_json.*                 iAP2 to the adapter style JSON the Dart models parse
  iap2/file_transfer.*
  usb/iphone_source.*            finding iPhones, 0x52, configuration 6
  usb/usbmux.*                   the mux and its mini TCP
  usb/lockdown.*                 pairing, pair records, StartService, TLS
  usb/ncm.*, usb/ntb.*           userspace NCM, for the TAP fallback and Android
  net/link_local.*               finding our fe80 address and scope on an interface
  airplay/rtsp.*, bplist.*, tlv8.*
  airplay/pairing.*              SRP, pair-setup, pair-verify, the pairings store
  airplay/auth_setup.*, mfi4/*   the two MFi exchanges inside AirPlay
  airplay/info.*                 /info from the plugin's config
  airplay/receiver.*             listener, control, SETUP, RECORD, TEARDOWN, commands
  airplay/events.*               the event channel, HID reports, outgoing commands
  airplay/screen_stream.*        decrypt, avcC to Annex B, VideoDecoder::Submit
  airplay/audio_stream.*         decrypt, byte swap, AudioOutput::Submit
  airplay/mic_uplink.*           AudioInput to RTP
  airplay/timing.*
  crypto/*                       thin OpenSSL wrappers: AEAD, HKDF, X25519, Ed25519, CMAC, CCM
  mfi/*                          section 4.6
  wireless/*                     BlueZ profiles, Wi-Fi network reading, Bonjour (M13.12)
native_test/native/*             the ported LIVI vectors and scripted phone sessions
```

`protocol.*` style files with no state and no I/O (the link engine, the codecs, the ciphers)
are the ones unit tested, as `src/dongle/protocol.*` is.

### 6.2 Threads

| Thread | Owns | Must never |
|---|---|---|
| libusb event thread (shared, process lifetime) | bulk completions for usbmux and NCM | block |
| session control thread (one per native session) | the iAP2 link engine and its timers, the bring-up, signer calls, lockdown | touch GL or Dart |
| AirPlay control thread (one per phone connection) | RTSP requests and answers, pairing, SETUP | call the signer without a timeout |
| screen reader thread | the screen TCP socket, decrypt, `VideoDecoder::Submit` | block on anything but its socket |
| audio receiver thread (one per audio stream) | UDP receive, decrypt, `AudioOutput::Submit` | run a decoder that can stall |
| timing thread | the timing socket, both directions | |
| capture thread (the existing one) | `AudioInput`, now sending RTP | outlive the phone's request |

The signer can take three seconds (4.2), so it runs on the session control thread, and the
AirPlay control thread waits for it with a timeout rather than calling it itself.

### 6.3 What the shared pipeline needs

- `src/backend.h` and a choice in `cp_core.cc`: `CarPlayConfig.backend` of `adapter` or
  `native`, both producing the same `SessionState` and metadata kinds. `cp_core.h` gains only the
  config fields; nothing else above the C ABI changes, as `docs/architecture.md` promises.
- `VideoDecoder` unchanged: Annex B in, `SetKeyframeRequest` wired to `forceKeyFrame`.
- `AudioOutput` unchanged: PCM arrives big endian and is swapped in the backend.
- `AudioInput` unchanged: its 20 ms buffers already match LIVI's default frame.
- Frame size: `ResolveFrameSize` gives `widthPixels` and `heightPixels` for `/info`. Following a
  resize means a new session **[unconfirmed]** whether an AirPlay display can be resized live.
- Touch: the view already sends projected pixels, which is what the HID report wants.

### 6.4 Privileges on Linux, without a root process

| Need | How, as a normal user |
|---|---|
| open Apple devices, set configuration 6 | a udev rule for `05ac` with `TAG+="uaccess"`, added by `tools/setup-dev-machine.sh --udev`, then `libusb_set_configuration`. Whether usbfs allows the switch while a kernel driver is bound, and how usbmuxd reacts, is milestone M13.2's first question |
| the system usbmuxd | either the plugin uses it (libusbmuxd's socket, giving up our own mux on Linux) or the udev rule keeps it off the phones the plugin is driving. Decide on hardware in M13.2 |
| the NCM network | the kernel's `cdc_ncm` binds configuration 6's NCM function by itself (LIVI's installer preloads `cdc_ncm` "so cdc_ncm binds the AV interface at cold boot", `scripts/install/headless/install.sh:118-123`), the kernel gives it a fe80 address, and a udev rule sets `accept_dad=0` as LIVI's does (`assets/linux/99-LIVI.rules.template:10`). NetworkManager must leave it alone or only do link local. No TAP and no `CAP_NET_ADMIN` at run time. The TAP path stays as a fallback for machines where that fails |
| lockdown pair records | in the app's own data directory, not `/var/lib/lockdown` (root owned, and absent on this machine) |
| an I2C chip | membership of the `i2c` group, or a udev rule for `/dev/i2c-N` |
| a CH341 | a udev rule for `1a86:5512` |
| a LIVI Link | nothing; the kernel's `cdc_ncm` and DHCP from NetworkManager |
| Bluetooth profiles | BlueZ's D-Bus policy lets the console user register profiles, which android_auto already relies on |
| the Wi-Fi access point | not the plugin's (6.5) |

### 6.5 Wireless, and whose job the access point is

android_auto decided that bringing up an access point is the machine's business, not a plugin's:
the plugin reads SSID, BSSID and channel off the machine and the host app supplies the passphrase
(`../FlutterAndroidAuto/packages/android_auto_native/src/wireless/wifi_network.h`). The same split
works here: `0x5703` and `0x4301` need SSID, passphrase, channel, security and our fe80 address,
which is the same information. So:

- the plugin registers the Bluetooth profiles, runs iAP2 over RFCOMM, answers `0x5702` and sends
  the wireless `0x4301`, publishes `_airplay._tcp` through avahi, and pages known phones;
- the access point is documented, not run: a `docs/` recipe with LIVI's hostapd settings
  (WPA2 PSK with CCMP, PMF off, or WPA2/WPA3 transition; 5 GHz; the Apple vendor element; a DHCP
  range; a link local address on the interface) and a `dev/` script for this machine;
- android_auto's lessons apply unchanged: the phone must be paired after the service exists, the
  SDP record must name an RFCOMM channel, a phone told WPA2 cannot join a WPA3 only AP, and an
  Intel card cannot beacon on most 5 GHz channels (`../FlutterAndroidAuto/docs/wireless.md`).

Whether CarPlay accepts a network the head unit is a client of, rather than its own AP, is
**[unconfirmed]**; LIVI always runs its own.

### 6.6 Android, later

The Android seams exist already (`packages/carplay_native/android/CMakeLists.txt`). The native
backend adds: the iPhone through `UsbManager` (and a fresh permission after the 0x52
re-enumeration **[unconfirmed]**), userspace NCM into a `VpnService` TUN with LIVI's NDP shim
(user consent dialog), the mDNS responder, Bluetooth through `BluetoothServerSocket`, a Wi-Fi
Direct group owner, a CH341 through `UsbManager`, and a LIVI Link through userspace NCM and DHCP.
That is a milestone of its own after M14.

---

## 7. Milestones

Every box is a checkpoint, ticked only when verified on this machine, as in `PLAN.md`. The order
puts everything that needs only an iPhone (or nothing) first. **Needs** says what hardware.

These replace the six M13 bullets in `PLAN.md` when M13 starts: "decide the signer hardware" is
M13.6, "study LIVI and decide" is this document, the `native/` directory is M13.0, "wired iAP2
identification and authentication" is M13.3 and M13.7, "AirPlay over NCM" is M13.4 and M13.8 to
M13.10, "wireless" is M13.12.

**M13.0 Seam and scaffolding.** Needs: nothing.
- [ ] `src/backend.h`; `DongleSession` and an empty `NativeSession` behind it; `CarPlayConfig.backend`
- [ ] With `backend: native` and nothing plugged in, the state goes to searching and says "No iPhone on USB." once
- [ ] `dev/scenarios.sh` still all PASS (the adapter path is untouched)
- [ ] NOTICE, LICENSE pointer, README attribution and the example's About entry (5.4)
- [ ] OpenSSL and avahi-client in the CMake build and in `tools/setup-dev-machine.sh --build-deps`

**M13.1 iAP2 codecs.** Needs: nothing.
- [ ] Link engine: every case in LIVI's `iap2-link/tests/engine.rs` (handshake both ways, retransmit until dead, window, delayed and cumulative acks, reorder, EAK, RST, resync) plus the derived SYN payloads for the Bluetooth and wired configurations
- [ ] Control session codec: all 33 vectors of `iap2-csm/tests/vectors.txt` round trip, and the `codec.rs` cases
- [ ] `tools/native-test.sh` prints OK

**M13.2 An iPhone over USB, up to the carkit service.** Needs: an iPhone and its cable.
- [ ] Find out on this machine how usbmuxd, `ipheth` and `cdc_ncm` behave when the phone is switched to configuration 6, and settle 6.4's first two rows; write the answer here and in `AGENTS.md`
- [ ] udev rule; the plugin opens the phone as a normal user, sends 0x52, the phone comes back with 6 or more configurations, configuration 6 is set
- [ ] usbmux version exchange and a mux TCP connection to lockdown 62078
- [ ] First run: the Trust dialog appears, the pair record is written to the app's data directory; second run: no dialog
- [ ] `StartService("com.apple.carkit.service")` answers a port and a TLS flag, and the service connection opens (with TLS if asked)
- [ ] Unplug mid way, replug: it recovers without a restart

**M13.3 iAP2 identification against a real phone.** Needs: an iPhone.
- [ ] Marker and link sync complete over the carkit stream
- [ ] `0x1D00`, our `0x1D01`, and `IdentificationAccepted 0x1D02` from the phone
- [ ] The phone's next message (`0xAA00` expected) is logged and, with no signer, the state says "Needs an MFi signer" rather than failing silently
- [ ] The captured exchange is saved as a new native test vector
- [ ] Find out whether the duplicated transport fields and the packed language list matter, by trying without them

**M13.4 The NCM link.** Needs: an iPhone.
- [ ] After configuration 6, a netdev for the phone's NCM function exists, has a fe80 address, DAD off (kernel `cdc_ncm` path)
- [ ] The phone appears in `ip -6 neigh` on it; a TCP listener on `[::]` is reachable on that address with its scope **[unconfirmed]** whether the phone talks on this link before CarPlay starts
- [ ] The TAP fallback (userspace NCM) gives the same result when `cdc_ncm` is unloaded

**M13.5 AirPlay codecs and a scripted phone.** Needs: nothing.
- [ ] bplist, TLV8, RTSP, SRP-6a 3072, HKDF, the control and media ciphers, auth-setup, the MFi 4.0 key schedule (with the RFC 4493 CMAC vector), HID descriptors and reports: every vector in `livi-cp/src/ts_vectors.json` and the unit tests named in 5.3
- [ ] Screen stream: a recorded config and frame decrypt and become Annex B the decoder accepts (`native/crates/livi-screen-stream/src/lib.rs` tests, `livi-video/src/nal.rs` tests)
- [ ] A port of the scripted phone in `livi-cp/src/stack/tests.rs` drives the C++ receiver from `/info` through pair-setup, pair-verify, SETUP and RECORD over a loopback socket
- [ ] `/info` for the example's config matches LIVI's structure key for key (its byte vector depends on LIVI's config, so compare structure, not hash)

**M13.6 The signer.** Needs: one of a LIVI Link dongle, or an MFi 3.0 (or 2.0C) chip on a CH341 module.
- [ ] Decide which signer to buy first and note it here (7.1)
- [ ] `LiviLinkSigner`: generation, certificate, a signature, from the dongle at `livi-link.local:5000`
- [ ] `Ch341` bus and `CoprocessorSigner`: probe at 0x10 and 0x11, generation from 0x02, certificate, signature, with the NACK retries
- [ ] `FileSigner` against a key and certificate generated in the test (never a real one, 4.7)
- [ ] The one shot check (4.6) passes on real hardware: the signature verifies against the certificate's key; the example's bench shows the signer's backend, generation and certificate size
- [ ] Unplugging a LIVI Link mid session gives "No MFi signer", replugging recovers

**M13.7 Wired authentication and metadata, before any picture.** Needs: an iPhone and a signer.
- [ ] `AuthenticationSucceeded 0xAA05`
- [ ] `PowerSourceUpdate`, the phone charges
- [ ] The five subscriptions accepted; now playing fills the example's card from iAP2, album art through file transfer
- [ ] A route in Apple Maps fills the turn card (maneuver codes compared with what the adapter's JSON uses)
- [ ] A call fills the call card, incoming and outgoing
- [ ] `0x4300 CarPlayAvailability` arrives and is logged

**M13.8 The AirPlay session.** Needs: an iPhone and a signer.
- [ ] `0x4301` with our fe80 address and port; the phone connects within LIVI's 8 s
- [ ] `/info`, pair-setup (first time), pair-verify, then the encrypted control channel
- [ ] auth-setup with a 2.0 or 3.0 signer, or MFi 4.0 when the phone asks and the signer is 3.0; note which the phone chose
- [ ] Session SETUP, stream SETUPs, RECORD; the phone shows CarPlay as connected
- [ ] Timing answered both ways; the session stays up for ten minutes
- [ ] TEARDOWN and unplug end it cleanly, and the next plug starts again without the Trust dialog

**M13.9 The picture and touch.** Needs: an iPhone and a signer.
- [ ] The screen stream decrypts; CarPlay's home screen in `CarPlayView` at the view's size, VA-API decoding, verified by screenshot
- [ ] `forceKeyFrame` through `SetKeyframeRequest` recovers a flushed decoder
- [ ] Touch lands where the finger is; two fingers zoom in Maps
- [ ] Wire to frame latency measured and compared with the adapter backend's
- [ ] Media keys and the knob through HID, night mode both ways, the car icon sends `CarPlayRequest.showHostUi`

**M13.10 Audio and the microphone.** Needs: an iPhone and a signer.
- [ ] `/info` offers PCM only at first; music plays on media at 48 kHz stereo, no underruns over ten minutes
- [ ] A Maps prompt plays on navigation and media ducks under it (`duckAudio`)
- [ ] Siri: the voice stream plays and Siri hears the microphone; `pactl list short source-outputs` is empty otherwise
- [ ] A call's audio both ways
- [ ] Decide whether to also offer AAC-LC (type 102) or Opus, from what the phone picks when offered

**M13.11 The rest of what the adapter backend offers.** Needs: an iPhone and a signer.
- [ ] Safe area and view area through `/info` displays
- [ ] The car's position: `0xFFFA` answered with `0xFFFB` sentences from `setLocation`, reusing `nmea.*`
- [ ] Siri held from the dock button (`requestSiri`)
- [ ] Instrument cluster: a type 111 display, its stream into the cluster decoder and texture

**M13.12 Wireless.** Needs: an iPhone, a signer, Bluetooth (this laptop's AX201), and an access point capable Wi-Fi interface on 5 GHz (or a LIVI Link's AP and Bluetooth).
- [ ] BlueZ profiles as in 2.4, the phone pairs and lists the car under CarPlay
- [ ] iAP2 over RFCOMM: identification with the Bluetooth and wireless CarPlay components, authentication
- [ ] `0x5703` with the machine's network, the phone joins, `0x4301` wireless, the AirPlay session runs over Wi-Fi
- [ ] `_airplay._tcp` published through avahi-client
- [ ] On the next start the phone reconnects without being touched
- [ ] Plugging the cable in moves the session to it (LIVI drops the Bluetooth session when the cable has the phone, `bringup.rs:570-574`)

**M13.13 Robustness.** Needs: as M13.12.
- [ ] Phone unplugged and replugged mid session; host app killed and restarted; signer lost and regained; phone locked; screen off
- [ ] A scripted phone over a socket pair (like android_auto's `AA_WIRELESS_FAKE_PHONE`) runs the iAP2 and AirPlay paths in `dev/scenarios.sh` without a phone, from the M13.5 harness

### 7.1 Shopping list

| Item | For | Notes |
|---|---|---|
| an iPhone and a data cable | everything from M13.2 | owned |
| **a signer, one of:** | M13.6 onwards | |
| a Carlinkit Mini Ultra 3 or C2Air (AX520 or V821B, AIC8800 Wi-Fi 6 and Bluetooth), reflashed as LIVI Link | signer, plus an AP and Bluetooth for M13.12 | LIVI's maintainer recommends these over the CCPA (`docs/research.md`); a dedicated unit keeps the CCPA for the adapter backend. Reflashing is at the owner's risk; the stock backup is kept (`LIVI-LINK.md`) |
| or the CCPA reflashed as LIVI Link | the same | only once the adapter backend no longer needs it, or restored to stock between uses with the provisioning tool's "back to the vendor firmware" |
| or an MFi 3.0 coprocessor (MFI343S00177) on a breakout, a CH341A USB to I2C module set to 3.3 V, jumper wires | signer only | chips are grey market or salvaged; LIVI's community PCB in discussion #346; "CP2.0 packages with CP3.0 dies" exist (`docs/research.md`). 3.0 rather than 2.0C, because MFi 4.0 mutual authentication needs 3.0 |
| a USB Wi-Fi adapter that does AP mode on 5 GHz with an in kernel driver (mt76 based, for instance) | M13.12 without a LIVI Link | the laptop's Intel card cannot beacon on most 5 GHz channels, and hosting an AP costs the laptop its own Wi-Fi (`../FlutterAndroidAuto/docs/wireless.md`) |

The cheapest complete set is one reflashed Mini Ultra 3 or C2Air: it signs, and it brings the
radios for wireless.

---

## 8. Risks and open questions

### 8.1 Risks

- **The phone's side changes.** Every value LIVI sends (the `/info` feature bits
  `0x615653aee2`, the Bonjour bits `0x44540380,0x61`, source version "950.7.1", the setup code
  "3939", the identification quirks) was found to work, not specified. An iOS update can change
  what is accepted; LIVI follows quickly, which is the best reason to keep the LIVI file map
  current.
- **Untrusted input in the host app's process.** The receiver parses iAP2, plists, TLV8, RTSP and
  media from a phone, and CarPlay receivers have had remote code execution bugs (Oligo's AirBorne
  and Pwn My Ride, `docs/research.md` section 4). Bounds on every length, no recursion without a
  depth limit in the bplist parser, and fuzzing the codecs in `native_test` are part of the work,
  not an afterthought.
- **Signer hardware.** MFi chips are only sold to licensees, so they are grey market or salvaged,
  of uncertain generation, and their certificates could in principle be revoked by Apple
  **[unconfirmed]** whether that has happened to any. A LIVI Link depends on LIVI's firmware and a
  dongle whose stock firmware may block reflashing.
- **The usbmuxd question** (2.3 step 2, 6.4) may force using the system usbmuxd on Linux, which
  splits the Linux and Android USB code.
- **Privilege creep.** If the kernel `cdc_ncm` route fails on some machines, the TAP fallback
  needs `CAP_NET_ADMIN`, which a Flutter app does not have; it would need a pre-made persistent TAP
  from the setup script or a helper.
- **Size.** About 15 thousand lines of Rust for wired. The milestone order puts visible results
  (metadata in M13.7) before the largest part (AirPlay) so progress is checkable.
- **Upstream drift** after the port, mitigated as in 5.3 and never eliminated.
- **The attribution term reaches host apps** (5.4): a host app author who misses it is out of
  licence. The automatic licence page entry is the main mitigation.
- **Wireless on desktops**: radio and regulatory limits, and the AP being the machine's job, as in
  android_auto.

### 8.2 Open questions

1. Does iOS accept authentication before identification as well as LIVI's order? (M13.3)
2. Are the duplicated transport component fields and the packed language list needed? (M13.3)
3. What does the system usbmuxd do when the phone is in configuration 6, and can libusb switch
   the configuration while `ipheth` or `cdc_ncm` hold interfaces? (M13.2)
4. Is interface 2 and 3 always the CarPlay NCM function in configuration 6, and does the phone
   answer on it before `0x4301`? (M13.4)
5. Is Bonjour needed at all for wired? (M13.8, by not publishing)
6. What are the screen stream header's other 123 bytes? Timestamps would help latency
   measurement. (M13.9)
7. Which audio formats does the phone pick when offered only PCM, and does it insist on a type
   102 stream? (M13.10)
8. Can a display change size within a session, or does a resize need a new session? (M13.9)
9. Is `public_key` in `0x4301` really meant to be the pairing id, as LIVI sends it?
   (`native/crates/livi-helper/src/lib.rs:56`)
10. Is DeviceTimeUpdate's time Unix seconds or seconds since 2001? (M13.7)
11. Which Class of Device does an iPhone want, `0x200418` or `0x200420`, and what does the Apple
    vendor element in the beacon mean? (M13.12)
12. Does CarPlay accept the head unit as a client of an existing network instead of its own AP?
    (M13.12)
13. CH341: the exact command stream and ACK reporting, checked against the datasheet before
    M13.6.
14. Does a 2.0C chip's paged certificate read in one go from 0x31? (M13.6, with a 2.0C chip only)
15. Will LIVI ever publish its protocol crates as a stable library? If so, 5.3 is worth revisiting.
