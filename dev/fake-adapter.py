#!/usr/bin/env python3
"""A fake Carlinkit CPC200-CCPA adapter, served through Linux FunctionFS.

Emulates the head unit facing side of the "Auto Box" adapter (USB 1314:1520) closely
enough that the plugin's real libusb code path can be run end to end with no hardware:
it answers Open the way the adapter does, pretends an iPhone connected over USB, and then
streams a moving H.264 test picture, optional media and navigation audio, now playing and
navigation metadata, and the microphone and Siri commands. Every number on the wire (types,
payload layouts, command ids, packet sizes, timings) comes from docs/dongle-protocol.md.

It is not run directly. dev/fake-adapter.sh mounts FunctionFS, starts this program
against the mount, and binds the gadget to a dummy_hcd UDC once the endpoints exist.

FunctionFS files used: ep0 gets the descriptors and strings and then delivers events
(BIND, ENABLE, DISABLE, UNBIND, SETUP), ep1 is the bulk IN endpoint (adapter to host) and
ep2 the bulk OUT endpoint (host to adapter).

Standard library only, meant for `python3 -I`. The only outside program is ffmpeg, for
the video and the album art.
"""

import argparse
import errno
import json
import math
import os
import queue
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time

# ---- protocol constants (docs/dongle-protocol.md sections 2 and 3) ----

MAGIC = 0x55AA55AA
HEADER = struct.Struct("<IIII")
MAX_PAYLOAD = 1024 * 1024

T_OPEN = 0x01
T_PLUGGED = 0x02
T_PHASE = 0x03
T_UNPLUGGED = 0x04
T_TOUCH = 0x05
T_VIDEO = 0x06
T_AUDIO = 0x07
T_COMMAND = 0x08
T_BT_ADDRESS = 0x0A
T_FRAME = 0x0C
T_BT_NAME = 0x0D
T_WIFI_NAME = 0x0E
T_DISCONNECT_PHONE = 0x0F
T_CLOSE_DONGLE = 0x15
T_MULTITOUCH = 0x17
T_BOX_SETTINGS = 0x19
T_GNSS = 0x29
T_MEDIA = 0x2A
T_CLUSTER_VIDEO = 0x2C
T_SEND_FILE = 0x99
T_BOX_CONFIG = 0xA2
T_HEARTBEAT = 0xAA
T_SOFTWARE_VERSION = 0xCC

TYPE_NAMES = {
    0x01: "Open", 0x02: "Plugged", 0x03: "Phase", 0x04: "Unplugged", 0x05: "Touch",
    0x06: "VideoData", 0x07: "AudioData", 0x08: "Command", 0x09: "LogoType",
    0x0A: "BluetoothAddress", 0x0B: "CarPlayModeChange", 0x0C: "Frame",
    0x0D: "BluetoothDeviceName", 0x0E: "WifiDeviceName", 0x0F: "DisconnectPhone",
    0x11: "AutoConnectByBluetoothAddress", 0x12: "BluetoothPairedList",
    0x15: "CloseDongle", 0x17: "MultiTouch", 0x19: "BoxSettings", 0x22: "ForgetBluetoothAddr",
    0x29: "GnssData", 0x2A: "MediaData", 0x2C: "ClusterVideoData", 0x6E: "NaviFocusRequest", 0x6F: "NaviFocusRelease",
    0x77: "FactorySetting", 0x88: "DebugTest", 0x99: "SendFile", 0xA0: "AppInfo",
    0xA2: "BoxConfig", 0xAA: "HeartBeat", 0xCC: "SoftwareVersion", 0xCD: "Reboot",
    0xCE: "ResetUSB", 0xF0: "EnableCrypt", 0xFD: "DisplayReady",
}

COMMAND_NAMES = {
    1: "StartRecordMic", 2: "StopRecordMic", 3: "RequestHostUI", 5: "SiriButtonDown",
    6: "SiriButtonUp", 7: "UseCarMic", 8: "UseBoxMic", 12: "RequestKeyFrame",
    14: "Hide", 15: "UseBoxI2SMic", 16: "StartNightMode", 17: "StopNightMode",
    18: "StartGNSSReport", 19: "StopGNSSReport", 21: "UsePhoneMic",
    22: "UseBluetoothAudio", 23: "UseBoxTransAudio", 24: "Use24GWiFi", 25: "Use5GWiFi",
    26: "RefreshFrame", 100: "ButtonLeft", 101: "ButtonRight", 102: "ButtonUp",
    103: "ButtonDown", 104: "ButtonSelectDown", 105: "ButtonSelectUp", 106: "ButtonBack",
    111: "KnobLeft", 112: "KnobRight", 200: "Home", 201: "Play", 202: "Pause",
    203: "PlayOrPause", 204: "Next", 205: "Prev", 300: "PhoneAnswer", 301: "PhoneHungUp",
    500: "RequestVideoFocus", 501: "ReleaseVideoFocus", 508: "RequestNaviScreenFocus",
    509: "ReleaseNaviScreenFocus", 1000: "SupportWifi", 1001: "SupportAutoConnect",
    1002: "StartAutoConnect", 1012: "WiFiPair", 1013: "GetBluetoothOnlineList",
}

# Audio (section 6).
AUDIO_HEADER = struct.Struct("<IfI")
DT_MEDIA_48K_STEREO = 4
DT_VOICE_16K_MONO = 5
AT_MAIN = 1
AT_NAVI = 2
AT_MIC = 3
AC_OUTPUT_START = 1
AC_INPUT_CONFIG = 3
AC_NAVI_START = 6
AC_NAVI_STOP = 7
AC_SIRI_START = 8
AC_SIRI_STOP = 9

