// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/preferences.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

/// Which side of the screen a tab is on.
enum FloatingWindowSide {
  left,
  right,
}

/// Generic holder for user settings used to configure floating windows.
/// Takes [preference] bundle containing user settings and a [prefix] string used to disambiguate per-window settings
/// [FloatingWindowSettings]
class FloatingWindowSettings {
  FloatingWindowSettings(PreferencesBundle preferences, String prefix)
      : side = preferences.persistent.getEnum<FloatingWindowSide>(
          '$prefix.side',
          defaultValue: FloatingWindowSide.right,
          enumValues: FloatingWindowSide.values,
        ),
        yAxis = preferences.persistent.getDouble(
          '$prefix.yAxis',
          defaultValue: 1.0,
        ),
        bIsDocked = preferences.persistent.getBool(
          '$prefix.bIsDocked',
          defaultValue: true,
        );

  /// Which side of the screen the window is on.
  Preference<FloatingWindowSide> side;

  /// The vertical position at which the tab should sit (as a fraction of its container height).
  Preference<double> yAxis;

  /// Whether the window is docked off-screen.
  Preference<bool> bIsDocked;
}
