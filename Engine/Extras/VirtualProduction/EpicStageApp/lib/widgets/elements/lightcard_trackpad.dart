//Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';

import 'package:epic_common/theme.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:vector_math/vector_math_64.dart' as vec;

import '../../models/actor_data/light_card_actor_data.dart';
import '../../models/property_modify_operations.dart';
import '../../models/settings/delta_widget_settings.dart';
import '../../models/settings/selected_actor_settings.dart';
import '../../models/unreal_actor_manager.dart';
import '../../models/unreal_property_controller.dart';
import '../../models/unreal_types.dart';
import '../../utilities/constants.dart';
import 'unreal_widget_base.dart';

/// Turns the area into a virtual trackpad, allowing the user to control the selected actors' latitude and longitude
/// by dragging on it.
class LightCardTrackpad extends StatefulWidget {
  const LightCardTrackpad({super.key});

  @override
  State<LightCardTrackpad> createState() => _LightCardTrackpadState();
}

class _LightCardTrackpadState extends State<LightCardTrackpad> {
  /// List of actor classes that can be controlled by the trackpad.
  static const List<String> _controllableClasses = [lightCardClassName, ...colorCorrectWindowClassNames];

  /// Base change rate of latitude/longitude per logical pixel of change in the user's pointer position.
  static const double _positionalDeltaMultiplier = 0.4;

  /// Base change rate of UV coordinates per logical pixel of change in the user's pointer position.
  static const double _uvDeltaMultiplier = 0.004;

  /// Maximum value of latitude at which the "north" pole sits. "South" pole is assumed to be at the negation of this.
  static const _latitudeMax = 90;

  /// Operation used for all property modifications.
  static const _propertyOperation = const AddOperation();

  late final UnrealActorManager _actorManager;
  late final SelectedActorSettings _selectedActorSettings;
  late final DeltaWidgetSettings _deltaSettings;

  /// Property controller for latitude.
  late final UnrealPropertyController<double> _latitudeController;

  /// Property controller for longitude.
  late final UnrealPropertyController<double> _longitudeController;

  /// Property controller for UV coordinates.
  late final UnrealPropertyController<vec.Vector2> _uvController;

  /// Set of property indices for actors whose trackpad controls are reversed for the current drag operation.
  Set<int> _reversedControls = {};

  /// stream subscriptions for watching changes to selected actors.
  StreamSubscription? _selectedActorsSubscription;

  /// True if the user is currently interacting with the trackpad.
  bool _bIsPointerDown = false;

  @override
  void initState() {
    _actorManager = Provider.of<UnrealActorManager>(context, listen: false);
    _selectedActorSettings = Provider.of<SelectedActorSettings>(context, listen: false);
    _deltaSettings = Provider.of<DeltaWidgetSettings>(context, listen: false);

    _latitudeController = UnrealPropertyController(context, bShouldInitTransaction: false);
    _longitudeController = UnrealPropertyController(context, bShouldInitTransaction: false);
    _uvController = UnrealPropertyController(context, bShouldInitTransaction: false);

    _updateTrackedProperties();

    _selectedActorsSubscription = _selectedActorSettings.selectedActors.listen((event) {
      if (mounted) {
        _updateTrackedProperties();
      }
    });

    _actorManager.watchExistingSubscriptions(_onManagedActorsChanged);

    super.initState();
  }

