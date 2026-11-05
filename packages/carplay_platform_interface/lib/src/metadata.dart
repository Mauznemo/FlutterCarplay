// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:convert';
import 'dart:typed_data';

/// Which kind of update a metadata message carries, in the order of `CpMetadata` in the
/// native `cp_core.h`. The integers cross FFI as-is, so the two stay in step.
enum CarPlayMetadata {
  /// The track playing on the phone.
  nowPlaying,

  /// The cover of that track.
  albumArt,

  /// Turn by turn guidance.
  navigation,

  /// A phone call.
  call,

  /// What the adapter says about itself.
  adapter,

  /// The phone connected now.
  phone,

  /// Something the phone asks the head unit to do.
  request,
}

/// What a phone asks of the head unit, rather than tells it.
enum CarPlayRequest {
  /// The driver tapped the car's own icon in CarPlay: show the head unit's interface.
  /// The phone stays connected, and the projection is still there to go back to.
  showHostUi,

  /// CarPlay went to the background on the phone's side. Not a disconnect.
  hide,

  /// The adapter wants the projection on screen.
  videoFocus,

  /// The adapter no longer needs the projection on screen.
  videoFocusReleased,
}

/// Reads a JSON object the phone or the adapter sent, or null when it is not one.
///
/// The phone's own JSON comes through the adapter untouched and has been seen with
/// duplicate keys; `jsonDecode` keeps the last, which is the newest value.
Map<String, dynamic>? decodeJsonObject(String text) {
  try {
    final decoded = jsonDecode(text);
    return decoded is Map<String, dynamic> ? decoded : null;
  } on FormatException {
    return null;
  }
}

int? _int(Object? value) => switch (value) {
  final int v => v,
  final double v => v.round(),
  final String v => int.tryParse(v),
  _ => null,
};

String? _string(Object? value) {
  if (value is! String) {
    return null;
  }
  final trimmed = value.trim();
  return trimmed.isEmpty ? null : trimmed;
}

/// The track the phone is playing.
///
/// The phone sends this in pieces: a new track's title and artist together, and then
/// the position once a second on its own. [merge] folds a piece into what was known, so
/// a field the phone has not mentioned keeps its last value rather than going blank.
class CarPlayNowPlaying {
  /// The track's title.
  final String? title;

  /// The artist.
  final String? artist;

  /// The album.
  final String? album;

  /// The app playing it, such as "Music" or "Spotify".
  final String? app;

  /// How long the track is.
  final Duration? duration;

  /// How far into it the phone is, as of the update that said so.
  final Duration? position;

  /// Whether it is playing rather than paused. Null until the phone has said.
  final bool? playing;

  /// The cover, as the JPEG or PNG the phone sent. Null until one arrives.
  final Uint8List? albumArt;

  /// Creates a description of a track. Every field is optional because the phone
  /// sends them separately.
  const CarPlayNowPlaying({
    this.title,
    this.artist,
    this.album,
    this.app,
    this.duration,
    this.position,
    this.playing,
    this.albumArt,
  });

  /// Nothing known yet.
  static const CarPlayNowPlaying empty = CarPlayNowPlaying();

  /// This, with whatever the phone's now playing JSON [json] says on top.
  ///
  /// A new title drops the old cover: it belonged to the last track, and the new one's
  /// arrives in a message of its own a moment later.
  CarPlayNowPlaying merge(Map<String, dynamic> json) {
    final newTitle = _string(json['MediaSongName']);
    final changedTrack = newTitle != null && newTitle != title;
    final status = _int(json['MediaPlayStatus']);
    final durationMs = _int(json['MediaSongDuration']);
    final positionMs = _int(json['MediaSongPlayTime']);
    return CarPlayNowPlaying(
      title: newTitle ?? title,
      artist: _string(json['MediaArtistName']) ?? (changedTrack ? null : artist),
      album: _string(json['MediaAlbumName']) ?? (changedTrack ? null : album),
      app: _string(json['MediaAPPName']) ?? app,
      duration: durationMs != null
          ? Duration(milliseconds: durationMs)
          : (changedTrack ? null : duration),
      position: positionMs != null
          ? Duration(milliseconds: positionMs)
          : (changedTrack ? null : position),
      // 1 is the only value seen for playing. Anything else is read as paused.
      playing: status != null ? status == 1 : playing,
      albumArt: changedTrack ? null : albumArt,
    );
  }

