// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from android_auto's AndroidAutoView, which has the same job: keep the two in
// step when either changes how it maps touch or places the texture.
import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:flutter/widgets.dart';

import 'carplay_controller.dart';

/// Renders the projected phone screen and forwards touches to it.
///
/// This is the whole point of the package: the projection is a [Texture] in the widget
/// tree, not a separate window, so a host app can put anything it likes on top of it
/// with ordinary Flutter widgets.
///
/// ```dart
/// Stack(
///   children: [
///     CarPlayView(controller: controller),
///     MyStatusBar(),
///   ],
/// )
/// ```
///
/// Anything drawn over the view wins the hit test, so an overlay button takes its own
/// taps and the phone never sees them. Touches that land on the projection itself are
/// mapped into projected video pixels and sent on, which is what makes the phone's UI
/// usable rather than just visible.
///
/// The view tells the controller its size on every layout, and with no size in the
/// [CarPlayConfig] the phone is asked for a frame exactly that big when the session
/// starts, so it lays its interface out for the view and is drawn one to one. Unlike
/// Android Auto, CarPlay takes any size, so there are no margins to crop. Put widgets
/// *beside* the view to take space from the phone, and *over* it to cover part of
/// what the phone draws. A view that changes size after the start keeps the frame it
/// started with, fitted by [fit], until the next start.
///
/// Sizes are measured in physical pixels with [MediaQuery.devicePixelRatioOf]. A host
/// app that draws on a fixed design canvas and scales it to the screen (a [FittedBox]
/// or [Transform.scale] near the root) should publish the ratio inside that canvas in a
/// [MediaQuery], or the phone is asked for a picture of the wrong size and it is drawn
/// resampled rather than one to one.
class CarPlayView extends StatefulWidget {
  /// The session to render.
  final CarPlayController controller;

  /// Shown whenever there is no live picture from the phone connected now: before the
  /// first frame, after [CarPlayController.stop], and from the moment a connection
  /// is lost until the next stream's first frame. See [CarPlayController.hasVideo].
  ///
  /// Nothing, so whatever is behind the view, when left null.
  final Widget? placeholder;

  /// How the projected surface is fitted into the available space.
  ///
  /// Touch coordinates are mapped through the same fit, so changing this stays
  /// consistent between what is drawn and where taps land.
  final BoxFit fit;

  /// Whether touches on the projection are forwarded to the phone.
  ///
  /// Turn it off for a head unit that drives input some other way, for instance from a
  /// real touchscreen wired straight into [CarPlayController.sendTouch].
  final bool enableTouch;

  /// Creates a view for [controller].
  const CarPlayView({
    super.key,
    required this.controller,
    this.placeholder,
    this.fit = BoxFit.contain,
    this.enableTouch = true,
  });

  @override
  State<CarPlayView> createState() => _CarPlayViewState();
}

class _CarPlayViewState extends State<CarPlayView> {
  /// Every finger currently down, in the order it arrived, which is the order the
  /// phone is given them in. Keyed by Flutter's pointer id.
  final Map<int, CarPlayTouchPoint> _pointers = {};

  /// Flutter pointer id to the small index the protocol wants.
  ///
  /// Flutter's ids are an ever growing counter across the whole app, and the adapter
  /// expects small slots: 0 to 4, reused as fingers come and go.
  final Map<int, int> _slots = {};

  static const int _maxFingers = 5;

  /// The projection's rectangle inside this widget, in local coordinates, as of the
  /// last layout. Touch mapping needs it and paint already computed it.
  Rect _projection = Rect.zero;

