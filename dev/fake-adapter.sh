#!/usr/bin/env bash
# A fake Carlinkit CPC200-CCPA adapter on this machine, made of software, so the plugin's
# real libusb code path can be tested end to end with no adapter on the desk.
#
#   dev/fake-adapter.sh setup [-- options]   build, load and start it (options go to
#                                            dev/fake-adapter.py, see --help there)
#   dev/fake-adapter.sh teardown             undo every part of setup
#   dev/fake-adapter.sh status               what is loaded, bound and running
#   dev/fake-adapter.sh log [-f]             the fake's log, /tmp/cp-fake-adapter.log
#
# How: the Linux USB gadget stack can present a device to the same machine through
# dummy_hcd, a virtual host controller wired straight to a virtual device controller.
# A configfs gadget with the adapter's identity (1314:1520, "Magic Communication Tec.",
# "Auto Box") gets one FunctionFS function, whose vendor interface and two bulk endpoints
# are served from user space by dev/fake-adapter.py. The plugin finds it with lsusb like
# the real thing.
#
# Needs sudo, and a kernel with its headers installed. Ubuntu does not ship dummy_hcd, so
# setup downloads dummy_hcd.c for the running kernel's version from git.kernel.org, builds
# it out of tree under ~/.cache/carplay-fake-adapter and loads that self built module
# (unsigned, so the kernel is tainted until the next reboot; that is harmless).
#
# Everything setup changes is temporary: the gadget, the FunctionFS mount, the modules and
# a udev rule in /run/udev/rules.d that lets a normal user open the fake. teardown removes
# all of it, and so does a reboot.
#
# Running setup again while it is up restarts the Python side with new options, which is
# the way to switch on audio or the unplug test: setup -- --audio --unplug-after 20

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/carplay-fake-adapter"
KERNEL="$(uname -r)"
MODULE_DIR="$CACHE/dummy_hcd-$KERNEL"
MODULE="$MODULE_DIR/dummy_hcd.ko"
GADGET=/sys/kernel/config/usb_gadget/cp_fake_adapter
FFS_NAME=cp_fake_adapter
FFS_MOUNT=/run/cp-fake-adapter/ffs
UDEV_RULE=/run/udev/rules.d/70-cp-fake-adapter.rules
LOG=/tmp/cp-fake-adapter.log
PIDFILE="$CACHE/fake-adapter.pid"

die() { echo "fake-adapter.sh: $*" >&2; exit 1; }
say() { echo "fake-adapter.sh: $*"; }
# Writes one configfs attribute as root.
put() { echo "$2" | sudo tee "$1" >/dev/null; }
# Not `lsmod | grep -q`: grep quits early, lsmod dies of SIGPIPE, and pipefail turns
# that into "not loaded".
loaded() { grep -q "^$1 " /proc/modules; }

require_sudo() {
  sudo -n true 2>/dev/null && return 0
  [ -t 0 ] || die "sudo needs a password and there is no terminal to ask on. Run 'sudo -v' first."
  sudo -v || die "could not get root"
}

# The pid of the running dev/fake-adapter.py, or nothing. Checked against the process's
# command line, because a stale pid file may name an unrelated process by now.
python_pid() {
  local pid
  [ -f "$PIDFILE" ] || return 0
  pid="$(cat "$PIDFILE")"
  if [ -n "$pid" ] && tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null | grep -q "fake-adapter.py"; then
    echo "$pid"
  fi
}

stop_python() {
  local pid i
  pid="$(python_pid)"
  if [ -n "$pid" ]; then
    kill "$pid" 2>/dev/null || true
    for i in $(seq 1 30); do
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.1
    done
    kill -9 "$pid" 2>/dev/null || true
    say "stopped the gadget program (pid $pid)"
  fi
  rm -f "$PIDFILE"
}