# What the fake says about itself (section 3.7, values of a Rev B A15W).
SOFTWARE_VERSION = "2025.02.25.1521CAY"
BOX_NAME = "FakeAutoBox"
BT_ADDRESS = "00:11:22:33:44:55"
BOX_INFO = {"uuid": "fake", "MFD": "20240119", "boxType": "YA", "productType": "A15W",
            "hwVersion": "YMA0-WN16-0003", "WiFiChannel": 36}
PHONE_INFO = {"MDLinkType": "CarPlay", "MDModel": "iPhone16,1", "MDOSVersion": "23A341",
              "btMacAddr": "AA:BB:CC:DD:EE:FF"}

# ---- FunctionFS (include/uapi/linux/usb/functionfs.h) ----

FFS_DESCRIPTORS_MAGIC_V2 = 3
FFS_STRINGS_MAGIC = 2
FFS_HAS_FS_DESC = 1
FFS_HAS_HS_DESC = 2
FFS_EVENT_NAMES = {0: "BIND", 1: "UNBIND", 2: "ENABLE", 3: "DISABLE", 4: "SETUP",
                   5: "SUSPEND", 6: "RESUME"}
FFS_EVENT_SIZE = 12


def ffs_descriptors():
    # Interface 0: vendor class 0xFF, subclass 0xF0, protocol 0, two endpoints, as
    # measured on a CCPA (section 1.2). iInterface 1 is the one string below.
    interface = struct.pack("<BBBBBBBBB", 9, 4, 0, 0, 2, 0xFF, 0xF0, 0x00, 1)

    def endpoint(address, max_packet):
        # The 7 byte endpoint descriptor: bulk, no interval.
        return struct.pack("<BBBBHB", 7, 5, address, 0x02, max_packet, 0)

    # Descriptor order decides the file names: the first endpoint is ep1, the second ep2.
    full_speed = interface + endpoint(0x81, 64) + endpoint(0x02, 64)
    high_speed = interface + endpoint(0x81, 512) + endpoint(0x02, 512)
    body = struct.pack("<II", 3, 3) + full_speed + high_speed
    return struct.pack("<III", FFS_DESCRIPTORS_MAGIC_V2, 12 + len(body),
                       FFS_HAS_FS_DESC | FFS_HAS_HS_DESC) + body


def ffs_strings():
    body = struct.pack("<H", 0x0409) + b"Auto Box\0"
    return struct.pack("<IIII", FFS_STRINGS_MAGIC, 16 + len(body), 1, 1) + body


# ---- helpers ----

def log(text):
    now = time.time()
    stamp = time.strftime("%H:%M:%S", time.localtime(now)) + ".%03d" % int(now * 1000 % 1000)
    sys.stdout.write("%s %s\n" % (stamp, text))
    sys.stdout.flush()


def encode(msg_type, payload=b""):
    return HEADER.pack(MAGIC, len(payload), msg_type, msg_type ^ 0xFFFFFFFF) + payload


def u32(value):
    return struct.pack("<I", value)


def audio_header(decode_type, audio_type, volume=0.0):
    return AUDIO_HEADER.pack(decode_type, volume, audio_type)


def sine_table(frequency, rate, channels, gain, seconds=1.0):
    """s16le PCM of a sine that loops seamlessly when the frequency fits the length."""
    frames = int(rate * seconds)
    amplitude = int(32767 * max(0.0, min(1.0, gain)))
    out = bytearray()
    for i in range(frames):
        sample = int(amplitude * math.sin(2 * math.pi * frequency * i / rate))
        out += struct.pack("<h", sample) * channels
    return bytes(out)


def text_preview(data, limit=80):
    stripped = data.rstrip(b"\0")
    try:
        text = stripped.decode("ascii")
    except UnicodeDecodeError:
        return "%d bytes binary" % len(data)
    if not text.isprintable():
        return "%d bytes binary" % len(data)
    if len(text) > limit:
        text = text[:limit] + "..."
    return repr(text)


def sleep_while(alive, seconds):
    """Sleeps in small steps. False as soon as alive() is, True after the full time."""
    end = time.monotonic() + seconds
    while True:
        if not alive():
            return False
        left = end - time.monotonic()
        if left <= 0:
            return True
        time.sleep(min(0.05, left))


def find_start_code(buf, start):
    """Offset of the next Annex B start code at or after `start`, including a leading
    zero byte if it is a four byte one. -1 if none. Emulation prevention guarantees
    00 00 01 never occurs inside a NAL unit."""
    pos = buf.find(b"\x00\x00\x01", start)
    if pos > start and buf[pos - 1] == 0:
        return pos - 1
    return pos


def nal_types(au):
    types = []
    pos = find_start_code(au, 0)
    while pos >= 0:
        header = au.find(b"\x00\x00\x01", pos) + 3
        if header < len(au):
            types.append(au[header] & 0x1F)
        pos = find_start_code(au, header)
    return types


def strip_aud(au):
    """The real adapter sends no access unit delimiter, so it is cut off the front."""
    first = au.find(b"\x00\x00\x01")
    if first < 0 or first + 3 >= len(au) or (au[first + 3] & 0x1F) != 9:
        return au
    nxt = find_start_code(au, first + 4)
    return au[nxt:] if nxt > 0 else au


# ---- the fake ----

