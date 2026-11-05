# carplay_example

The reference integration, and the test bench. It shows how a head unit is put together with
the plugin and exercises every part of it.

```bash
cd example && flutter run -d linux
```

Press **Test pattern** to see the projection path with no hardware. With a Carlinkit adapter
plugged in, press **Start** and connect an iPhone to the adapter.

## Where to start reading

| File | What |
|---|---|
| `lib/config.dart` | what the head unit tells the adapter and the phone it is |
| `lib/head_unit_page.dart` | one controller, one `CarPlayView`, a status bar above it and everything else stacked on top |
| `lib/head_unit/` | the pieces a real head unit would ship: the status bar, the placeholder, and the turn card, call banner and now playing bar drawn from metadata |
| `lib/bench/` | the test bench: the dock of keys along the bottom and the adapter panel |

## Knobs

Environment variables of the example app, not the plugin:

| | |
|---|---|
| `CP_AUTOSTART=1` | press Start on launch |
| `CP_SIMULATE=1` | use `CarPlaySimulator` instead of the adapter, as macOS and Windows always do |
| `CP_TEST_PATTERN=1` | start the test pattern on launch |
| `CP_LOG_LEVEL=DEBUG` | the plugin's native log, every adapter message included |
