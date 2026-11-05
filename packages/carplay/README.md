# carplay

Run Apple CarPlay inside a Flutter app, through a Carlinkit CPC200-CCPA adapter. The iPhone's
screen is a Flutter `Texture`, and what the phone says about the track playing, the route and
calls arrives as data for the app to draw itself.

**In development, nothing has projected yet.** See the repository's
[`PLAN.md`](https://github.com/Mauznemo/FlutterCarplay/blob/main/PLAN.md).

```dart
final controller = CarPlayController();
// ...
CarPlayView(controller: controller)
```

Linux only for now. GPL-3.0-or-later. Not affiliated with Apple or Carlinkit.