class FakeAdapter:
    def __init__(self, args):
        self.args = args
        self.lock = threading.RLock()
        self.write_queue = queue.Queue()
        self.children = set()

        # Bumped on every USB ENABLE and DISABLE, so work queued for a link that went
        # away is dropped rather than written into the next one.
        self.link_gen = 0
        self.enabled = False
        # Bumped on Open, CloseDongle and link loss: everything a session started
        # checks it and stops once it has moved on.
        self.session_gen = 0
        self.opened = False
        self.open_params = None
        # Bumped on every plug and unplug.
        self.phone_gen = 0
        self.phone = False
        self.keyframe_wanted = threading.Event()

        self.heartbeats = 0
        self.last_heartbeat = None
        self.heartbeat_warned = False
        self.mic_bytes = 0
        self.mic_packets = 0
        self.mic_decode_type = None
        self.mic_active = False
        self.stats_lock = threading.Lock()
        self.album_art = None
        self.phase13_sent = False
        # From the host's BoxSettings, and the 508 handshake's progress for this phone.
        self.navi_screen = None
        self.cluster_awaiting_echo = False
        self.cluster_streaming_gen = None

    # -- USB plumbing --

    def start(self):
        ffs = self.args.ffs
        self.ep0 = os.open(os.path.join(ffs, "ep0"), os.O_RDWR)
        os.write(self.ep0, ffs_descriptors())
        os.write(self.ep0, ffs_strings())
        # The endpoint files only exist once ep0 has both.
        self.ep_in = os.open(os.path.join(ffs, "ep1"), os.O_RDWR)
        self.ep_out = os.open(os.path.join(ffs, "ep2"), os.O_RDWR)
        log("descriptors written, ep1 bulk IN, ep2 bulk OUT; waiting for the gadget to be "
            "bound and the host to configure it")
        if self.args.pidfile:
            with open(self.args.pidfile, "w") as handle:
                handle.write("%d\n" % os.getpid())
        for target in (self.reader, self.writer, self.watchdog, self.mic_stats):
            threading.Thread(target=target, daemon=True).start()
        threading.Thread(target=self.make_album_art, daemon=True).start()
        self.ep0_loop()

    def ep0_loop(self):
        while True:
            try:
                data = os.read(self.ep0, FFS_EVENT_SIZE * 4)
            except OSError as error:
                if error.errno == errno.EINTR:
                    continue
                log("ep0 read failed: %s" % error)
                time.sleep(0.5)
                continue
            for offset in range(0, len(data) - FFS_EVENT_SIZE + 1, FFS_EVENT_SIZE):
                event = data[offset:offset + FFS_EVENT_SIZE]
                self.on_ep0_event(event[8], event[:8])

    def on_ep0_event(self, kind, setup):
        name = FFS_EVENT_NAMES.get(kind, str(kind))
        if kind == 4:
            request_type, request, value, index, length = struct.unpack("<BBHHH", setup)
            log("ep0 SETUP type 0x%02x request 0x%02x value 0x%04x index %d length %d, "
                "stalled" % (request_type, request, value, index, length))
            # FunctionFS stalls a request when ep0 is used in the wrong direction.
            try:
                if request_type & 0x80:
                    os.read(self.ep0, 0)
                else:
                    os.write(self.ep0, b"")
            except OSError:
                pass
            return
        log("ep0 %s" % name)
        if kind == 2:
            with self.lock:
                self.link_gen += 1
                self.enabled = True
                self.last_heartbeat = time.monotonic()
                self.heartbeat_warned = False
                self.heartbeats = 0
            log("USB link up: the host configured the gadget, waiting for Open")
        elif kind in (1, 3):
            with self.lock:
                was = self.enabled
                self.enabled = False
                self.link_gen += 1
                self.end_session("USB %s" % name)
            if was:
                log("USB link down (%s): streams stopped, waiting for the host again" % name)

    def send(self, msg_type, payload=b"", quiet=False, pgen=None):
        """Queues one message for ep1. Dropped if the link changes before it goes out,
        and with `pgen`, also if that phone has left by then."""
        with self.lock:
            if not self.enabled:
                return False
            gen = self.link_gen
        self.write_queue.put((gen, pgen, encode(msg_type, payload)))
        if not quiet:
            log("-> %s, %d bytes" % (TYPE_NAMES.get(msg_type, "0x%02X" % msg_type), len(payload)))
        return True

    def writer(self):
        while True:
            gen, pgen, data = self.write_queue.get()
            if gen != self.link_gen or not self.enabled:
                continue
            if pgen is not None and pgen != self.phone_gen:
                continue
            view = memoryview(data)
            try:
                while view:
                    written = os.write(self.ep_in, view)
                    view = view[written:]
            except OSError as error:
                log("write to ep1 failed (%s), message dropped" % os.strerror(error.errno))

    def reader(self):
        buf = bytearray()
        seen_gen = -1
        skipped = 0
        while True:
            try:
                data = os.read(self.ep_out, 65536)
            except OSError as error:
                if error.errno not in (errno.EINTR, errno.EAGAIN):
                    # ESHUTDOWN or ECONNRESET while the link goes down: wait for the
                    # next one.
                    time.sleep(0.05)
                continue
            if not data:
                continue
            if self.link_gen != seen_gen:
                seen_gen = self.link_gen
                buf.clear()
            buf += data
            while len(buf) >= HEADER.size:
                magic, length, msg_type, check = HEADER.unpack_from(buf, 0)
                if magic != MAGIC or check != msg_type ^ 0xFFFFFFFF or length > MAX_PAYLOAD:
                    # Out of step: scan forward for the next magic.
                    nxt = buf.find(b"\xaa\x55\xaa\x55", 1)
                    drop = nxt if nxt > 0 else max(1, len(buf) - 3)
                    skipped += drop
                    del buf[:drop]
                    log("bad header, skipped %d byte(s) so far" % skipped)
                    continue
                if len(buf) < HEADER.size + length:
                    break
                payload = bytes(buf[HEADER.size:HEADER.size + length])
                del buf[:HEADER.size + length]
                try:
                    self.on_message(msg_type, payload)
                except Exception as error:  # A handler bug must not kill the reader.
                    log("handler for type 0x%02X failed: %r" % (msg_type, error))

    # -- inbound messages --

    def on_message(self, msg_type, payload):
        name = TYPE_NAMES.get(msg_type, "0x%02X" % msg_type)
        size = len(payload)
        if msg_type == T_HEARTBEAT:
            with self.lock:
                self.heartbeats += 1
                if self.heartbeat_warned:
                    log("heartbeat back after %.1f s" % (time.monotonic() - self.last_heartbeat))
                self.last_heartbeat = time.monotonic()
                self.heartbeat_warned = False
                count = self.heartbeats
            log("<- HeartBeat #%d" % count)
            return
        if msg_type == T_AUDIO and size >= 12:
            decode_type, volume, audio_type = AUDIO_HEADER.unpack_from(payload, 0)
            if audio_type == AT_MIC and size > 13:
                with self.stats_lock:
                    if self.mic_packets == 0:
                        log("<- AudioData microphone, decode type %d, %d bytes (first packet; "
                            "the rate is logged once a second)" % (decode_type, size - 12))
                    self.mic_bytes += size - 12
                    self.mic_packets += 1
                    self.mic_decode_type = decode_type
                return
            log("<- AudioData, %d bytes, decode type %d, audio type %d, %d byte(s) after the "
                "header" % (size, decode_type, audio_type, size - 12))
            return

        if msg_type == T_OPEN and size >= 28:
            fields = struct.unpack_from("<7I", payload, 0)
            log("<- Open, %d bytes: width %d, height %d, fps %d, format %d, packetMax %d, "
                "boxVersion %d, workMode %d" % ((size,) + fields))
            self.on_open(payload[:28], fields)
        elif msg_type == T_COMMAND and size >= 4:
            command = struct.unpack_from("<I", payload, 0)[0]
            log("<- Command %d (%s), %d bytes" % (command, COMMAND_NAMES.get(command, "?"), size))
            if command == 12:
                self.request_keyframe("Command 12")
            elif command == 508:
                self.on_cluster_focus()
        elif msg_type == T_FRAME and size == 0:
            log("<- Frame (0x0C), 0 bytes")
            self.request_keyframe("Frame message")
        elif msg_type == T_SEND_FILE:
            log("<- SendFile, %d bytes: %s" % (size, self.describe_send_file(payload)))
        elif msg_type in (T_BOX_SETTINGS, T_BOX_CONFIG):
            text = payload.rstrip(b"\0").decode("utf-8", "replace")
            log("<- %s, %d bytes: %s" % (name, size, text))
            try:
                navi = json.loads(text).get("naviScreenInfo")
            except ValueError:
                navi = None
            if isinstance(navi, dict) and navi.get("width") and navi.get("height"):
                self.navi_screen = navi
                log("the host has a cluster screen: %dx%d" % (navi["width"], navi["height"]))
        elif msg_type == T_MULTITOUCH:
            points = []
            for offset in range(0, size - 15, 16):
                x, y, action, ident = struct.unpack_from("<ffII", payload, offset)
                points.append("id %d %s (%.3f, %.3f)" % (
                    ident, {0: "up", 1: "down", 2: "move"}.get(action, str(action)), x, y))
            log("<- MultiTouch, %d bytes: %s" % (size, "; ".join(points)))
        elif msg_type == T_TOUCH and size >= 16:
            action, x, y, flags = struct.unpack_from("<IIII", payload, 0)
            log("<- Touch, %d bytes: action %d at (%d, %d), flags %d" % (size, action, x, y, flags))
        elif msg_type == T_DISCONNECT_PHONE:
            log("<- DisconnectPhone, %d bytes" % size)
            self.on_disconnect_phone()
        elif msg_type == T_CLOSE_DONGLE:
            log("<- CloseDongle, %d bytes" % size)
            with self.lock:
                had = self.opened
                self.end_session("CloseDongle")
            if had:
                log("session closed, waiting for a new Open")
        elif msg_type == T_GNSS:
            text = payload.decode("ascii", "replace").strip().replace("\r\n", " | ")
            log("<- GnssData, %d bytes: %s" % (size, text))
        else:
            log("<- %s, %d bytes%s" % (name, size,
                                       (": " + text_preview(payload)) if size else ""))

    @staticmethod
    def describe_send_file(payload):
        if len(payload) < 4:
            return "truncated"
        name_len = struct.unpack_from("<I", payload, 0)[0]
        path = payload[4:4 + name_len].rstrip(b"\0").decode("ascii", "replace")
        offset = 4 + name_len
        if len(payload) < offset + 4:
            return "%s, truncated" % path
        content_len = struct.unpack_from("<I", payload, offset)[0]
        content = payload[offset + 4:offset + 4 + content_len]
        if content_len == 4:
            value = "u32 %d" % struct.unpack("<I", content)[0]
        elif content_len % 4 == 0 and 4 < content_len <= 32 and path.startswith("/etc/RiddleBoxData/"):
            value = "u32 " + ", ".join(str(v) for v in struct.unpack("<%dI" % (content_len // 4), content))
        else:
            value = text_preview(content)
        return "%s = %s" % (path, value)

    # -- session --

    def end_session(self, why):
        """Stops whatever a session started. Caller holds the lock."""
        if self.opened or self.phone:
            log("ending the session (%s)" % why)
        self.session_gen += 1
        self.opened = False
        self.phone_gen += 1
        self.phone = False

    def on_open(self, raw, fields):
        with self.lock:
            # The real adapter keeps a phone session across a host that went away without
            # DisconnectPhone or CloseDongle, and ignores the next Open: no echo, no
            # Plugged, no keyframe (docs/dongle-protocol.md 8.2, quirk 6). The phone keeps
            # streaming P frames into the void.
            if self.args.sticky_session and self.phone:
                log("--sticky-session: a phone session is up, so this Open is ignored, as "
                    "the real adapter does")
                return
            if self.opened:
                self.end_session("a new Open")
            self.session_gen += 1
            sgen = self.session_gen
            self.opened = True
            self.open_params = fields
        # The adapter's answer: the same 28 bytes back, then what it says about itself
        # (section 4.3).
        self.send(T_OPEN, raw)
        self.send(T_SOFTWARE_VERSION, SOFTWARE_VERSION.encode().ljust(32, b"\0"))
        self.send(T_BT_NAME, BOX_NAME.encode())
        self.send(T_WIFI_NAME, BOX_NAME.encode())
        self.send(T_BT_ADDRESS, BT_ADDRESS.encode())
        self.send(T_BOX_SETTINGS, json.dumps(BOX_INFO, separators=(",", ":")).encode())
        self.send(T_COMMAND, u32(1000))
        if self.args.no_plug:
            log("--no-plug: no phone will connect")
            return
        log("a phone connects in %.1f s" % self.args.plug_delay)
        self.later(self.args.plug_delay, lambda: self.session_gen == sgen and self.enabled,
                   lambda: self.plug(sgen))

    def later(self, seconds, alive, action):
        def run():
            if sleep_while(alive, seconds) and alive():
                action()
        threading.Thread(target=run, daemon=True).start()

    def plug(self, sgen):
        with self.lock:
            if self.session_gen != sgen or self.phone or not self.enabled:
                return
            self.phone_gen += 1
            pgen = self.phone_gen
            self.phone = True
        log("an iPhone connects over USB")
        self.send(T_BOX_SETTINGS, json.dumps(PHONE_INFO, separators=(",", ":")).encode())
        self.send(T_PLUGGED, u32(3) + u32(0))
        self.send(T_PHASE, u32(7))
        if self.args.phase13_once and not self.phase13_sent:
            self.phase13_sent = True
            # AirPlay negotiation failing, which the real adapter reports when the view or
            # safe area is larger than the Open size. It then sits there until told to
            # drop the phone.
            log("--phase13-once: the negotiation fails with Phase 13")
            self.send(T_PHASE, u32(13))
            return
        threading.Thread(target=self.phone_session, args=(sgen, pgen), daemon=True).start()

    def phone_alive(self, pgen):
        return self.phone_gen == pgen and self.enabled

    def phone_session(self, sgen, pgen):
        alive = lambda: self.phone_alive(pgen)
        if not sleep_while(alive, 1.0):
            return
        self.send(T_COMMAND, u32(500))
        self.send(T_PHASE, u32(8))
        log("streaming")
        args = self.args
        workers = [self.video]
        if args.audio:
            workers.append(self.media_audio)
        if args.nav_prompts:
            workers.append(self.nav_prompts)
        if not args.no_metadata:
            workers.append(self.metadata)
        if args.mic_after is not None:
            workers.append(self.mic_test)
        if args.siri_after is not None:
            workers.append(self.siri_test)
        for worker in workers:
            threading.Thread(target=worker, args=(pgen,), daemon=True).start()
        if args.unplug_after is not None:
            self.later(args.unplug_after, alive, lambda: self.simulate_unplug(sgen, pgen))
        if not args.no_cluster_offer:
            self.later(2.0, alive, lambda: self.offer_cluster(pgen))
        if args.gnss:
            # The phone wants the car's position, which the adapter says with Command 18
            # (StartGNSSReport). The host answers with GnssData, logged as it arrives.
            self.later(1.0, alive, lambda: self.send(T_COMMAND, u32(18), pgen=pgen))

    def unplug(self, why):
        with self.lock:
            if not self.phone:
                return False
            self.phone_gen += 1
            self.phone = False
        log("the iPhone disconnects (%s)" % why)
        self.send(T_UNPLUGGED)
        return True

    def simulate_unplug(self, sgen, pgen):
        if not self.phone_alive(pgen) or not self.unplug("--unplug-after"):
            return
        log("it comes back in %.1f s" % self.args.replug_after)
        self.later(self.args.replug_after, lambda: self.session_gen == sgen and self.enabled,
                   lambda: self.plug(sgen))

    def on_disconnect_phone(self):
        with self.lock:
            sgen = self.session_gen
            opened = self.opened
        if not self.unplug("host DisconnectPhone"):
            return
        # A wired iPhone is still on the cable, so it comes back unless the host goes on
        # to close the session.
        if opened and not self.args.no_plug:
            self.later(self.args.plug_delay, lambda: self.session_gen == sgen and self.enabled,
                       lambda: self.plug(sgen))

    def offer_cluster(self, pgen):
        # Wired CarPlay: the adapter starts the handshake (docs/dongle-protocol.md 5.6).
        if self.navi_screen is None or not self.phone_alive(pgen):
            return
        with self.lock:
            self.cluster_awaiting_echo = True
        log("-> Command 508, offering the cluster stream")
        self.send(T_COMMAND, u32(508), pgen=pgen)

    def on_cluster_focus(self):
        with self.lock:
            pgen = self.phone_gen
            streaming = self.cluster_streaming_gen == pgen
            awaiting = self.cluster_awaiting_echo
            ready = self.phone and self.navi_screen is not None
        if not ready or streaming:
            return
        if awaiting:
            with self.lock:
                self.cluster_awaiting_echo = False
                self.cluster_streaming_gen = pgen
            log("the host echoed 508, the cluster stream starts")
            threading.Thread(target=self.cluster_video, args=(pgen,), daemon=True).start()
        else:
            # The host started it, as it has to over Wi-Fi: answer, and wait for its echo.
            with self.lock:
                self.cluster_awaiting_echo = True
            log("-> Command 508, answering the host's")
            self.send(T_COMMAND, u32(508), pgen=pgen)

    def request_keyframe(self, why):
        with self.lock:
            streaming = self.phone
        if streaming:
            log("keyframe requested (%s): restarting the encoder" % why)
            self.keyframe_wanted.set()
        else:
            log("keyframe requested (%s) with no phone streaming, ignored" % why)

    # -- video (section 5) --

    def ffmpeg_command(self, width, height, fps, offset, label="FAKE CARPLAY",
                       source_name="testsrc2"):
        args = self.args
        source = "%s=size=%dx%d:rate=%d" % (source_name, width, height, fps)
        command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
                   "-f", "lavfi", "-i", source]
        if os.path.exists(args.font):
            size = max(16, height // 14)
            # The frame number keeps counting across encoder restarts, so a keyframe
            # request is visible as a jump nowhere but in the log.
            text = "%s  %%{localtime\\:%%T}  frame %%{eif\\:n+%d\\:d}" % (label, offset)
            draw = ("drawtext=fontfile=%s:text='%s':fontsize=%d:fontcolor=white:"
                    "box=1:boxcolor=black@0.6:boxborderw=8:x=(w-tw)/2:y=h/6"
                    % (args.font, text, size))
            command += ["-vf", draw]
        command += ["-c:v", "libx264", "-preset", args.x264_preset, "-profile:v", "high",
                    "-tune", "zerolatency", "-pix_fmt", "yuv420p",
                    "-x264-params", "keyint=%d:aud=1:sliced-threads=0" % args.keyint,
                    "-bsf:v", "h264_mp4toannexb", "-f", "h264", "-"]
        return command

    def spawn(self, command):
        proc = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=None)
        with self.lock:
            self.children.add(proc)
        return proc

    def reap(self, proc):
        try:
            proc.kill()
        except OSError:
            pass
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass
        if proc.stdout:
            proc.stdout.close()
        with self.lock:
            self.children.discard(proc)

    @staticmethod
    def access_units(stream):
        """Yields one Annex B access unit at a time, split on the AUD x264 puts first."""
        buf = bytearray()
        start = -1
        scan = 0
        fd = stream.fileno()
        while True:
            chunk = os.read(fd, 65536)
            if not chunk:
                if start >= 0 and len(buf) > start:
                    yield bytes(buf[start:])
                return
            buf += chunk
            while True:
                pos = buf.find(b"\x00\x00\x01", scan)
                if pos < 0 or pos + 3 >= len(buf):
                    scan = max(0, len(buf) - 3)
                    break
                scan = pos + 3
                if buf[pos + 3] & 0x1F != 9:
                    continue
                begin = pos - 1 if pos > 0 and buf[pos - 1] == 0 else pos
                if start >= 0:
                    yield bytes(buf[start:begin])
                del buf[:begin]
                start = 0
                scan = 4

    def video(self, pgen):
        params = self.open_params
        self.stream_video(pgen, T_VIDEO, (params[0] // 2) * 2, (params[1] // 2) * 2,
                          self.args.fps, "FAKE CARPLAY", 3, "testsrc2", "video")

    def cluster_video(self, pgen):
        info = self.navi_screen
        # The cluster stream is a separate encoder at its own size and a low rate, as the
        # real one is (docs/dongle-protocol.md 5.6), with encoder state 1.
        self.stream_video(pgen, T_CLUSTER_VIDEO, (info["width"] // 2) * 2,
                          (info["height"] // 2) * 2, min(10, info.get("fps", 10)),
                          "FAKE CLUSTER", 1, "smptebars", "cluster")

    def stream_video(self, pgen, msg_type, width, height, fps, label, state, source, name):
        restartable = msg_type == T_VIDEO
        sent = 0
        started = time.monotonic()
        first = True
        while self.phone_alive(pgen):
            if restartable:
                self.keyframe_wanted.clear()
            command = self.ffmpeg_command(width, height, fps, sent, label, source)
            log("%s: %dx%d at %d fps, encoder started" % (name, width, height, fps))
            proc = self.spawn(command)
            restart = False
            try:
                for au in self.access_units(proc.stdout):
                    if not self.phone_alive(pgen):
                        break
                    au = strip_aud(au)
                    due = started + sent / fps
                    delay = due - time.monotonic()
                    if delay > 0:
                        time.sleep(delay)
                    elif delay < -1.0:
                        # Far behind (the machine was busy): start the clock again.
                        started = time.monotonic() - sent / fps
                    # Back pressure: wait for the writer rather than queue frames up.
                    while self.write_queue.qsize() > 8 and self.phone_alive(pgen):
                        time.sleep(0.005)
                    header = struct.pack("<5I", width, height, state, int(sent * 1000 / fps), 0)
                    self.send(msg_type, header + au, quiet=True, pgen=pgen)
                    if first:
                        first = False
                        log("-> %s, first frame, %d bytes, NAL types %s" % (
                            TYPE_NAMES.get(msg_type, msg_type), len(au) + 20, nal_types(au)))
                    sent += 1
                    if sent % (fps * 10) == 0:
                        log("%s: %d frames sent" % (name, sent))
                    if restartable and self.keyframe_wanted.is_set():
                        restart = True
                        break
            finally:
                self.reap(proc)
            if restart and self.phone_alive(pgen):
                first = True
                continue
            if self.phone_alive(pgen):
                log("%s: the encoder ended on its own, restarting it" % name)
                time.sleep(0.5)
        log("%s: stopped after %d frames" % (name, sent))

    # -- audio (section 6) --

    def pcm_loop(self, pgen, table, audio_type, packet_bytes, packet_seconds, until=None):
        """Sends `table` cyclically in packets at real time pace while the phone is there
        (and, with `until`, until that monotonic time)."""
        offset = 0
        started = time.monotonic()
        count = 0
        while self.phone_alive(pgen) and (until is None or time.monotonic() < until):
            chunk = table[offset:offset + packet_bytes]
            if len(chunk) < packet_bytes:
                chunk += table[:packet_bytes - len(chunk)]
            offset = (offset + packet_bytes) % len(table)
            self.send(T_AUDIO, audio_header(DT_MEDIA_48K_STEREO, audio_type) + chunk, quiet=True,
                      pgen=pgen)
            count += 1
            delay = started + count * packet_seconds - time.monotonic()
            if delay > 0:
                time.sleep(delay)
        return count

    def media_audio(self, pgen):
        # 60 ms of 48 kHz stereo s16le is 11520 bytes, as on the real adapter (6.6).
        table = sine_table(440, 48000, 2, self.args.audio_gain)
        self.send(T_AUDIO, audio_header(DT_MEDIA_48K_STEREO, AT_MAIN) + bytes([AC_OUTPUT_START]))
        log("audio: 440 Hz media on audio type 1, 11520 bytes every 60 ms")
        count = self.pcm_loop(pgen, table, AT_MAIN, 11520, 0.06)
        log("audio: stopped after %d packets" % count)

    def nav_prompts(self, pgen):
        table = sine_table(880, 48000, 2, self.args.audio_gain)
        alive = lambda: self.phone_alive(pgen)
        while sleep_while(alive, self.args.nav_prompt_every):
            log("audio: navigation prompt on audio type 2")
            self.send(T_AUDIO, audio_header(DT_MEDIA_48K_STEREO, AT_NAVI) + bytes([AC_NAVI_START]))
            self.pcm_loop(pgen, table, AT_NAVI, 11520, 0.06, until=time.monotonic() + 1.2)
            self.send(T_AUDIO, audio_header(DT_MEDIA_48K_STEREO, AT_NAVI) + bytes([AC_NAVI_STOP]))

    # -- metadata (section 3.9) --

    def make_album_art(self):
        command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-f", "lavfi",
                   "-i", "testsrc2=size=300x300:rate=1", "-frames:v", "1",
                   "-pix_fmt", "yuvj420p", "-c:v", "mjpeg", "-f", "image2pipe", "-"]
        try:
            self.album_art = subprocess.run(command, stdout=subprocess.PIPE, check=True,
                                            timeout=20).stdout
        except (OSError, subprocess.SubprocessError) as error:
            log("album art: ffmpeg failed (%s), none will be sent" % error)

    def metadata(self, pgen):
        alive = lambda: self.phone_alive(pgen)
        started = time.monotonic()
        distance = 800
        sent_art = False
        while alive():
            play_ms = int((time.monotonic() - started) * 1000) % 180000
            now_playing = {"MediaSongName": "Fake Song", "MediaArtistName": "Fake Artist",
                           "MediaAlbumName": "Fake Album", "MediaAPPName": "Music",
                           "MediaSongDuration": 180000, "MediaSongPlayTime": play_ms,
                           "MediaPlayStatus": 1}
            self.send(T_MEDIA, u32(1) + json.dumps(now_playing, separators=(",", ":")).encode()
                      + b"\0", quiet=True, pgen=pgen)
            if not sent_art and self.album_art:
                sent_art = True
                self.send(T_MEDIA, u32(3) + self.album_art, quiet=True, pgen=pgen)
                log("-> MediaData album art, %d byte JPEG" % len(self.album_art))
            navigation = {"NaviStatus": 1, "NaviManeuverType": 2, "NaviRemainDistance": distance,
                          "NaviRoadName": "Fake Street", "NaviDestinationName": "Home"}
            self.send(T_MEDIA, u32(200) + json.dumps(navigation, separators=(",", ":")).encode(),
                      quiet=True, pgen=pgen)
            log("-> MediaData now playing at %d ms, navigation %d m to the turn"
                % (play_ms, distance))
            distance = distance - 50 if distance > 50 else 800
            sleep_while(alive, self.args.meta_every)

    # -- microphone (section 6.7) --

    def mic_test(self, pgen):
        alive = lambda: self.phone_alive(pgen)
        if not sleep_while(alive, self.args.mic_after):
            return
        log("microphone: asking the host to record (Command 1)")
        self.send(T_COMMAND, u32(1))
        if sleep_while(alive, self.args.mic_for):
            log("microphone: asking the host to stop (Command 2)")
            self.send(T_COMMAND, u32(2))

    def siri_test(self, pgen):
        alive = lambda: self.phone_alive(pgen)
        if not sleep_while(alive, self.args.siri_after):
            return
        log("Siri: SIRI_START, INPUT_CONFIG 16 kHz, Command 1")
        self.send(T_AUDIO, audio_header(DT_VOICE_16K_MONO, AT_MAIN) + bytes([AC_SIRI_START]))
        self.send(T_AUDIO, audio_header(DT_VOICE_16K_MONO, AT_MAIN) + bytes([AC_INPUT_CONFIG]))
        self.send(T_COMMAND, u32(1))
        if sleep_while(alive, self.args.mic_for):
            log("Siri: Command 2, SIRI_STOP")
            self.send(T_COMMAND, u32(2))
            self.send(T_AUDIO, audio_header(DT_VOICE_16K_MONO, AT_MAIN) + bytes([AC_SIRI_STOP]))

    def mic_stats(self):
        while True:
            time.sleep(1.0)
            with self.stats_lock:
                got, packets = self.mic_bytes, self.mic_packets
                self.mic_bytes = 0
                decode_type = self.mic_decode_type
                if got == 0:
                    if self.mic_active:
                        log("microphone: no more data from the host")
                    self.mic_active = False
                    self.mic_packets = 0
                    continue
                self.mic_active = True
            log("microphone: %d bytes/s from the host, decode type %s, %d packets so far"
                % (got, decode_type, packets))

    # -- heartbeat watchdog (section 3.8) --

    def watchdog(self):
        while True:
            time.sleep(1.0)
            with self.lock:
                if not self.enabled or self.last_heartbeat is None or self.heartbeat_warned:
                    continue
                silent = time.monotonic() - self.last_heartbeat
                if silent > 10.0:
                    self.heartbeat_warned = True
                    log("WARNING: no heartbeat for %.1f s. The real adapter resets the link at "
                        "about 15 s" % silent)

    def shutdown(self):
        with self.lock:
            children = list(self.children)
        for proc in children:
            try:
                proc.kill()
            except OSError:
                pass


def main():
    parser = argparse.ArgumentParser(
        description="Fake Carlinkit adapter on FunctionFS. Started by dev/fake-adapter.sh.")
    parser.add_argument("--ffs", required=True, help="the FunctionFS mount point")
    parser.add_argument("--pidfile", help="write this process's pid here once ready")
    parser.add_argument("--plug-delay", type=float, default=2.0,
                        help="seconds from Open to the fake iPhone connecting (default 2)")
    parser.add_argument("--no-plug", action="store_true", help="never connect a phone")
    parser.add_argument("--sticky-session", action="store_true",
                        help="ignore Open while a phone session is up, until the host sends "
                             "DisconnectPhone or CloseDongle, as a real CCPA does after its "
                             "host restarted")
    parser.add_argument("--no-cluster-offer", action="store_true",
                        help="never start the cluster handshake, as over Wi-Fi, so the host "
                             "has to")
    parser.add_argument("--gnss", action="store_true",
                        help="once streaming, ask the host for the car's position "
                             "(Command 18)")
    parser.add_argument("--phase13-once", action="store_true",
                        help="fail the first phone negotiation with Phase 13")
    parser.add_argument("--fps", type=int, default=30, help="video frame rate (default 30)")
    parser.add_argument("--keyint", type=int, default=900,
                        help="frames between IDRs, CarPlay sends very few (default 900)")
    parser.add_argument("--x264-preset", default="veryfast", help="libx264 preset")
    parser.add_argument("--font", default="/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        help="font for the clock drawn on the video, skipped if missing")
    parser.add_argument("--audio", action="store_true",
                        help="stream a 440 Hz sine as media audio (audio type 1)")
    parser.add_argument("--audio-gain", type=float, default=0.1,
                        help="sine amplitude 0..1 (default 0.1)")
    parser.add_argument("--nav-prompts", action="store_true",
                        help="a navigation prompt on audio type 2 every --nav-prompt-every s")
    parser.add_argument("--nav-prompt-every", type=float, default=20.0)
    parser.add_argument("--no-metadata", action="store_true",
                        help="send no now playing, album art or navigation data")
    parser.add_argument("--meta-every", type=float, default=3.0,
                        help="seconds between metadata updates (default 3)")
    parser.add_argument("--mic-after", type=float,
                        help="send Command 1 (StartRecordMic) this many s into streaming")
    parser.add_argument("--siri-after", type=float,
                        help="send SIRI_START, INPUT_CONFIG and Command 1 this many s into "
                             "streaming")
    parser.add_argument("--mic-for", type=float, default=5.0,
                        help="seconds before the matching stop (Command 2, SIRI_STOP)")
    parser.add_argument("--unplug-after", type=float,
                        help="the phone leaves this many s into streaming (repeats after "
                             "every re-plug)")
    parser.add_argument("--replug-after", type=float, default=5.0,
                        help="seconds before a phone that left comes back (default 5)")
    args = parser.parse_args()

    if shutil.which("ffmpeg") is None:
        log("ffmpeg is not installed, there will be no video")
    fake = FakeAdapter(args)

    def stop(signum, frame):
        log("signal %d, exiting" % signum)
        fake.shutdown()
        os._exit(0)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    log("fake Carlinkit adapter, pid %d, options %s" % (os.getpid(), vars(args)))
    try:
        fake.start()
    except OSError as error:
        log("FunctionFS at %s failed: %s" % (args.ffs, error))
        fake.shutdown()
        sys.exit(1)


if __name__ == "__main__":
    main()
