# Research: CarPlay on a Linux head unit, and why an adapter first

A dated record of what was known on **2026-11-05**, before any code was written, and of the
decision taken from it. Read it for why the project is shaped the way it is, never for what the
code does now: that is `AGENTS.md`, `PLAN.md` and the code itself. The adapter's protocol has its
own reference, `docs/dongle-protocol.md`.

## The decision

**One plugin, with the CarPlay source behind a seam, and two backends planned:**

1. **A stock Carlinkit CPC200-CCPA adapter, first.** The adapter is the CarPlay receiver: it does
   Apple's authentication, the Wi-Fi and the Bluetooth, and hands the host H.264 and PCM over a
   small, well documented USB protocol. Buildable and testable before the hardware arrives, and
   the quickest route to a working CarPlay. It is `packages/carplay_native/src/dongle/`.
2. **Native CarPlay, later.** iAP2 and AirPlay implemented on the host, the way LIVI did in 2026,
   with only the authentication signature coming from hardware (an MFi chip, or a cheap adapter
   reflashed as a signer). This is what gives the same depth as android_auto: turn by turn and
   now playing straight from iAP2, the host's own Wi-Fi and Bluetooth, lower latency, no vendor
   firmware in the video path. A milestone in `PLAN.md`.

Everything above the backend is shared: the texture, the decoder, the three audio streams, the
microphone, the Dart API. So the second backend is a new directory beside `dongle/`, not a new
plugin, and a host app does not change.