build_module() {
  if [ -f "$MODULE" ] && [ "$(modinfo -F vermagic "$MODULE" | cut -d' ' -f1)" = "$KERNEL" ]; then
    return 0
  fi
  [ -d "/lib/modules/$KERNEL/build" ] || die "no kernel headers in /lib/modules/$KERNEL/build.
  Install them: sudo apt install linux-headers-$KERNEL"
  # Upstream tags are vMAJOR.MINOR for the first release of a series, which is what the
  # distribution kernel is based on. dummy_hcd.c hardly ever changes within a series.
  local tag url
  tag="v$(echo "$KERNEL" | cut -d. -f1-2)"
  url="https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain/drivers/usb/gadget/udc/dummy_hcd.c?h=$tag"
  mkdir -p "$MODULE_DIR"
  say "downloading dummy_hcd.c ($tag)"
  curl -fsSL -o "$MODULE_DIR/dummy_hcd.c" "$url" || die "could not download $url"
  echo 'obj-m := dummy_hcd.o' >"$MODULE_DIR/Kbuild"
  say "building dummy_hcd for $KERNEL"
  make -C "/lib/modules/$KERNEL/build" M="$MODULE_DIR" modules >"$MODULE_DIR/build.log" 2>&1 \
    || die "the build failed, see $MODULE_DIR/build.log"
}

load_modules() {
  sudo modprobe libcomposite
  sudo modprobe usb_f_fs
  if ! loaded dummy_hcd; then
    # insmod resolves no dependencies, but udc_core came in with libcomposite.
    sudo insmod "$MODULE"
  fi
}

udc_name() {
  local udc
  for udc in /sys/class/udc/dummy_udc.*; do
    [ -e "$udc" ] && { basename "$udc"; return 0; }
  done
  return 1
}

create_gadget() {
  [ -d "$GADGET" ] && return 0
  sudo mkdir "$GADGET"
  put "$GADGET/idVendor" 0x1314
  put "$GADGET/idProduct" 0x1520
  put "$GADGET/bcdUSB" 0x0200
  # bcdDevice 4.09 and the placeholder serial are what stock units report.
  put "$GADGET/bcdDevice" 0x0409
  sudo mkdir -p "$GADGET/strings/0x409"
  put "$GADGET/strings/0x409/manufacturer" "Magic Communication Tec."
  put "$GADGET/strings/0x409/product" "Auto Box"
  put "$GADGET/strings/0x409/serialnumber" "0123456789FEDCBA"
  sudo mkdir -p "$GADGET/configs/c.1/strings/0x409"
  put "$GADGET/configs/c.1/strings/0x409/configuration" "android_accessory"
  put "$GADGET/configs/c.1/MaxPower" 500
  # High speed at most: the adapter is a USB 2.0 device, and FunctionFS gets no
  # SuperSpeed descriptors from the program.
  put "$GADGET/max_speed" high-speed
  sudo mkdir "$GADGET/functions/ffs.$FFS_NAME"
  sudo ln -s "$GADGET/functions/ffs.$FFS_NAME" "$GADGET/configs/c.1/"
}

mount_ffs() {
  mountpoint -q "$FFS_MOUNT" && return 0
  sudo mkdir -p "$FFS_MOUNT"
  # Owned by this user, so the Python side runs without root.
  sudo mount -t functionfs -o "uid=$(id -u),gid=$(id -g)" "$FFS_NAME" "$FFS_MOUNT"
}

install_udev_rule() {
  [ -f "$UDEV_RULE" ] && return 0
  sudo mkdir -p "$(dirname "$UDEV_RULE")"
  echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1314", ATTR{idProduct}=="152*", MODE="0666", TAG+="uaccess"' \
    | sudo tee "$UDEV_RULE" >/dev/null
  sudo udevadm control --reload
}

unbind() {
  if [ -f "$GADGET/UDC" ] && [ -n "$(cat "$GADGET/UDC")" ]; then
    put "$GADGET/UDC" ""
  fi
}

