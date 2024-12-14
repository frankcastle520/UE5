// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';

import 'package:epic_common/preferences.dart';
import 'package:epic_common/utilities/json.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/foundation.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';
import 'package:vector_math/vector_math_64.dart' as vec;

import 'selected_actor_settings.dart';

enum ProjectionMode {
  azimuthal,
  orthographic,
  perspective,
  uv,
}

/// Holds user settings used to render and control the stage map.
class StageMapSettings {
  StageMapSettings(PreferencesBundle preferences, BuildContext context)
      : bIsInTransformMode = preferences.persistent.getBool(
          'stageMap.bIsInMultiSelectMode',
          defaultValue: false,
        ),
        perRootActorPreferences = preferences.persistent.getCustomValue(
          'stageMap.perRootActor',
          defaultValue: {},
          adapter: JsonAdapter(
            serializer: (Map<String, _PerRootActorPreferenceGroup> map) => map.map(
              (String key, _PerRootActorPreferenceGroup value) => MapEntry(key, value.toJson()),
            ),
            deserializer: (json) {
              if (!(json is Map<String, dynamic>)) {
                return {};
              }

              return json.map(
                (key, value) => MapEntry(key, _PerRootActorPreferenceGroup.fromJson(value)),
              );
            },
          ),
        ),
        focalPoint = preferences.transient.get(
          'stageMap.focalPoint',
          defaultValue: Offset(0.5, 0.5),
        ),
        zoomLevel = preferences.transient.get(
          'stageMap.zoomLevel',
          defaultValue: 1.0,
        ) {
    projectionMode = PerRootActorPreference<ProjectionMode>(
      context: context,
      perRootActorPreferences: perRootActorPreferences,
      getter: (group) => group.projectionMode,
      setter: (group, value) => group.copyWith(projectionMode: value),
    );

    cameraAngle = PerRootActorPreference<vec.Vector2>(
      context: context,
      perRootActorPreferences: perRootActorPreferences,
      getter: (group) => group.cameraAngle.clone(), // Clone this to avoid mutating the cached vector
      setter: (group, value) => group.copyWith(cameraAngle: value),
    );
  }

  /// If true, the map is in transform mode and the user can pinch/pan to adjust lightcard scale/rotation/location.
  /// If false, the map is in map mode, and the user can pinch/pan to move the map or long-press pins to drag them
  /// around.
  final Preference<bool> bIsInTransformMode;

  /// The camera's current projection mode for stage actor locations.
  late final PerRootActorPreference<ProjectionMode> projectionMode;

  /// The camera's current rotation setting in degrees, where X is yaw and Y is pitch.
  late final PerRootActorPreference<vec.Vector2> cameraAngle;

  /// Map from root actor path to preferences associated only with that root actor.
  final Preference<Map<String, _PerRootActorPreferenceGroup>> perRootActorPreferences;

  /// The location on the map in center of the stage map view, assuming a 1:1 aspect ratio.
  final TransientPreference<Offset> focalPoint;

  /// The zoom level of the stage map view.
  final TransientPreference<double> zoomLevel;

  /// Dispose of any resources held by this.
  void dispose() {
    projectionMode.dispose();
    cameraAngle.dispose();
  }
}

/// Contains/serializes/deserializes all preferences that depend on the currently selected root actor.
class _PerRootActorPreferenceGroup {
  _PerRootActorPreferenceGroup({
    ProjectionMode? projectionMode,
    vec.Vector2? cameraAngle,
  })  : this.projectionMode = projectionMode ?? ProjectionMode.azimuthal,
        this.cameraAngle = cameraAngle ?? vec.Vector2(0, 90);

  /// Create a preference group from JSON data.
  _PerRootActorPreferenceGroup.fromJson(Map<String, dynamic> json)
      : this(
          projectionMode: jsonToEnumValue(json['ProjectionMode'], ProjectionMode.values),
          cameraAngle: jsonToVector2(json['CameraAngle']),
        );

  /// Convert a preference group to JSON data.
  Map<String, dynamic> toJson() => {
        'ProjectionMode': enumToJsonValue<ProjectionMode>(projectionMode),
        'CameraAngle': cameraAngle.toJson(),
      };

  /// Copy the group, changing one or more of its values.
  _PerRootActorPreferenceGroup copyWith({
    ProjectionMode? projectionMode,
    vec.Vector2? cameraAngle,
  }) =>
      _PerRootActorPreferenceGroup(
        projectionMode: projectionMode ?? this.projectionMode,
        cameraAngle: cameraAngle ?? this.cameraAngle,
      );

  /// The camera's current projection mode for stage actor locations.
  final ProjectionMode projectionMode;

  /// The camera's current rotation setting in degrees, where X is yaw and Y is pitch.
  final vec.Vector2 cameraAngle;
}

/// Provides access to a single preference that depends on the currently selected root actor.
class PerRootActorPreference<T> extends ChangeNotifier implements ValueListenable<T> {
  PerRootActorPreference({
    required BuildContext context,
    required this.perRootActorPreferences,
    required this.getter,
    required this.setter,
  })  : _displayClusterRootPath = Provider.of<SelectedActorSettings>(context, listen: false).displayClusterRootPath,
        _group = _PerRootActorPreferenceGroup() {
    _subscriptions.addAll([
      _displayClusterRootPath.listen((value) => _retrieveAndNotify()),
      perRootActorPreferences.listen((value) => _retrieveAndNotify()),
    ]);

    _retrieveAndNotify();
  }

  /// Preference containing the map from root actor paths to associated settings.
  final Preference<Map<String, _PerRootActorPreferenceGroup>> perRootActorPreferences;

  /// Function which retrieves the preference from the given preference [group].
  final T Function(_PerRootActorPreferenceGroup group) getter;

  /// Function which returns a new preference group by taking an existing [group] and applying the preference's new
  /// [value] to it.
  final _PerRootActorPreferenceGroup Function(_PerRootActorPreferenceGroup group, T value) setter;

  /// Preference containing the path of the selected root actor.
  final Preference<String> _displayClusterRootPath;

  /// Subscriptions to clean up when this is disposed.
  final List<StreamSubscription> _subscriptions = [];

  /// Cached group containing the preference.
  _PerRootActorPreferenceGroup _group;

  /// Clean up any references held by this.
  @override
  void dispose() {
    _subscriptions.forEach((subscription) => subscription.cancel());

    super.dispose();
  }

  /// The current value of the preference.
  @override
  T get value => getter(_group);

  set value(T newValue) {
    if (value == newValue) {
      return;
    }

    final String path = _displayClusterRootPath.getValue();
    final Map<String, _PerRootActorPreferenceGroup> prefMap = perRootActorPreferences.getValue();
    final _PerRootActorPreferenceGroup group = prefMap[path] ?? _PerRootActorPreferenceGroup();

    prefMap[path] = setter(group, newValue);

    perRootActorPreferences.setValue(prefMap); // This will in turn notify us, causing us to update the cached value
  }

  /// Update the cached value of the preference based on the stored data and notify listeners.
  void _retrieveAndNotify() {
    final String path = _displayClusterRootPath.getValue();

    // Cache the group rather than using the getter here in case the getter needs to run additional operations at
    // the time of access. For example, we may want each access to create a new copy rather than returning the same
    // cached copy.
    _group = perRootActorPreferences.getValue()[path] ?? _PerRootActorPreferenceGroup();

    notifyListeners();
  }
}