The author's brief was the most flexibility and the deepest integration, like android_auto, with
the cost of hardware no object. The adapter first and native later serves both: it projects soon,
and a CPC200-CCPA can itself be reflashed into a signer for the native backend later (see "LIVI
Link" below), so the first purchase is not wasted by the second step.

**Licence: GPL-3.0-or-later**, confirmed by the author on 2026-11-05, the same as android_auto.
Nothing here links aasdk, so a permissive licence was possible; the uniform licence keeps code
moving freely between the two repositories, and it also makes LIVI's GPL code usable for the
native backend (with LIVI's attribution term).

**Linux now, Android later**, confirmed the same day. The seams android_auto uses for Android
(decoder, present adapter, audio, USB from Java) exist here unchanged, so Android is one file per
seam when it comes.

## What the research found

### Summary

- **The belief "CarPlay needs extra hardware" was true until about September 2026 and is now only true for a clean, legitimate setup.** The iPhone always authenticates the head unit with an Apple-signed accessory certificate plus a challenge signature. Normally that key lives in an MFi authentication coprocessor. That part has not changed.
- **What changed in 2026:** an accessory identity (certificate + private key) was recovered from public Carlinkit firmware and is now bundled in software-only receivers (DiPlay, xcertplay forks, PlayPort and many others). These do wired and wireless CarPlay with **no dongle and no chip**, and were still being accepted by iOS 27 in October 2026. It is a shared, extractable, revocable key of unclear legal status.
- **The open source reference project moved on from "dongle does everything".** LIVI (the successor of react-carplay, Linux, GPL-3.0) now implements CarPlay natively (iAP2 + AirPlay, wired over plain USB and wireless over its own Wi-Fi AP + BlueZ) since 2026-07-15, and **removed the Carlinkit projection path on 2026-09-06**. It still needs an MFi coprocessor, either on I2C or a cheap reflashed dongle ("LIVI Link") used only as an auth oracle and optional radio.
- **CPC200-CCPA is no longer the best buy.** LIVI's maintainer calls it EOL, its newer stock firmware (2025.10.15.1127) breaks reflashing, its i.MX6 has secure boot (HAB) enabled, and he recommends the cheaper Carlinkit Mini Ultra 3 (Axera AX520) or the hardware identical "C2Air" (Allwinner V821B) and rebrands instead, for the LIVI Link role.

### 1. Does CarPlay really need an MFi chip?

| Claim | Source (date) |
|---|---|
| CarPlay (wired USB and wireless) is licensed only through the MFi program; specs, hardware components (auth coprocessors) and certification tools are NDA only. | https://mfi.apple.com/en/how-it-works , https://mfi.apple.com/faqs (accessed 2026-11-05) |
| Since 2014 the CarPlay spec requires an authentication chip to block unofficial products. | https://9to5mac.com/2014/12/11/apple-carplay-mfi-standards/ (2014-12-11) |
| CarPlay uses iAP2 for authentication/control and AirPlay for media. "To launch a sophisticated attack, such as extracting the private key, an actor would need physical access to the MFi chip." (Hannah Noettgen, TROOPERS24 "Apple CarPlay: What's Under the Hood") | https://9to5mac.com/2024/11/07/security-bite-mechanics-of-apple-carplay/ (2024-11-07), https://troopers.de/troopers24/talks/8mcykg (June 2024) |
| Wireless CarPlay: iAP2 over Bluetooth negotiates Wi-Fi credentials, then AirPlay over Wi-Fi. Authentication is one way: the phone authenticates the head unit, "the head-unit does not authenticate the phone". | Oligo "Pwn My Ride", DEF CON 2025: https://www.oligo.security/blog/pwn-my-ride-exploring-the-carplay-attack-surface (2025-09-09) |
| The phone requests the accessory certificate, sends a challenge, and the accessory signs it with the matching private key (PKCS#8 EC P-256 key + accessory certificate). "Without these two files the server exits at startup, and there is no software workaround." | PlayPort README/wiki: https://github.com/youcci/playport , https://github.com/youcci/playport/wiki (2026) |
| LIVI: "CarPlay requires the accessory to authenticate against the phone. The usual way is an Apple MFi authentication coprocessor, a hardware chip that cannot be emulated and that LIVI neither ships nor bypasses." | https://github.com/f-io/LIVI (README, accessed 2026-11-05) |
| USB-C iPhones did not remove the receiver-side MFi requirement (only cable restrictions). | HN thread: https://news.ycombinator.com/item?id=43956804 (2025-05-11) |

So: yes, the phone verifies an Apple-rooted certificate chain and a live signature. You cannot invent your own key.

### 2. Ways people have got around the chip (2025 to 2026)

#### 2a. Leaked Carlinkit accessory identity (software only, works on Linux/Android)

- **DiPlay** (Android head units, BYD focus, GPL-3.0, created 2026-09-24, about 13.3k stars): "The APK bundles an experimental accessory identity recovered from public Carlinkit firmware, not a newly provisioned MFi identity for DiPlay. A bundled private key is extractable." Wired and wireless. Release notes mention testers on **iOS 27**. Keys are excluded from the git repo, only in the release APK. https://github.com/shihabal3amri/DiPlay (README accessed 2026-11-05). Write-up: https://www.besthub.dev/articles/diplay-open-source-carplay-receiver-for-byd-infotainment-no-dongle-required-3493141e1967 (2026-10-04)
- **xcertplay** (Android/AAOS CarPlay host, GPL-3.0, created 2026-09-12, about 1.6k stars, DiPlay's protocol core). Auth options: MFi chip via CH341 USB-I2C bridge or native `/dev/i2c-N`, local cert/key files, or "Remote MFi" service. https://github.com/shilapi/xcertplay
- **PlayPort** (macOS server, browser client, wireless only): does not support a physical chip; you supply the key pair from the DiPlay APK, Carlinkit C2Air firmware (Allwinner V821), your own MFi credentials, or a remote MFi server. Warns "Apple can revoke it in any iOS update", personal experimentation only, do not redistribute the key. https://github.com/youcci/playport/wiki
- Many forks appeared in October 2026 (ChairPlay for Windows, RustCarPlay, nexuscp "embedded offline MFi", PlayThing on Spotify Car Thing, an ESP32-S3 receiver, a PS Vita receiver). GitHub search "carplay receiver", accessed 2026-11-05.
- No report found (as of 2026-11-05) of Apple revoking this identity yet.

#### 2b. Apple "BAA" certificates (only on Apple hardware)

- Apple's CarPlay Simulator on a Mac has no auth coprocessor; it presents a certificate from Apple's internal BAA CA instead. A jailbroken iPhone/iPad can obtain such a proof and act as a wireless head unit. **Showcase** (jailbroken iOS, GPL-3.0-or-later): https://github.com/amineross/showcase , write-up by Amine Rostane: https://blog-8pl.pages.dev/articles/showcase/ (2026-04-15)
- LIVI: "does not support Apple's Basic Accessory Authentication on MacOS, but may in the future." Not usable on a generic Linux or Android box.

#### 2c. Real MFi chip wired to I2C (no dongle, but still "extra hardware")

- LIVI config keys `carPlayMfiI2cBus` (default 2) and `carPlayMfiPowerGpio` (default 21). https://github.com/f-io/LIVI
- Community PCB for an MFI337S3959 (CP2.0) on Pi 4/5, plus a design for MFI343S00177 (CP3.0). Maintainer notes "there are CP2.0 packages on the grey market with CP3.0 dies inside". https://github.com/f-io/LIVI/discussions/346 (2026-09-20)
- xcertplay supports chip over CH341 bridge or native I2C (see above). Older reference: BertoldVdb WACResearch (Go, MFi chip on Pi Zero over I2C GPIO overlays): https://github.com/BertoldVdb/WACResearch
- Chips are only sold to MFi licensees, so in practice they are grey market or salvaged from cheap adapters.

#### 2d. Reflashed dongle as an auth oracle ("LIVI Link")

- A CarPlay dongle is reflashed into a network accessory that provides MFi signing over the network, plus optionally its Wi-Fi AP and Bluetooth (vhci). Supported SoCs: NXP i.MX6UL (several Wi-Fi modules), Allwinner V821B, Axera AX520CE. The projection itself runs natively on the host. https://github.com/f-io/LIVI/blob/main/LIVI-LINK.md (accessed 2026-11-05)
- Timeline from LIVI commits: native wired + wireless CarPlay merged 2026-07-15 (PR #303); "LIVI Link" 2026-09-06; "refactor(projection)!: remove the dongle projection path, keep the uploader" 2026-09-06. Latest release v10.4.0 on 2026-11-04. https://github.com/f-io/LIVI

### 3. Newer iOS / CarPlay Ultra

- No public evidence that iOS 17/18/26/27 or CarPlay Ultra changed the accessory authentication model. CarPlay Ultra launched 2025-05-15 on Aston Martin (iPhone 12+, iOS 18.5+), Hyundai/Kia expected 2H 2026. https://techcrunch.com/2025/05/15/apple-finally-launches-next-gen-carplay-ultra-software-starting-with-aston-martin , https://bgr.com/2091835/apple-carplay-ultra-coming-kia-hyundai-2026/
- LIVI already advertises "Native Apple CarPlay Ultra" features (cluster video, H.265, climate/radio apps) on top of the same MFi auth; xcertplay has untested CarPlay Ultra triggering. Sources above.
- iOS 26 added AirPlay video while parked in CarPlay (only on supported cars). https://9to5mac.com/2026/05/06/carplay-video-ios-26/ (2026-05-06)

### 4. Security research worth knowing (not an MFi bypass)

- Oligo "AirBorne" (2025-04-29): 23 AirPlay protocol/SDK bugs, 17 CVEs, incl. CVE-2025-24132 in the AirPlay SDK used by CarPlay head units. https://www.securityweek.com/remote-carplay-hack-puts-drivers-at-risk-of-distraction-and-surveillance/
- Oligo "Pwn My Ride" (DEF CON 2025, post 2025-09-09): iAP2 is one way, head units accept any client. https://www.oligo.security/blog/pwn-my-ride-exploring-the-carplay-attack-surface ; Heise: BMW will not patch https://heise.de/-11135791
- USENIX VehicleSec 2025 "CarPlay at Risk" on third-party adapters: https://www.usenix.org/conference/vehiclesec25/presentation/won
- Relevance for you: if you implement the receiver side natively, you are the one parsing untrusted iAP2/AirPlay input, so harden it.

### 5. Dongles that expose projection to a host (stock firmware)

- **CPC200-CCPA** = Carlinkit "wireless CarPlay / Android Auto adapter for Android head units" (works with the AutoKit app; the successor of the CPC200-Autokit). It acts as the CarPlay receiver itself and streams video to the host over USB with the Carlinkit protocol. This is the opposite of the U2W/U2AW/Mini/Ultra family, which turn a car's factory wired CarPlay into wireless (they act as an iPhone towards the car). https://carlinkitcarplay.com/products/carlinkit-wirless-adapter-for-android-head-unit-support-carplay-and-android-auto , Hudiy forum: https://hudiy.eu/community/viewtopic.php?p=337
- **react-carplay / node-carplay** supports "Carlinkit (cpc200-ccpa or cpc200-ccpm)". https://github.com/rhysmorgan134/react-carplay
- **pycarplay / carplay-receiver** targets the older "Autobox" dongles (2020). https://github.com/harrylepotter/carplay-receiver
- **LIVI** (formerly listed CPC200-CCPA and CPC200-CCPW) dropped that path on 2026-09-06 (see 2d).
- Protocol RE references: https://github.com/ludwig-v/wireless-carplay-dongle-reverse-engineering (updated 2026-07-23), open replacement firmware for the CCPA: https://github.com/lvalen91/ocbm (2026).

**Maintainer's view on the CCPA (2026-09/10):** "The i.MX6 has HAB enabled ... The CPC200-CCPA is EOL now (afaik), so an ultra3, or the hardware-wise identical CPC-c2air and the other rebrands, are probably the better and cheaper option." https://github.com/f-io/LIVI/issues/347 . Note that the Mini Ultra 3 / C2Air are only useful after reflashing (LIVI Link), not as stock projection boxes.

### 6. Risks with dongles

- **Firmware updates change behaviour.** CCPA stock firmware 2025.10.15.1127 grabs the USB gadget back and breaks LIVI Link provisioning; downgrading to 2025.02.25.1521 fixed it. https://github.com/f-io/LIVI/issues/347 , https://github.com/f-io/LIVI/issues/366 (2026-09/10). MacRumors users report Carlinkit updates that bricked units, blocked firmware modding, or were meant for a clone (Joyeauto). https://forums.macrumors.com/threads/carplay-adapter-to-convert-to-wireless-carplay.2193978/page-178
- **Recommendation:** pin a known good firmware, do not use the vendor web UI/AutoKit "update" button, and keep the dongle off the internet. node-carplay style hosts also upload assets to the dongle at every boot, so behaviour depends on what the host sends.
- **Hardware variants / clones:** "countless hardware and software variants out there", same model name with different SoCs and Wi-Fi chips; reflashing can brick (maintainer desoldered SPI flash several times). https://github.com/f-io/LIVI/issues/366

### 7. Legal / licensing angle (not legal advice)

- Implementing iAP2/AirPlay receiver protocols from reverse engineering for interoperability is what LIVI, xcertplay and DiPlay do openly under GPL-3.0; none show a takedown so far.
- The risky part is **distributing a third-party private key**. DiPlay keeps it out of git and only ships it in the APK with a disclaimer; PlayPort refuses to ship it at all ("third-party key material", "must not be redistributed"). The key belongs to Carlinkit's MFi identity; using it may breach Apple/Carlinkit terms and Apple can revoke it.
- Trademarks: do not call the product "CarPlay certified"; all these projects add an "independent, not affiliated with Apple" notice.
- Parallel you already live with: aasdk ships a hardcoded Android Auto head unit certificate and private key in `Cryptor.cpp` (https://github.com/f1xpl/aasdk , https://github.com/opencardev/aasdk).
- If you copy code from LIVI/xcertplay/DiPlay, your app inherits GPL-3.0 (DiPlay UI parts are AGPL-3.0).

### 8. Practical recommendation for a Flutter Linux/Android head unit

1. **Lowest effort, legitimate:** stock-firmware Carlinkit receiver dongle (CCPA style) and talk its USB protocol. Works today, but the leading project just abandoned this path, the CCPA is reportedly EOL, and you are at the mercy of vendor firmware.
2. **Best long term (what LIVI did):** implement CarPlay natively in your app (iAP2 over USB NCM and Bluetooth RFCOMM, AirPlay screen stream, wireless via hostapd/BlueZ), with a pluggable "signer" interface:
   - I2C MFi coprocessor (Pi or board with I2C, or CH341 USB-I2C bridge on any PC/Android),
   - remote signer such as a reflashed cheap dongle (LIVI Link protocol),
   - optional user-supplied cert/key file (lets users try the leaked identity at their own risk without you distributing it).
3. **Software only (no hardware at all):** technically possible today via the leaked Carlinkit identity, but shared, revocable, and legally gray. Do not bundle the key in your repo or releases.

## Checked by hand on the same day

The claims the decision rests on were checked against the repositories themselves rather than
taken from a summary:

- LIVI (`f-io/LIVI`, GPL-3.0, about 1k stars) describes itself as a "native CarPlay Ultra & Android
  Auto head unit for Linux, macOS and Android", its CarPlay stack is the `livi-cp` crate (about 21
  thousand lines of Rust) with `iap2-*` crates beside it, and its commits from September to November
  2026 are LIVI Link firmware for i.MX6UL, Allwinner V821B and Axera AX520 adapters.
- LIVI issues #347 and #366 (September and October 2026) are CPC200-CCPA owners provisioning LIVI
  Link: it fails on stock firmware 2025.10.15.1127 and works after a downgrade to 2025.02.25.1521,
  or with the provisioning tool's later builds. So a CCPA can serve both backends, though flashing
  it is at the owner's risk.
- DiPlay and xcertplay exist as described, both GPL-3.0, created in September 2026.

## What this project will not do

- **Bundle or fetch a leaked accessory identity.** It is one shared key, extractable, revocable by
  any iOS update, and not ours to distribute. If the native backend ever takes a certificate and
  key from a file, the file is the user's to provide, as PlayPort does.
- **Present itself as certified.** The plugin is independent and not affiliated with Apple, and
  says so in the README.
