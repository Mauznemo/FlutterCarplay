// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:math' as math;

import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:flutter/widgets.dart';

import 'simulated_phone.dart';

/// A drawn stand in for CarPlay: a dock along the driver's side, and the app it shows.
///
/// Laid out for whatever size it is given, the way the phone lays out for the frame it
/// is asked for. Everything that reacts to a tap sits in a [MetaData] with a
/// [SimulatedTarget], so touches that arrive as projected video pixels can be hit tested
/// against it. Labelled as simulated, so a screenshot is never mistaken for the car.
class SimulatedScreen extends StatefulWidget {
  /// The pretend phone this draws.
  final SimulatedPhone phone;

  /// Which side the dock is on.
  final CarPlayDriveSide driveSide;

  /// Where the controls stay out of, as a real phone keeps out of the safe area. The
  /// background still fills everything.
  final EdgeInsets safeArea;

  /// Creates the screen.
  const SimulatedScreen({
    super.key,
    required this.phone,
    required this.driveSide,
    this.safeArea = EdgeInsets.zero,
  });

  @override
  State<SimulatedScreen> createState() => _SimulatedScreenState();
}

class _Palette {
  final Color background;
  final Color dock;
  final Color tile;
  final Color text;
  final Color dim;
  final Color accent;

  const _Palette(this.background, this.dock, this.tile, this.text, this.dim, this.accent);

  static const day = _Palette(
    Color(0xFFF2F2F7),
    Color(0xFFE5E5EA),
    Color(0xFFFFFFFF),
    Color(0xFF111111),
    Color(0xFF6B6B70),
    Color(0xFF0A84FF),
  );
  static const night = _Palette(
    Color(0xFF000000),
    Color(0xFF1C1C1E),
    Color(0xFF2C2C2E),
    Color(0xFFFFFFFF),
    Color(0xFF98989F),
    Color(0xFF0A84FF),
  );
}

class _SimulatedScreenState extends State<SimulatedScreen> {
  SimulatedPhone get phone => widget.phone;

  @override
  void initState() {
    super.initState();
    phone.addListener(_changed);
    phone.attach(context);
  }

  @override
  void didUpdateWidget(SimulatedScreen oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (!identical(oldWidget.phone, phone)) {
      oldWidget.phone
        ..removeListener(_changed)
        ..detach(context);
      phone
        ..addListener(_changed)
        ..attach(context);
    }
  }

  @override
  void dispose() {
    phone
      ..removeListener(_changed)
      ..detach(context);
    super.dispose();
  }

  void _changed() => setState(() {});