  /// This with [image] as the cover.
  CarPlayNowPlaying withAlbumArt(Uint8List? image) => CarPlayNowPlaying(
    title: title,
    artist: artist,
    album: album,
    app: app,
    duration: duration,
    position: position,
    playing: playing,
    albumArt: image,
  );

  @override
  String toString() => 'CarPlayNowPlaying($title by $artist, playing: $playing)';
}

/// What a turn instruction asks the driver to do, CarPlay's own list of maneuvers.
///
/// The order is the protocol's numbering, so [CarPlayManeuver.values] indexed by the
/// number the phone sent is the maneuver.
enum CarPlayManeuver {
  /// No turn.
  noTurn,

  /// Turn left.
  left,

  /// Turn right.
  right,

  /// Go straight on.
  straight,

  /// Make a U turn.
  uTurn,

  /// Follow the road.
  followRoad,

  /// Enter a roundabout.
  enterRoundabout,

  /// Leave a roundabout.
  exitRoundabout,

  /// Take the off ramp.
  rampOff,

  /// Take the on ramp.
  rampOn,

  /// Guidance has ended.
  endOfNavigation,

  /// Head to the route.
  proceedToRoute,

  /// Arrived.
  arrived,

  /// Keep left.
  keepLeft,

  /// Keep right.
  keepRight,

  /// Board a ferry.
  enterFerry,

  /// Leave a ferry.
  exitFerry,

  /// Change ferry.
  changeFerry,

  /// U turn to get onto the route.
  uTurnToRoute,

  /// U turn at a roundabout.
  roundaboutUTurn,

  /// Turn left at the end of the road.
  endOfRoadLeft,

  /// Turn right at the end of the road.
  endOfRoadRight,

  /// Off ramp on the left.
  rampOffLeft,

  /// Off ramp on the right.
  rampOffRight,

  /// Arrived, destination on the left.
  arrivedLeft,

  /// Arrived, destination on the right.
  arrivedRight,

  /// U turn when possible.
  uTurnWhenPossible,

  /// End of directions.
  endOfDirections,

  /// Roundabout, first exit.
  roundaboutExit1,

  /// Roundabout, second exit.
  roundaboutExit2,

  /// Roundabout, third exit.
  roundaboutExit3,

  /// Roundabout, fourth exit.
  roundaboutExit4,

  /// Roundabout, fifth exit.
  roundaboutExit5,

  /// Roundabout, sixth exit.
  roundaboutExit6,

  /// Roundabout, seventh exit.
  roundaboutExit7,

  /// Roundabout, eighth exit.
  roundaboutExit8,

  /// Roundabout, ninth exit.
  roundaboutExit9,

  /// Roundabout, tenth exit.
  roundaboutExit10,

  /// Roundabout, eleventh exit.
  roundaboutExit11,

  /// Roundabout, twelfth exit.
  roundaboutExit12,

  /// Roundabout, thirteenth exit.
  roundaboutExit13,

  /// Roundabout, fourteenth exit.
  roundaboutExit14,

  /// Roundabout, fifteenth exit.
  roundaboutExit15,

  /// Roundabout, sixteenth exit.
  roundaboutExit16,

  /// Roundabout, seventeenth exit.
  roundaboutExit17,

  /// Roundabout, eighteenth exit.
  roundaboutExit18,

  /// Roundabout, nineteenth exit.
  roundaboutExit19,

  /// Sharp left.
  sharpLeft,

  /// Sharp right.
  sharpRight,

  /// Slight left.
  slightLeft,

  /// Slight right.
  slightRight,

  /// Change highway.
  changeHighway,

  /// Change highway, to the left.
  changeHighwayLeft,

  /// Change highway, to the right.
  changeHighwayRight;

  /// The maneuver the phone numbered [code], or null for a number past the list.
  static CarPlayManeuver? fromCode(int? code) =>
      code == null || code < 0 || code >= values.length ? null : values[code];

  /// Which exit to take, for the roundabout exits, or null for anything else.
  int? get roundaboutExit {
    final first = roundaboutExit1.index;
    final last = roundaboutExit19.index;
    return index >= first && index <= last ? index - first + 1 : null;
  }
}

/// Turn by turn guidance from the phone's navigation app.
///
/// Like [CarPlayNowPlaying] it arrives in pieces and [merge] folds them together. One
/// exception: when guidance stops, the turn fields are cleared rather than kept,
/// because an instruction left on screen after guidance ends misleads a driver.
class CarPlayNavigation {
  /// Whether guidance is running.
  final bool active;