  @override
  void dispose() {
    super.dispose();
    _actorManager.stopWatchingExistingSubscriptions(_onManagedActorsChanged);
    _selectedActorsSubscription?.cancel();
    _latitudeController.dispose();
    _longitudeController.dispose();
    _uvController.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: UnrealColors.gray14.withOpacity(0.5),
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onPanDown: _onPanDown,
        onPanUpdate: _onPanUpdate,
        onPanEnd: _onPanEnd,
        onPanCancel: _onPanCancel,
        child: Center(
          child: AnimatedScale(
            duration: const Duration(milliseconds: 200),
            curve: Curves.easeOut,
            scale: _bIsPointerDown ? 1.15 : 1,
            child: AssetIcon(
              path: 'assets/images/icons/trackpad.svg',
              size: 96,
              color: UnrealColors.white.withOpacity(0.2),
            ),
          ),
        ),
      ),
    );
  }

  /// Gets a list of selected actor paths, filtering out any actors that can't be controlled by the trackpad.
  /// If [bGetUVActors] is true, return a list of only UV light cards. Otherwise, return a list of only non-UV actors.
  List<String> _getSelectedActorPaths({required bool bGetUVActors}) {
    final List<String> validActorPaths = [];

    for (final String actorPath in _selectedActorSettings.selectedActors.getValue()) {
      final UnrealObject? actor = _actorManager.getActorAtPath(actorPath);

      if (actor == null) {
        continue;
      }

      if (!actor.isAny(_controllableClasses)) {
        // Actor is not a class with position properties, so leave it out entirely
        continue;
      }

      // Filter to UV/non-UV actors
      final LightCardActorData? lightCardActorData = actor.getPerClassData<LightCardActorData>();
      if (lightCardActorData?.bIsUV != bGetUVActors) {
        continue;
      }

      validActorPaths.add(actorPath);
    }

    return validActorPaths;
  }

  /// Update tracked properties for both latitude and longitude controllers.
  void _updateTrackedProperties() {
    _reversedControls.clear();

    _longitudeController.trackAllProperties(_getPositionalProperties('Longitude'));
    _latitudeController.trackAllProperties(_getPositionalProperties('Latitude'));
    _uvController.trackAllProperties(
      _getSelectedActorPaths(bGetUVActors: true)
          .map((String actorPath) => UnrealProperty(objectPath: actorPath, propertyName: 'UVCoordinates'))
          .toList(growable: false),
    );
  }

  /// Called when the user's input suggests a pan gesture may be about to start.
  void _onPanDown(DragDownDetails details) {
    setState(() {
      _bIsPointerDown = true;
    });
  }

  /// Called whenever a pan gesture's position updates based on new user input.
  void _onPanUpdate(DragUpdateDetails details) {
    // Transaction is global, so this will be shared with the latitude controller
    _longitudeController.beginTransaction();

    final Offset baseDelta = details.delta * _deltaSettings.sensitivity.getValue();
    final Offset positionalDelta = baseDelta * _positionalDeltaMultiplier;

    // Longitude is a simple loop operation at the min/max values, so we can update it directly
    _longitudeController.modifyProperties(
      _propertyOperation,
      values: List.generate(
        _longitudeController.properties.length,
        (_) => positionalDelta.dx,
        growable: false,
      ),
      minMaxBehaviour: PropertyMinMaxBehaviour.loop,
    );

    // Latitude requires special handling at the min/max values. When we reach either pole, the longitude rotates by
    // 180 degrees and changes in latitude delta reverses direction to continue moving smoothly.
    // For example, if the user is dragging up to move north, once they reach the north pole, they expect to continue
    // circling the globe in the same direction, i.e. starting to move south on the opposite longitudinal side of the
    // globe.
    _latitudeController.properties.indexed.forEach((propertyPair) {
      final int propertyIndex = propertyPair.$1;
      final WidgetControlledUnrealProperty<double>? property = propertyPair.$2;

      final double? currentValue = property?.value;
      if (currentValue == null) {
        return;
      }

      final bool bIsReversed = _reversedControls.contains(propertyIndex);

      double deltaY = positionalDelta.dy;

      // Latitude moves the opposite direction of where the user would expect on a trackpad, so negate it by default
      if (!bIsReversed) {
        deltaY = -deltaY;
      }

      double newValue = currentValue + deltaY;
      final double absNewValue = newValue.abs();

      if (absNewValue < _latitudeMax) {
        // Just move the distance specified
        _latitudeController.modifyProperty(_propertyOperation, propertyIndex, value: deltaY);
        return;
      }

      // Move to the pole
      final double distanceToPole = (_latitudeMax - absNewValue) * newValue.sign;
      _latitudeController.modifyProperty(_propertyOperation, propertyIndex, value: distanceToPole);

      // Flip longitude
      _longitudeController.modifyProperty(
        const AddOperation(),
        propertyIndex,
        value: 180,
        minMaxBehaviour: PropertyMinMaxBehaviour.loop,
      );

      // Move the remaining distance away from the pole
      final double remainingDistance = (_latitudeMax - absNewValue) * newValue.sign;
      _latitudeController.modifyProperty(_propertyOperation, propertyIndex, value: remainingDistance);

      // Toggle whether this latitude's direction is reversed
      if (bIsReversed) {
        _reversedControls.remove(propertyIndex);
      } else {
        _reversedControls.add(propertyIndex);
      }
    });

    // UV coordinates can be modified directly without any clamping/looping
    final Offset uvDelta = baseDelta * _uvDeltaMultiplier;
    final uvDeltaVector = vec.Vector2(uvDelta.dx, uvDelta.dy);

    _uvController.modifyProperties(
      _propertyOperation,
      values: List.generate(
        _uvController.properties.length,
        (_) => uvDeltaVector,
        growable: false,
      ),
      minMaxBehaviour: PropertyMinMaxBehaviour.ignore,
    );
  }

  /// Called when pan/drag gestures ends.
  void _onPanEnd(DragEndDetails details) {
    _onPanFinished();
  }

  /// Called when pan/drag gesture is cancelled.
  void _onPanCancel() {
    _onPanFinished();
  }

  /// Called when a pan gesture ends for any reason.
  void _onPanFinished() {
    setState(() {
      _bIsPointerDown = false;
    });

    _longitudeController.endTransaction();
    _reversedControls.clear();
  }

  /// Get a list of UnrealProperties corresponding to the [propertyName] on all of the selected, controllable non-UV
  /// actors. This will modify the property name to account for actor types where the parameters are in a nested struct.
  List<UnrealProperty> _getPositionalProperties(String propertyName) {
    return _getSelectedActorPaths(bGetUVActors: false).map((actorPath) {
      final UnrealObject? actor = _actorManager.getActorAtPath(actorPath);

      final String pathToProperty;

      if (actor?.isAny(colorCorrectWindowClassNames) == true) {
        // CCWs store their positional properties in a sub-structure
        pathToProperty = 'PositionalParams.' + propertyName;
      } else {
        pathToProperty = propertyName;
      }

      return UnrealProperty(objectPath: actorPath, propertyName: pathToProperty);
    }).toList(growable: false);
  }

  /// Called when any of the actors in the [UnrealActorManager] changes.
  void _onManagedActorsChanged(ActorUpdateDetails details) {
    if (!mounted) {
      return;
    }

    final Set<String> newActorPaths = details.addedActors.map((actor) => actor.path).toSet();

    // If any selected actors were added, update our tracked properties.
    // This is necessary to catch new actors, which may be selected before they're registered with the actor manager.
    bool bShouldUpdate = false;
    for (final String actorPath in _selectedActorSettings.selectedActors.getValue()) {
      if (newActorPaths.contains(actorPath)) {
        bShouldUpdate = true;
        break;
      }
    }

    if (bShouldUpdate) {
      _updateTrackedProperties();
    }
  }
}
