// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:typed_data';

import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('CarPlayNowPlaying', () {
    test('merges partial updates', () {
      var playing = CarPlayNowPlaying.empty.merge({
        'MediaSongName': 'Song',
        'MediaArtistName': 'Artist',
        'MediaAlbumName': 'Album',
        'MediaAPPName': 'Music',
        'MediaSongDuration': 180000,
        'MediaPlayStatus': 1,
      });
      // The position on its own, the way the phone sends it once a second.
      playing = playing.merge({'MediaSongPlayTime': 42000});
      expect(playing.title, 'Song');
      expect(playing.artist, 'Artist');
      expect(playing.duration, const Duration(minutes: 3));
      expect(playing.position, const Duration(seconds: 42));
      expect(playing.playing, isTrue);
    });

    test('a new title drops the old track details and cover', () {
      final first = CarPlayNowPlaying.empty
          .merge({'MediaSongName': 'One', 'MediaArtistName': 'A'})
          .withAlbumArt(Uint8List.fromList([1, 2, 3]));
      final second = first.merge({'MediaSongName': 'Two'});
      expect(second.title, 'Two');
      expect(second.artist, isNull);
      expect(second.albumArt, isNull);
      // The same title again is not a new track.
      expect(first.merge({'MediaSongName': 'One'}).albumArt, isNotNull);
    });

    test('reads a status other than 1 as paused', () {
      expect(CarPlayNowPlaying.empty.merge({'MediaPlayStatus': 2}).playing, isFalse);
    });
  });

  group('CarPlayNavigation', () {
    test('merges and maps the maneuver', () {
      var navigation = CarPlayNavigation.inactive.merge({
        'NaviStatus': 1,
        'NaviManeuverType': 2,
        'NaviRemainDistance': 300,
        'NaviRoadName': 'Main Street',
      });
      navigation = navigation.merge({'NaviRemainDistance': 120});
      expect(navigation.active, isTrue);
      expect(navigation.maneuver, CarPlayManeuver.right);
      expect(navigation.distanceToManeuver, 120);
      expect(navigation.roadName, 'Main Street');
    });

    test('NaviStatus arrives as a float and still counts', () {
      // Seen from real firmware: 1.0000002.
      expect(CarPlayNavigation.inactive.merge({'NaviStatus': 1.0000002}).active, isTrue);
    });

    test('guidance ending clears the turn', () {
      final active = CarPlayNavigation.inactive.merge({
        'NaviStatus': 1,
        'NaviManeuverType': 1,
        'NaviRoadName': 'Road',
      });
      final ended = active.merge({'NaviStatus': 0});
      expect(ended.active, isFalse);
      expect(ended.maneuver, isNull);
      expect(ended.roadName, isNull);
    });

    test('the maneuver list matches the protocol numbering', () {
      expect(CarPlayManeuver.values.length, 54);
      expect(CarPlayManeuver.fromCode(28), CarPlayManeuver.roundaboutExit1);
      expect(CarPlayManeuver.fromCode(28)!.roundaboutExit, 1);
      expect(CarPlayManeuver.fromCode(46)!.roundaboutExit, 19);
      expect(CarPlayManeuver.fromCode(53), CarPlayManeuver.changeHighwayRight);
      expect(CarPlayManeuver.fromCode(54), isNull);
      expect(CarPlayManeuver.left.roundaboutExit, isNull);
    });
  });

  test('CarPlayCall reads the corrected numbering', () {
    final call = CarPlayCall.fromJson({
      'CallStatus': 4,
      'CallDirection': 1,
      'CallName': 'Alice',
      'CallNumber': '+49 123',
    });
    expect(call.state, CarPlayCallState.active);
    expect(call.incoming, isTrue);
    expect(call.name, 'Alice');
    expect(CarPlayCall.fromJson({'CallStatus': 9}).state, CarPlayCallState.idle);
  });

  group('CarPlayPhone', () {
    test('reads the native update with the adapter details inside', () {
      final phone = CarPlayPhone.fromJson({
        'phoneType': 3,
        'wireless': true,
        'details': '{"MDModel":"iPhone16,1","MDOSVersion":"23A341"}',
      });
      expect(phone, isNotNull);
      expect(phone!.type, CarPlayPhoneType.carPlay);
      expect(phone.wireless, isTrue);
      expect(phone.model, 'iPhone16,1');
    });

    test('an empty update means the phone has gone', () {
      expect(CarPlayPhone.fromJson({}), isNull);
    });

    test('malformed adapter details cost the details only', () {
      final phone = CarPlayPhone.fromJson({'phoneType': 5, 'details': '{not json'});
      expect(phone!.type, CarPlayPhoneType.androidAuto);
      expect(phone.model, isNull);
    });
  });

  test('CarPlayAdapterInfo reads the model out of the adapter JSON', () {
    final info = CarPlayAdapterInfo.fromJson({
      'open': true,
      'softwareVersion': '2025.02.25.1521CAY',
      'details': '{"productType":"A15W","hwVersion":"YMA0-WN16-0003"}',
    });
    expect(info.open, isTrue);
    expect(info.model, 'A15W');
    expect(info.hardwareVersion, 'YMA0-WN16-0003');
    expect(info.softwareVersion, '2025.02.25.1521CAY');
  });

  test('decodeJsonObject tolerates what the phone sends', () {
    // Duplicate keys have been seen in navigation updates; the last one wins.
    expect(decodeJsonObject('{"a":1,"a":2}'), {'a': 2});
    expect(decodeJsonObject('[1]'), isNull);
    expect(decodeJsonObject(''), isNull);
  });

  test('enum orders match the C ABI', () {
    // These cross FFI as integers, see cp_core.h.
    expect(CarPlayConnectionState.values.map((v) => v.name), [
      'idle',
      'searching',
      'waitingForPhone',
      'connected',
      'error',
    ]);
    expect(CarPlayAudioStream.values.map((v) => v.name), [
      'media',
      'voice',
      'navigation',
    ]);
    expect(CarPlayMicrophone.values.map((v) => v.name), ['headUnit', 'adapter', 'phone']);
    expect(CarPlayMetadata.values.length, 7);
    expect(CarPlayTouchAction.pointerUp.code, 6);
  });
}
