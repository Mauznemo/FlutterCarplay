// SPDX-License-Identifier: GPL-3.0-or-later
/// Run Apple CarPlay inside a Flutter app, through a Carlinkit adapter.
///
/// The iPhone's screen is rendered into a Flutter [Texture], so ordinary widgets can be
/// composited on top of it, and what the phone says about itself (the track playing,
/// turn by turn guidance, calls) arrives as data for the app to draw in its own design.
/// How the pieces fit together is written up in
/// [docs/architecture.md](https://github.com/Mauznemo/FlutterCarplay/blob/main/docs/architecture.md).
library;

export 'package:carplay_platform_interface/carplay_platform_interface.dart'
    show
        CarPlayAdapterInfo,
        CarPlayAudioBuffer,
        CarPlayAudioDevice,
        CarPlayAudioStream,
        CarPlayCall,
        CarPlayCallState,
        CarPlayConfig,
        CarPlayConnectionState,
        CarPlayDriveSide,
        CarPlayEvent,
        CarPlayKey,
        CarPlayManeuver,
        CarPlayMetadata,
        CarPlayMicrophone,
        CarPlayNavigation,
        CarPlayNowPlaying,
        CarPlayPhone,
        CarPlayPhoneType,
        CarPlayRequest,
        CarPlayTouchAction,
        CarPlayTouchPoint,
        CarPlayVideoInfo,
        CarPlayWifiBand;

export 'src/carplay_controller.dart';
export 'src/carplay_view.dart';