cmd_setup() {
  [ "${1:-}" = "--" ] && shift
  command -v ffmpeg >/dev/null || die "ffmpeg is needed for the video"
  require_sudo
  build_module
  load_modules
  create_gadget
  mount_ffs
  install_udev_rule

  # A second setup restarts the program with the options given now.
  unbind
  stop_python

  : >"$LOG"
  mkdir -p "$CACHE"
  setsid python3 -I "$REPO/dev/fake-adapter.py" --ffs "$FFS_MOUNT" --pidfile "$PIDFILE" "$@" \
    >>"$LOG" 2>&1 </dev/null &

  # The endpoint files appear once the program has written its descriptors, and the
  # gadget can only be bound after that.
  local i
  for i in $(seq 1 50); do
    [ -e "$FFS_MOUNT/ep2" ] && [ -n "$(python_pid)" ] && break
    sleep 0.1
  done
  [ -e "$FFS_MOUNT/ep2" ] || { tail -n 20 "$LOG" >&2; die "the gadget program did not start"; }

  local udc
  udc="$(udc_name)" || die "no dummy_udc in /sys/class/udc, is dummy_hcd loaded?"
  put "$GADGET/UDC" "$udc"
  for i in $(seq 1 50); do
    lsusb -d 1314:1520 >/dev/null 2>&1 && break
    sleep 0.1
  done
  lsusb -d 1314:1520 || die "bound to $udc, but the fake did not show up on USB"
  say "up on $udc, program pid $(python_pid), log $LOG"
}

cmd_teardown() {
  require_sudo
  [ -d "$GADGET" ] && unbind
  stop_python
  if mountpoint -q "$FFS_MOUNT"; then
    sudo umount "$FFS_MOUNT"
  fi
  sudo rmdir "$FFS_MOUNT" "$(dirname "$FFS_MOUNT")" 2>/dev/null || true
  if [ -d "$GADGET" ]; then
    # configfs wants everything removed in the reverse order of creation.
    sudo rm -f "$GADGET/configs/c.1/ffs.$FFS_NAME"
    sudo rmdir "$GADGET/configs/c.1/strings/0x409" "$GADGET/configs/c.1" \
      "$GADGET/functions/ffs.$FFS_NAME" "$GADGET/strings/0x409" "$GADGET"
    say "gadget removed"
  fi
  if loaded dummy_hcd; then
    sudo rmmod dummy_hcd
  fi
  # Only if nothing else on the machine uses the gadget stack.
  if [ -z "$(ls -A /sys/kernel/config/usb_gadget 2>/dev/null)" ]; then
    loaded usb_f_fs && sudo rmmod usb_f_fs
    loaded libcomposite && sudo rmmod libcomposite
  else
    say "other gadgets exist, leaving libcomposite and usb_f_fs loaded"
  fi
  if [ -f "$UDEV_RULE" ]; then
    sudo rm -f "$UDEV_RULE"
    sudo udevadm control --reload
  fi
  say "torn down"
}

cmd_status() {
  local m pid
  for m in dummy_hcd libcomposite usb_f_fs; do
    if loaded "$m"; then echo "module $m: loaded"; else echo "module $m: not loaded"; fi
  done
  if [ -d "$GADGET" ]; then
    echo "gadget: $GADGET, UDC '$(cat "$GADGET/UDC" 2>/dev/null)'"
  else
    echo "gadget: none"
  fi
  if mountpoint -q "$FFS_MOUNT"; then echo "functionfs: mounted at $FFS_MOUNT"; else echo "functionfs: not mounted"; fi
  if [ -f "$UDEV_RULE" ]; then echo "udev rule: $UDEV_RULE"; else echo "udev rule: none"; fi
  pid="$(python_pid)"
  if [ -n "$pid" ]; then echo "program: running, pid $pid"; else echo "program: not running"; fi
  echo "usb: $(lsusb -d 1314:1520 2>/dev/null || echo 'no 1314:1520 device')"
  if [ -f "$LOG" ]; then
    echo "last log lines ($LOG):"
    tail -n 5 "$LOG"
  fi
}

cmd_log() {
  [ -f "$LOG" ] || die "no log at $LOG"
  if [ "${1:-}" = "-f" ]; then
    tail -n 60 -f "$LOG"
  else
    tail -n 60 "$LOG"
  fi
}

case "${1:-}" in
  setup)    shift; cmd_setup "$@" ;;
  teardown) cmd_teardown ;;
  status)   cmd_status ;;
  log)      shift; cmd_log "$@" ;;
  *)        echo "usage: $0 setup [-- options] | teardown | status | log [-f]" >&2; exit 2 ;;
esac