  /// The next maneuver.
  final CarPlayManeuver? maneuver;

  /// Metres to the next maneuver.
  final int? distanceToManeuver;

  /// The road the maneuver leads onto.
  final String? roadName;

  /// Where the route ends.
  final String? destinationName;

  /// Metres to the destination.
  final int? distanceToDestination;

  /// Time left to the destination.
  final Duration? timeToDestination;

  /// The navigation app, such as "Maps".
  final String? appName;

  /// The angle of the turn in degrees, as the phone reports it.
  final int? turnAngle;

  /// Which side the turn is on, as the phone reports it.
  final int? turnSide;

  /// The roundabout exit number, when the maneuver is one.
  final int? roundaboutExit;

  /// Creates a description of guidance.
  const CarPlayNavigation({
    this.active = false,
    this.maneuver,
    this.distanceToManeuver,
    this.roadName,
    this.destinationName,
    this.distanceToDestination,
    this.timeToDestination,
    this.appName,
    this.turnAngle,
    this.turnSide,
    this.roundaboutExit,
  });

  /// No guidance.
  static const CarPlayNavigation inactive = CarPlayNavigation();

  /// This, with whatever the phone's navigation JSON [json] says on top.
  CarPlayNavigation merge(Map<String, dynamic> json) {
    final status = _int(json['NaviStatus']);
    // 1 is active. Android Auto uses 2 for inactive, CarPlay 0; neither is 1.
    final isActive = status != null ? status == 1 : active;
    if (!isActive) {
      return CarPlayNavigation(appName: _string(json['NaviAPPName']) ?? appName);
    }
    final seconds = _int(json['NaviTimeToDestination']);
    return CarPlayNavigation(
      active: true,
      maneuver: json.containsKey('NaviManeuverType')
          ? CarPlayManeuver.fromCode(_int(json['NaviManeuverType']))
          : maneuver,
      distanceToManeuver: _int(json['NaviRemainDistance']) ?? distanceToManeuver,
      roadName: _string(json['NaviRoadName']) ?? roadName,
      destinationName: _string(json['NaviDestinationName']) ?? destinationName,
      distanceToDestination:
          _int(json['NaviDistanceToDestination']) ?? distanceToDestination,
      timeToDestination: seconds != null ? Duration(seconds: seconds) : timeToDestination,
      appName: _string(json['NaviAPPName']) ?? appName,
      turnAngle: _int(json['NaviTurnAngle']) ?? turnAngle,
      turnSide: _int(json['NaviTurnSide']) ?? turnSide,
      roundaboutExit: _int(json['NaviRoundaboutExit']) ?? roundaboutExit,
    );
  }

  @override
  String toString() =>
      'CarPlayNavigation(${active ? '$maneuver in ${distanceToManeuver}m onto $roadName' : 'inactive'})';
}

/// Where a call is.
enum CarPlayCallState {
  /// No call, or the last one ended.
  idle,

  /// Calling out, not answered yet.
  dialing,

  /// Ringing in.
  ringing,

  /// Connecting.
  connecting,

  /// In progress.
  active,
}

/// A phone call, as the phone describes it.
///
/// The audio is not here: it is the voice stream, see `CarPlayAudioStream.voice`.
class CarPlayCall {
  /// Where the call is.
  final CarPlayCallState state;

  /// True for a call coming in, false going out, null when not said.
  final bool? incoming;

  /// The contact's name, when the phone knows one.
  final String? name;

  /// The number.
  final String? number;

  /// Creates a description of a call.
  const CarPlayCall({
    this.state = CarPlayCallState.idle,
    this.incoming,
    this.name,
    this.number,
  });

  /// No call.
  static const CarPlayCall none = CarPlayCall();

  /// Reads the phone's call status JSON. Replaces rather than merges: a call that
  /// ended takes its caller with it.
  factory CarPlayCall.fromJson(Map<String, dynamic> json) {
    final status = _int(json['CallStatus']) ?? 0;
    final direction = _int(json['CallDirection']);
    return CarPlayCall(
      state: status >= 0 && status < CarPlayCallState.values.length
          ? CarPlayCallState.values[status]
          : CarPlayCallState.idle,
      incoming: direction == null ? null : direction == 1,
      name: _string(json['CallName']),
      number: _string(json['CallNumber']),
    );
  }

  @override
  String toString() => 'CarPlayCall($state, $name $number)';
}

