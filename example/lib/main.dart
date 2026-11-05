// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:io';

import 'package:carplay/carplay.dart';
import 'package:flutter/material.dart';

import 'head_unit_page.dart';

void main() {
  // macOS and Windows get the simulator without asking. This puts it on Linux too, for
  // working on the app with no adapter to hand. An example app knob, not a plugin one.
  if (Platform.environment['CP_SIMULATE'] == '1') {
    CarPlaySimulator.registerWith();
  }
  runApp(const HeadUnitApp());
}

/// The reference integration for the carplay plugin.
///
/// Read it in this order:
///
/// 1. `config.dart`, what the head unit tells the adapter and the phone it is.
/// 2. `head_unit_page.dart`, one controller, one view, and everything else stacked on
///    top of the projection as ordinary Flutter widgets.
/// 3. `head_unit/`, the pieces a real head unit would ship: the status bar and the
///    now playing bar and turn card drawn from metadata.
/// 4. `bench/`, the test bench. A product would not show it to a driver, but every
///    call in it is the one a product would make.
class HeadUnitApp extends StatelessWidget {
  const HeadUnitApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'CarPlay head unit',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF4F8CFF),
          brightness: Brightness.dark,
        ),
      ),
      home: const HeadUnitPage(),
    );
  }
}
