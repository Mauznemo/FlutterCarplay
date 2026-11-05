// SPDX-License-Identifier: GPL-3.0-or-later
import 'package:carplay/carplay.dart';

import 'knobs.dart';

/// What this head unit tells the adapter and the phone it is.
///
/// Read when the adapter is configured: on Start, and again whenever the adapter is
/// plugged back in. The frame size is left to follow the view, which is what a head
/// unit almost always wants.
///
/// `CP_CLUSTER=1` adds an instrument cluster screen, shown small in the corner of the
/// projection, the way a car would show it behind the wheel.
CarPlayConfig headUnitConfig() => CarPlayConfig(
  name: 'Flutter Head Unit',
  cluster: Knobs.cluster ? const CarPlayCluster(width: 800, height: 240) : null,
  // The adapter applies a band change on its next reboot. 5 GHz is its default and
  // what wireless CarPlay prefers.
  wifiBand: CarPlayWifiBand.fiveGhz,
  // This machine's microphone, which is the only one the CPC200-CCPA has.
  microphone: CarPlayMicrophone.headUnit,
);