/// What kind of phone the adapter connected.
enum CarPlayPhoneType {
  /// An iPhone running CarPlay.
  carPlay,

  /// An Android phone running Android Auto through the adapter. This plugin does not
  /// project it; use the android_auto plugin for Android phones.
  androidAuto,

  /// Something else the adapter speaks, such as screen mirroring.
  other,
}

/// The phone connected now.
class CarPlayPhone {
  /// What kind of phone it is.
  final CarPlayPhoneType type;

  /// Whether it is connected over Wi-Fi rather than the cable.
  final bool wireless;

  /// The model identifier, such as "iPhone18,4".
  final String? model;

  /// The phone's OS build.
  final String? osVersion;

  /// The phone's Bluetooth address, as the adapter reports it.
  final String? bluetoothAddress;

  /// Creates a description of a phone.
  const CarPlayPhone({
    required this.type,
    required this.wireless,
    this.model,
    this.osVersion,
    this.bluetoothAddress,
  });

  /// Reads the native phone update, or null for `{}`, which means the phone has gone.
  static CarPlayPhone? fromJson(Map<String, dynamic> json) {
    final code = _int(json['phoneType']);
    if (code == null) {
      return null;
    }
    final details = json['details'] is String
        ? decodeJsonObject(json['details'] as String)
        : null;
    return CarPlayPhone(
      type: switch (code) {
        3 || 8 => CarPlayPhoneType.carPlay,
        5 => CarPlayPhoneType.androidAuto,
        _ => CarPlayPhoneType.other,
      },
      wireless: json['wireless'] == true,
      model: _string(details?['MDModel']),
      osVersion: _string(details?['MDOSVersion']),
      bluetoothAddress: _string(details?['btMacAddr']),
    );
  }

  @override
  String toString() => 'CarPlayPhone($type, ${wireless ? 'wireless' : 'wired'}, $model)';
}

/// What the Carlinkit adapter says about itself.
///
/// Useful on a settings screen, and for a bug report: firmware versions change the
/// adapter's behaviour, and the version is the first thing worth knowing.
class CarPlayAdapterInfo {
  /// Whether this head unit has the adapter open.
  final bool open;

  /// Where it is on USB, as "1314:1520 on bus 1, device 7".
  final String? usb;

  /// The firmware version, such as "2025.02.25.1521CAY".
  final String? softwareVersion;

  /// The adapter's Bluetooth name, which is what the phone pairs with.
  final String? bluetoothName;

  /// The adapter's Bluetooth address.
  final String? bluetoothAddress;

  /// The adapter's Wi-Fi network name.
  final String? wifiName;

  /// A Bluetooth pairing PIN to show the driver, while pairing asks for one.
  final String? pin;

  /// The adapter's list of paired phones, as text in its own format.
  final String? pairedDevices;

  /// The last wireless status the adapter reported, such as "scanning" or
  /// "bluetoothConnected".
  final String? linkStatus;

  /// The adapter's model, such as "A15W" for the CPC200-CCPA.
  final String? model;

  /// The adapter's hardware revision.
  final String? hardwareVersion;

  /// Creates a description of an adapter.
  const CarPlayAdapterInfo({
    this.open = false,
    this.usb,
    this.softwareVersion,
    this.bluetoothName,
    this.bluetoothAddress,
    this.wifiName,
    this.pin,
    this.pairedDevices,
    this.linkStatus,
    this.model,
    this.hardwareVersion,
  });

  /// No adapter.
  static const CarPlayAdapterInfo none = CarPlayAdapterInfo();

  /// Reads the native adapter update, which is the whole picture every time.
  factory CarPlayAdapterInfo.fromJson(Map<String, dynamic> json) {
    final details = json['details'] is String
        ? decodeJsonObject(json['details'] as String)
        : null;
    return CarPlayAdapterInfo(
      open: json['open'] == true,
      usb: _string(json['usb']),
      softwareVersion: _string(json['softwareVersion']),
      bluetoothName: _string(json['bluetoothName']),
      bluetoothAddress: _string(json['bluetoothAddress']),
      wifiName: _string(json['wifiName']),
      pin: _string(json['pin']),
      pairedDevices: _string(json['pairedDevices']),
      linkStatus: _string(json['linkStatus']),
      model: _string(details?['productType']),
      hardwareVersion: _string(details?['hwVersion']),
    );
  }

  @override
  String toString() => 'CarPlayAdapterInfo($model, firmware $softwareVersion)';
}
