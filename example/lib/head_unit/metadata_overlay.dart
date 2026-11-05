// SPDX-License-Identifier: GPL-3.0-or-later
import 'package:carplay/carplay.dart';
import 'package:flutter/material.dart';

/// The turn card, call banner and now playing bar, drawn from what the phone says
/// rather than from its pixels. Each appears only while the phone has something to
/// say, so an idle head unit shows an empty corner.
class MetadataOverlay extends StatelessWidget {
  final CarPlayController controller;

  const MetadataOverlay({super.key, required this.controller});

  @override
  Widget build(BuildContext context) {
    final navigation = controller.navigation;
    final call = controller.call;
    final playing = controller.nowPlaying;
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if (navigation.active) _TurnCard(navigation: navigation),
        if (call.state != CarPlayCallState.idle) _CallBanner(call: call),
        if (playing.title != null) _NowPlayingBar(playing: playing),
      ],
    );
  }
}

class _Card extends StatelessWidget {
  final Widget child;

  const _Card({required this.child});

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(top: 8),
      padding: const EdgeInsets.all(12),
      constraints: const BoxConstraints(maxWidth: 360),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerHighest
            .withValues(alpha: 0.92),
        borderRadius: BorderRadius.circular(12),
      ),
      child: child,
    );
  }
}

class _TurnCard extends StatelessWidget {
  final CarPlayNavigation navigation;

  const _TurnCard({required this.navigation});

  IconData get _icon => switch (navigation.maneuver) {
    CarPlayManeuver.left || CarPlayManeuver.endOfRoadLeft => Icons.turn_left,
    CarPlayManeuver.right || CarPlayManeuver.endOfRoadRight => Icons.turn_right,
    CarPlayManeuver.slightLeft || CarPlayManeuver.keepLeft => Icons.turn_slight_left,
    CarPlayManeuver.slightRight || CarPlayManeuver.keepRight => Icons.turn_slight_right,
    CarPlayManeuver.sharpLeft => Icons.turn_sharp_left,
    CarPlayManeuver.sharpRight => Icons.turn_sharp_right,
    CarPlayManeuver.uTurn ||
    CarPlayManeuver.uTurnToRoute ||
    CarPlayManeuver.uTurnWhenPossible => Icons.u_turn_left,
    CarPlayManeuver.arrived ||
    CarPlayManeuver.arrivedLeft ||
    CarPlayManeuver.arrivedRight => Icons.flag,
    final maneuver when maneuver?.roundaboutExit != null => Icons.roundabout_right,
    _ => Icons.straight,
  };

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final distance = navigation.distanceToManeuver;
    return _Card(
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(_icon, size: 40),
          const SizedBox(width: 12),
          Flexible(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                if (distance != null)
                  Text(
                    distance >= 1000
                        ? '${(distance / 1000).toStringAsFixed(1)} km'
                        : '$distance m',
                    style: theme.textTheme.titleLarge,
                  ),
                if (navigation.roadName != null)
                  Text(navigation.roadName!, overflow: TextOverflow.ellipsis),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _CallBanner extends StatelessWidget {
  final CarPlayCall call;

  const _CallBanner({required this.call});

  @override
  Widget build(BuildContext context) {
    return _Card(
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Icon(Icons.call),
          const SizedBox(width: 12),
          Flexible(
            child: Text(
              '${call.name ?? call.number ?? 'Unknown'}  ${call.state.name}',
              overflow: TextOverflow.ellipsis,
            ),
          ),
        ],
      ),
    );
  }
}

class _NowPlayingBar extends StatelessWidget {
  final CarPlayNowPlaying playing;

  const _NowPlayingBar({required this.playing});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final art = playing.albumArt;
    return _Card(
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          ClipRRect(
            borderRadius: BorderRadius.circular(6),
            child: SizedBox(
              width: 48,
              height: 48,
              child: art == null
                  ? const ColoredBox(color: Colors.black26, child: Icon(Icons.music_note))
                  : Image.memory(art, fit: BoxFit.cover, gaplessPlayback: true),
            ),
          ),
          const SizedBox(width: 12),
          Flexible(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  playing.title ?? '',
                  style: theme.textTheme.titleSmall,
                  overflow: TextOverflow.ellipsis,
                ),
                Text(
                  [playing.artist, playing.app].whereType<String>().join(' · '),
                  style: theme.textTheme.bodySmall,
                  overflow: TextOverflow.ellipsis,
                ),
              ],
            ),
          ),
          const SizedBox(width: 8),
          Icon(playing.playing == true ? Icons.pause : Icons.play_arrow),
        ],
      ),
    );
  }
}
