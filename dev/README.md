# Not part of the plugin

Everything in this directory drives the machine this plugin was written on. None of it
ships, nothing in `tools/` or in the packages calls into it, and deleting the whole
directory would leave the plugin building and running exactly as it does now.

It is here rather than outside the repository because the milestones were verified with
it, and a measurement is worth less without the thing that took it.

What it assumes, all of which is one machine rather than a requirement:

- KDE Plasma on Wayland, one 1920x1080 output at scale 1, so screenshot pixels equal
  screen coordinates
- a German (QWERTZ) keyboard layout, which is why `ui.sh` has a `paste` at all
- passwordless sudo, though every script here says so rather than assuming it

`dev-environment.md` is the full inventory.

| File | What |
|---|---|
| `ui.sh` | screenshots and synthetic input, so the app can be looked at with nobody at the keyboard |
| `run-example.sh` | build and launch the example app, in the foreground, detached, or straight from the bundle |
| `fake-adapter.sh` | a software Carlinkit adapter on this machine's USB (dummy_hcd plus a configfs gadget): `setup`, `teardown`, `status`, `log`. Needs sudo and kernel headers |
| `fake-adapter.py` | the gadget program behind it: answers Open, plugs a fake iPhone, streams H.264 test video, optional audio, metadata and microphone commands |
| `dev-environment.md` | the machine, and what it can and cannot be driven to do |

The counterpart is `tools/`, which is for anyone who clones this repository:
provisioning and the native tests.