  /// The video size that [_projection] was computed against.
  Size _source = Size.zero;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        // Before there is a texture as well as after: the frame size is settled when
        // the session starts, which is before the first frame. Physical pixels, because
        // that is what the frame is asked for in.
        final size = constraints.biggest;
        if (size.isFinite && !size.isEmpty) {
          widget.controller.setViewSize(size * _pixelRatio(context));
        }
        return _buildProjection(constraints);
      },
    );
  }

  Widget _buildProjection(BoxConstraints constraints) {
    return ListenableBuilder(
      listenable: widget.controller,
      builder: (context, _) {
        final textureId = widget.controller.textureId;
        // Not the texture id alone: the texture outlives a connection, and drawing it
        // then shows the last frame of a phone that has gone, frozen, which looks
        // exactly like a head unit that has hung.
        if (textureId == null || !widget.controller.hasVideo) {
          // The projection went away mid gesture, so the fingers it was tracking are
          // not coming back up. Forget them, or the next tap reports itself as a
          // second finger on a screen nothing is touching.
          _pointers.clear();
          _slots.clear();
          return widget.placeholder ?? const SizedBox.expand();
        }
        // The phone's actual size when it is known, the requested one until then. They
        // usually agree, and when they do not it is the phone that is right.
        final info = widget.controller.videoInfo;
        final source = Size(
          (info?.width ?? widget.controller.config.width ?? 1280).toDouble(),
          (info?.height ?? widget.controller.config.height ?? 720).toDouble(),
        );
        final pixelRatio = _pixelRatio(context);
        _source = source;
        _projection = _snapToPixels(
          _fitProjection(source, constraints.biggest, widget.fit),
          source,
          pixelRatio,
        );
        // In physical pixels, because that is what decides whether the texture is
        // being shrunk: a 1280 pixel wide video in a 640 logical pixel view on a
        // screen at 2x is drawn one to one.
        widget.controller.setDisplaySize(_projection.size * pixelRatio);
        // Placed by hand rather than through a FittedBox, so that it lands on whole
        // pixels. Clipped, so a BoxFit that overflows stays inside the view.
        final view = SizedBox.fromSize(
          size: _resolveBox(source, constraints.biggest),
          child: ClipRect(
            child: Stack(
              children: [
                Positioned.fromRect(
                  rect: _projection,
                  // A Texture, except for an implementation with no native video to
                  // show, which draws its own screen here.
                  child: CarPlayPlatform.instance.buildProjection(context, textureId),
                ),
              ],
            ),
          ),
        );
        if (!widget.enableTouch) {
          return view;
        }
        return Listener(
          // The projection swallows what lands on it rather than letting it fall
          // through to whatever is behind, which is what a real head unit screen
          // does. Widgets drawn *over* the view are unaffected: they are later in the
          // stack and win the hit test before this ever sees the pointer.
          behavior: HitTestBehavior.opaque,
          onPointerDown: _onPointerDown,
          onPointerMove: _onPointerMove,
          onPointerUp: _onPointerUp,
          onPointerCancel: _onPointerCancel,
          child: view,
        );
      },
    );
  }

  /// Physical pixels per logical pixel, in this widget's own coordinate space.
  ///
  /// From [MediaQuery] rather than [View], because the window's ratio is only this
  /// widget's ratio when nothing between the two scales the tree. A host app that lays
  /// out on a fixed design canvas and scales it to the screen, which is common on head
  /// units with odd screen sizes, publishes the ratio inside its canvas in MediaQuery.
  /// Reading the window's instead measured a view drawn at 0.948 as if it were 1.0: a
  /// frame chosen for a picture too big, and a texture shrunk by the converter to one
  /// size and then resampled by Flutter to another. Where nothing overrides it the two
  /// are the same number.
  static double _pixelRatio(BuildContext context) =>
      MediaQuery.maybeDevicePixelRatioOf(context) ?? View.of(context).devicePixelRatio;

  /// [rect] moved and sized onto whole physical pixels.
  ///
  /// A texture drawn at a fractional position is resampled even at one to one, which
  /// blurs every edge by half a pixel, and centring leaves exactly such a half whenever
  /// the space to spare is odd. And a texture within a pixel or two of its own size is
  /// drawn at exactly that size, rather than stretched across the difference, which
  /// would resample the whole picture to gain a pixel. The frame is rounded to even
  /// pixels, which can leave the video one pixel short of the view, so this is the
  /// common case.
  static Rect _snapToPixels(Rect rect, Size source, double pixelRatio) {
    if (rect.isEmpty || pixelRatio <= 0) {
      return rect;
    }
    var width = rect.width * pixelRatio;
    var height = rect.height * pixelRatio;
    if ((width - source.width).abs() <= 2 && (height - source.height).abs() <= 2) {
      width = source.width;
      height = source.height;
    } else {
      width = width.roundToDouble();
      height = height.roundToDouble();
    }
    final left = (rect.center.dx * pixelRatio - width / 2).roundToDouble();
    final top = (rect.center.dy * pixelRatio - height / 2).roundToDouble();
    return Rect.fromLTWH(
      left / pixelRatio,
      top / pixelRatio,
      width / pixelRatio,
      height / pixelRatio,
    );
  }

  /// The space the view takes: [box], or the video's own size along an axis the
  /// constraints leave unbounded, the way FittedBox sizes itself to its child there.
  static Size _resolveBox(Size source, Size box) => Size(
    box.width.isFinite ? box.width : source.width,
    box.height.isFinite ? box.height : source.height,
  );

  /// Where the projection is drawn inside a box of [box], for [fit].
  ///
  /// The same arithmetic [FittedBox] does, repeated here because it keeps the result
  /// rather than throwing it away: mapping a touch back onto the video needs it.
  static Rect _fitProjection(Size source, Size box, BoxFit fit) {
    if (source.isEmpty) {
      return Rect.zero;
    }
    final resolved = _resolveBox(source, box);
    if (resolved.isEmpty) {
      return Rect.zero;
    }
    final sizes = applyBoxFit(fit, source, resolved);
    // Centred, as FittedBox would. _snapToPixels then moves it by under a pixel, and
    // its result is what is painted, so touch and picture agree by construction.
    return Alignment.center.inscribe(sizes.destination, Offset.zero & resolved);
  }

  /// Maps a position in this widget onto projected video pixels.
  ///
  /// Returns null when the projection has no size yet. Positions outside the
  /// projection are clamped rather than rejected, so a drag that wanders onto the
  /// letterbox bars keeps tracking the edge instead of stopping dead.
  CarPlayTouchPoint? _toProjected(int slot, Offset local) {
    if (_projection.isEmpty || _source.isEmpty) {
      return null;
    }
    final x = (local.dx - _projection.left) * _source.width / _projection.width;
    final y = (local.dy - _projection.top) * _source.height / _projection.height;
    return CarPlayTouchPoint(
      id: slot,
      x: x.clamp(0.0, _source.width - 1).round(),
      y: y.clamp(0.0, _source.height - 1).round(),
    );
  }

  /// The lowest pointer index not already in use.
  int _claimSlot(int pointer) {
    final taken = _slots.values.toSet();
    var slot = 0;
    while (taken.contains(slot)) {
      slot++;
    }
    _slots[pointer] = slot;
    return slot;
  }

  void _onPointerDown(PointerDownEvent event) {
    // A touch starting on the letterbox bars is not a touch on the phone's screen, so
    // it is dropped outright rather than clamped onto the nearest edge. Clamping is
    // right for a drag that wanders off, wrong for a tap that was never on.
    if (!_projection.contains(event.localPosition)) {
      return;
    }
    final slot = _claimSlot(event.pointer);
    final point = _toProjected(slot, event.localPosition);
    // The adapter takes five fingers. A sixth is ignored rather than sent with a slot
    // the adapter would clamp onto one of the others.
    if (point == null || slot >= _maxFingers) {
      _slots.remove(event.pointer);
      return;
    }
    final first = _pointers.isEmpty;
    _pointers[event.pointer] = point;
    _send(
      first ? CarPlayTouchAction.down : CarPlayTouchAction.pointerDown,
      event.pointer,
    );
  }

  void _onPointerMove(PointerMoveEvent event) {
    final slot = _slots[event.pointer];
    if (slot == null) {
      return;
    }
    final point = _toProjected(slot, event.localPosition);
    if (point == null) {
      return;
    }
    _pointers[event.pointer] = point;
    _send(CarPlayTouchAction.move, event.pointer);
  }

  void _onPointerUp(PointerUpEvent event) => _release(event.pointer);

  // Cancel goes out as an ordinary up. The adapter has no cancel action, and a phone
  // left holding a finger that never lifts stays stuck in whatever gesture it thought
  // was in progress.
  void _onPointerCancel(PointerCancelEvent event) => _release(event.pointer);

  void _release(int pointer) {
    if (!_pointers.containsKey(pointer)) {
      return;
    }
    final last = _pointers.length == 1;
    // The finger going up is still in the report, and actionIndex is what says which
    // one it was.
    _send(last ? CarPlayTouchAction.up : CarPlayTouchAction.pointerUp, pointer);
    _pointers.remove(pointer);
    _slots.remove(pointer);
  }

  /// Sends the current pointer set, with [pointer] named as the one this is about.
  void _send(CarPlayTouchAction action, int pointer) {
    final order = _pointers.keys.toList();
    final points = [for (final key in order) _pointers[key]!];
    if (points.isEmpty) {
      return;
    }
    widget.controller.sendTouch(
      action,
      points,
      actionIndex: order.indexOf(pointer).clamp(0, points.length - 1),
    );
  }
}