  @override
  Widget build(BuildContext context) {
    final colors = phone.night ? _Palette.night : _Palette.day;
    return LayoutBuilder(
      builder: (context, constraints) {
        final size = constraints.biggest;
        // One unit is a tenth of the short side, so the layout scales with the frame.
        final unit = math.max(8.0, math.min(size.width, size.height) / 10);
        final dock = _Dock(phone: phone, colors: colors, unit: unit);
        final content = Expanded(child: _content(colors, unit));
        final row = widget.driveSide == CarPlayDriveSide.left
            ? [dock, content]
            : [content, dock];
        return DefaultTextStyle(
          style: TextStyle(color: colors.text, fontSize: unit * 0.32),
          child: ColoredBox(
            color: colors.background,
            child: Stack(
              children: [
                Positioned.fill(
                  child: Padding(
                    padding: widget.safeArea,
                    child: Row(children: row),
                  ),
                ),
                if (phone.call.state != CarPlayCallState.idle)
                  Positioned.fill(
                    child: _CallScreen(phone: phone, colors: colors, unit: unit),
                  ),
                if (phone.listening)
                  Positioned(
                    left: 0,
                    right: 0,
                    bottom: unit * 0.4,
                    child: _SiriOrb(unit: unit),
                  ),
                Positioned(
                  right: unit * 0.2,
                  top: unit * 0.15,
                  child: Text(
                    'SIMULATED CARPLAY',
                    style: TextStyle(
                      color: colors.dim,
                      fontSize: unit * 0.22,
                      letterSpacing: 2,
                    ),
                  ),
                ),
                for (final finger in phone.fingers.values)
                  Positioned(
                    left: finger.dx - unit * 0.3,
                    top: finger.dy - unit * 0.3,
                    child: IgnorePointer(
                      child: Container(
                        width: unit * 0.6,
                        height: unit * 0.6,
                        decoration: BoxDecoration(
                          shape: BoxShape.circle,
                          color: colors.accent.withValues(alpha: 0.35),
                        ),
                      ),
                    ),
                  ),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _content(_Palette colors, double unit) => switch (phone.app) {
    SimulatedApp.home => _Home(phone: phone, colors: colors, unit: unit),
    SimulatedApp.maps => _Maps(phone: phone, colors: colors, unit: unit),
    SimulatedApp.music => _Music(phone: phone, colors: colors, unit: unit),
    SimulatedApp.phone => _Phone(phone: phone, colors: colors, unit: unit),
  };
}

/// A tappable area for the simulator's hit test.
Widget _target(VoidCallback onTap, Widget child) => MetaData(
  metaData: SimulatedTarget(onTap),
  behavior: HitTestBehavior.opaque,
  child: child,
);

class _AppIcon extends StatelessWidget {
  final String label;
  final Color color;
  final String glyph;
  final double size;
  final VoidCallback onTap;
  final bool showLabel;

  const _AppIcon({
    required this.label,
    required this.color,
    required this.glyph,
    required this.size,
    required this.onTap,
    this.showLabel = true,
  });

  @override
  Widget build(BuildContext context) {
    return _target(
      onTap,
      Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: size,
            height: size,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: color,
              borderRadius: BorderRadius.circular(size * 0.22),
            ),
            child: Text(
              glyph,
              style: TextStyle(color: const Color(0xFFFFFFFF), fontSize: size * 0.45),
            ),
          ),
          if (showLabel) ...[
            SizedBox(height: size * 0.12),
            Text(label, style: TextStyle(fontSize: size * 0.2)),
          ],
        ],
      ),
    );
  }
}

const _mapsColor = Color(0xFF34C759);
const _musicColor = Color(0xFFFF2D55);
const _phoneColor = Color(0xFF30D158);
const _carColor = Color(0xFF8E8E93);

class _Dock extends StatelessWidget {
  final SimulatedPhone phone;
  final _Palette colors;
  final double unit;

  const _Dock({required this.phone, required this.colors, required this.unit});

  @override
  Widget build(BuildContext context) {
    final now = DateTime.now();
    final clock =
        '${now.hour.toString().padLeft(2, '0')}:'
        '${now.minute.toString().padLeft(2, '0')}';
    final icon = unit * 0.9;
    return Container(
      width: unit * 1.4,
      color: colors.dock,
      padding: EdgeInsets.symmetric(vertical: unit * 0.25),
      child: Column(
        children: [
          Text(
            clock,
            style: TextStyle(fontSize: unit * 0.3, fontWeight: FontWeight.w600),
          ),
          const Spacer(),
          _AppIcon(
            label: 'Maps',
            color: _mapsColor,
            glyph: 'M',
            size: icon,
            showLabel: false,
            onTap: () => phone.open(SimulatedApp.maps),
          ),
          SizedBox(height: unit * 0.2),
          _AppIcon(
            label: 'Music',
            color: _musicColor,
            glyph: '♪',
            size: icon,
            showLabel: false,
            onTap: () => phone.open(SimulatedApp.music),
          ),
          SizedBox(height: unit * 0.2),
          _AppIcon(
            label: 'Phone',
            color: _phoneColor,
            glyph: 'P',
            size: icon,
            showLabel: false,
            onTap: () => phone.open(SimulatedApp.phone),
          ),
          const Spacer(),
          // The home button.
          _target(
            () => phone.open(SimulatedApp.home),
            Container(
              width: icon * 0.8,
              height: icon * 0.8,
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(icon * 0.2),
                border: Border.all(color: colors.dim, width: 2),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _Home extends StatelessWidget {
  final SimulatedPhone phone;
  final _Palette colors;
  final double unit;

  const _Home({required this.phone, required this.colors, required this.unit});

  @override
  Widget build(BuildContext context) {
    final size = unit * 1.6;
    return Center(
      child: Wrap(
        spacing: unit * 0.8,
        runSpacing: unit * 0.5,
        alignment: WrapAlignment.center,
        children: [
          _AppIcon(
            label: 'Maps',
            color: _mapsColor,
            glyph: 'M',
            size: size,
            onTap: () => phone.open(SimulatedApp.maps),
          ),
          _AppIcon(
            label: 'Music',
            color: _musicColor,
            glyph: '♪',
            size: size,
            onTap: () => phone.open(SimulatedApp.music),
          ),
          _AppIcon(
            label: 'Phone',
            color: _phoneColor,
            glyph: 'P',
            size: size,
            onTap: () => phone.open(SimulatedApp.phone),
          ),
          // The car's own icon: tapping it asks the head unit for its interface.
          _AppIcon(
            label: 'Car',
            color: _carColor,
            glyph: 'C',
            size: size,
            onTap: phone.requestHostUi,
          ),
        ],
      ),
    );
  }
}

class _Maps extends StatelessWidget {
  final SimulatedPhone phone;
  final _Palette colors;
  final double unit;

  const _Maps({required this.phone, required this.colors, required this.unit});

  String get _arrow => switch (phone.maneuver) {
    CarPlayManeuver.right => '↱',
    CarPlayManeuver.slightLeft => '↖',
    CarPlayManeuver.arrived => '⚑',
    final maneuver when maneuver.roundaboutExit != null => '⟳',
    _ => '↑',
  };

  @override
  Widget build(BuildContext context) {
    final land = phone.night ? const Color(0xFF1B2430) : const Color(0xFFE8F0E3);
    final road = phone.night ? const Color(0xFF3A4556) : const Color(0xFFFFFFFF);
    return Stack(
      children: [
        Positioned.fill(child: ColoredBox(color: land)),
        Positioned.fill(
          child: CustomPaint(
            painter: _RoadPainter(road, colors.accent, phone.routeActive),
          ),
        ),
        Positioned(
          left: unit * 0.3,
          top: unit * 0.3,
          child: phone.routeActive
              ? Container(
                  padding: EdgeInsets.all(unit * 0.25),
                  decoration: BoxDecoration(
                    color: const Color(0xFF1C1C1E),
                    borderRadius: BorderRadius.circular(unit * 0.2),
                  ),
                  child: DefaultTextStyle(
                    style: TextStyle(
                      color: const Color(0xFFFFFFFF),
                      fontSize: unit * 0.35,
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(_arrow, style: TextStyle(fontSize: unit * 0.8)),
                        SizedBox(width: unit * 0.2),
                        Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Text('${phone.metresToManeuver} m'),
                            Text(phone.road, style: TextStyle(fontSize: unit * 0.28)),
                          ],
                        ),
                      ],
                    ),
                  ),
                )
              : const SizedBox.shrink(),
        ),
        Positioned(
          right: unit * 0.4,
          bottom: unit * 0.4,
          child: _target(
            phone.routeActive ? phone.stopRoute : phone.startRoute,
            Container(
              padding: EdgeInsets.symmetric(horizontal: unit * 0.5, vertical: unit * 0.2),
              decoration: BoxDecoration(
                color: phone.routeActive ? const Color(0xFFFF3B30) : _mapsColor,
                borderRadius: BorderRadius.circular(unit * 0.3),
              ),
              child: Text(
                phone.routeActive ? 'End route' : 'Go home',
                style: TextStyle(color: const Color(0xFFFFFFFF), fontSize: unit * 0.35),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class _RoadPainter extends CustomPainter {
  final Color road;
  final Color route;
  final bool active;

  _RoadPainter(this.road, this.route, this.active);

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = size.shortestSide * 0.05
      ..strokeCap = StrokeCap.round
      ..color = road;
    final path = Path()
      ..moveTo(size.width * 0.5, size.height)
      ..lineTo(size.width * 0.5, size.height * 0.55)
      ..lineTo(size.width * 0.85, size.height * 0.35)
      ..moveTo(size.width * 0.1, size.height * 0.4)
      ..lineTo(size.width * 0.5, size.height * 0.55);
    canvas.drawPath(path, paint);
    if (active) {
      canvas.drawPath(
        Path()
          ..moveTo(size.width * 0.5, size.height)
          ..lineTo(size.width * 0.5, size.height * 0.55)
          ..lineTo(size.width * 0.85, size.height * 0.35),
        paint
          ..color = route
          ..strokeWidth = size.shortestSide * 0.025,
      );
    }
  }

  @override
  bool shouldRepaint(_RoadPainter oldDelegate) =>
      oldDelegate.road != road || oldDelegate.active != active;
}

class _Music extends StatelessWidget {
  final SimulatedPhone phone;
  final _Palette colors;
  final double unit;

  const _Music({required this.phone, required this.colors, required this.unit});

  String _time(Duration value) =>
      '${value.inMinutes}:${(value.inSeconds % 60).toString().padLeft(2, '0')}';

  @override
  Widget build(BuildContext context) {
    final cover = unit * 4;
    final progress = phone.duration.inMilliseconds == 0
        ? 0.0
        : phone.position.inMilliseconds / phone.duration.inMilliseconds;
    Widget button(String glyph, VoidCallback onTap) => _target(
      onTap,
      Padding(
        padding: EdgeInsets.all(unit * 0.2),
        child: Text(glyph, style: TextStyle(fontSize: unit * 0.7)),
      ),
    );
    // Scaled down as a whole rather than laid out to the pixel: a small view, or the
    // wide test font, must not overflow.
    return Center(
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            // Painted, not decoded from the PNG the metadata stream carries: an image decode
            // never completes under `flutter test`, and a host app's own tests open this.
            Container(
              width: cover,
              height: cover,
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(unit * 0.2),
                gradient: LinearGradient(
                  begin: Alignment.topLeft,
                  end: Alignment.bottomRight,
                  colors: phone.artColors,
                ),
              ),
            ),
            SizedBox(width: unit * 0.6),
            SizedBox(
              width: unit * 4.5,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    phone.title,
                    style: TextStyle(fontSize: unit * 0.45, fontWeight: FontWeight.w600),
                  ),
                  Text(phone.artist, style: TextStyle(color: colors.dim)),
                  SizedBox(height: unit * 0.3),
                  Container(
                    height: unit * 0.06,
                    color: colors.dim.withValues(alpha: 0.3),
                    alignment: Alignment.centerLeft,
                    child: FractionallySizedBox(
                      widthFactor: progress.clamp(0.0, 1.0),
                      child: Container(color: colors.text),
                    ),
                  ),
                  SizedBox(height: unit * 0.1),
                  Text(
                    '${_time(phone.position)} / ${_time(phone.duration)}',
                    style: TextStyle(color: colors.dim, fontSize: unit * 0.25),
                  ),
                  SizedBox(height: unit * 0.3),
                  FittedBox(
                    fit: BoxFit.scaleDown,
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        // ASCII rather than media symbols, which not every font has.
                        button('|<', () => phone.skip(-1)),
                        button(phone.playing ? '||' : '>', phone.playPause),
                        button('>|', () => phone.skip(1)),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _Phone extends StatelessWidget {
  final SimulatedPhone phone;
  final _Palette colors;
  final double unit;

  const _Phone({required this.phone, required this.colors, required this.unit});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.all(unit * 0.5),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            'Favourites',
            style: TextStyle(fontSize: unit * 0.45, fontWeight: FontWeight.w600),
          ),
          SizedBox(height: unit * 0.3),
          for (var i = 0; i < SimulatedPhone.favourites.length; i++)
            _target(
              () => phone.callFavourite(i),
              Container(
                margin: EdgeInsets.only(bottom: unit * 0.15),
                padding: EdgeInsets.all(unit * 0.25),
                decoration: BoxDecoration(
                  color: colors.tile,
                  borderRadius: BorderRadius.circular(unit * 0.15),
                ),
                child: Row(
                  children: [
                    Text(SimulatedPhone.favourites[i].$1),
                    const Spacer(),
                    Text(
                      SimulatedPhone.favourites[i].$2,
                      style: TextStyle(color: colors.dim),
                    ),
                  ],
                ),
              ),
            ),
        ],
      ),
    );
  }
}

class _CallScreen extends StatelessWidget {
  final SimulatedPhone phone;
  final _Palette colors;
  final double unit;

  const _CallScreen({required this.phone, required this.colors, required this.unit});

  @override
  Widget build(BuildContext context) {
    final call = phone.call;
    Widget action(String label, Color color, VoidCallback onTap) => _target(
      onTap,
      Container(
        padding: EdgeInsets.symmetric(horizontal: unit * 0.6, vertical: unit * 0.25),
        decoration: BoxDecoration(
          color: color,
          borderRadius: BorderRadius.circular(unit),
        ),
        child: Text(label, style: const TextStyle(color: Color(0xFFFFFFFF))),
      ),
    );
    return ColoredBox(
      color: colors.background.withValues(alpha: 0.96),
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(call.name ?? call.number ?? '', style: TextStyle(fontSize: unit * 0.6)),
            Text(call.state.name, style: TextStyle(color: colors.dim)),
            SizedBox(height: unit * 0.5),
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                if (call.state == CarPlayCallState.ringing) ...[
                  action('Answer', _phoneColor, phone.answer),
                  SizedBox(width: unit * 0.5),
                ],
                action('End', const Color(0xFFFF3B30), phone.hangUp),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _SiriOrb extends StatelessWidget {
  final double unit;

  const _SiriOrb({required this.unit});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Container(
        width: unit * 1.2,
        height: unit * 1.2,
        decoration: const BoxDecoration(
          shape: BoxShape.circle,
          gradient: SweepGradient(
            colors: [
              Color(0xFF5E5CE6),
              Color(0xFFFF375F),
              Color(0xFF64D2FF),
              Color(0xFF5E5CE6),
            ],
          ),
        ),
      ),
    );
  }
}
