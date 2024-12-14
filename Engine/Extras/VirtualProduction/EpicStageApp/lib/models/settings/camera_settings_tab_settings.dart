// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/preferences.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

/// Possible modes for the camera settings tab's left column.
enum CameraSettingsLeftColumnMode {
  camera,
  focus,
}

/// Holds user settings used in the Camera Settings tab.
class CameraSettingsTabSettings {
  CameraSettingsTabSettings(PreferencesBundle preferences)
      : selectedCamera = preferences.transient.get(
          'cameraSettingsTab.selectedCamera',
          defaultValue: '',
        ),
        leftColumnMode = preferences.persistent.getEnum(
          'cameraSettingsTab.mode',
          defaultValue: CameraSettingsLeftColumnMode.focus,
          enumValues: CameraSettingsLeftColumnMode.values,
        );

  /// The path of the camera currently selected for editing in the camera settings tab.
  final TransientPreference<String> selectedCamera;

  /// The selected mode for the camera settings tab's left column.
  final Preference<CameraSettingsLeftColumnMode> leftColumnMode;
}
