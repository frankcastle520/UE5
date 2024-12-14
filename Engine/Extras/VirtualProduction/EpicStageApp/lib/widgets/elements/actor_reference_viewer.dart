// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/preferences.dart';
import 'package:epic_common/theme.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';
import 'package:provider/provider.dart';

import '../../models/engine_connection.dart';
import '../../models/settings/delta_widget_settings.dart';
import 'delta_widget_base.dart';
import 'unreal_widget_base.dart';

/// Displays the name of actors referenced by properties in Unreal Engine.
/// This widget is read-only, so users have to change the value from the editor.
class UnrealActorReferenceViewer extends UnrealWidget {
  const UnrealActorReferenceViewer({
    super.key,
    required super.unrealProperties,
  });

  @override
  State<UnrealActorReferenceViewer> createState() => _UnrealActorDisplayState();
}

class _UnrealActorDisplayState extends State<UnrealActorReferenceViewer>
    with UnrealWidgetStateMixin<UnrealActorReferenceViewer, String> {
  /// The name to display for the selected actor(s).
  String _actorName = '';

  @override
  Widget build(BuildContext context) {
    final TextStyle textStyle = Theme.of(context).textTheme.labelMedium!;

    return Padding(
      padding: EdgeInsets.only(bottom: 16),
      child: TransientPreferenceBuilder(
        preference: Provider.of<DeltaWidgetSettings>(context, listen: false).bIsInResetMode,
        builder: (context, final bool bIsInResetMode) => Row(
          children: [
            Expanded(
              child: Padding(
                padding: EdgeInsets.symmetric(horizontal: DeltaWidgetConstants.widgetOuterXPadding),
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.start,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      propertyLabel,
                      style: textStyle.copyWith(
                        color: textStyle.color!.withOpacity(bIsInResetMode ? 0.4 : 1.0),
                      ),
                    ),
                    Padding(
                      padding: const EdgeInsets.only(
                        left: DeltaWidgetConstants.widgetInnerXPadding,
                        right: DeltaWidgetConstants.widgetInnerXPadding,
                        top: 4,
                      ),
                      child: Container(
                        height: 36,
                        decoration: BoxDecoration(
                          color: bIsInResetMode ? UnrealColors.gray22 : Theme.of(context).colorScheme.background,
                          borderRadius: BorderRadius.all(
                            const Radius.circular(4),
                          ),
                        ),
                        padding: EdgeInsets.symmetric(horizontal: 12),
                        child: Row(
                          children: [
                            Expanded(
                              child: Text(
                                _actorName,
                                overflow: TextOverflow.ellipsis,
                                maxLines: 1,
                                style: Theme.of(context).textTheme.bodyMedium!.copyWith(
                                      color: bIsInResetMode ? UnrealColors.gray56 : UnrealColors.white,
                                      letterSpacing: 0.5,
                                    ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  @override
  void handleOnPropertiesChanged() {
    _updateDisplayName();
  }

  /// Retrieve the name of the referenced actor from the engine.
  void _updateDisplayName() async {
    final SingleSharedValue<String> actorPath = getSingleSharedValue();

    if (actorPath.bHasMultipleValues) {
      setState(() {
        _actorName = AppLocalizations.of(context)!.mismatchedValuesLabel;
      });
      return;
    }

    if (actorPath.value == null || actorPath.value!.isEmpty) {
      setState(() {
        _actorName = AppLocalizations.of(context)!.actorReferenceNoneLabel;
      });

      return;
    }

    final connection = Provider.of<EngineConnectionManager>(context, listen: false);
    final UnrealHttpResponse response = await connection.sendHttpRequest(UnrealHttpRequest(
      url: '/remote/object/call',
      verb: 'PUT',
      body: {
        'objectPath': '/Script/Engine.Default__KismetSystemLibrary',
        'functionName': 'GetDisplayName',
        'parameters': {
          'Object': actorPath.value!,
        },
      },
    ));

    if (response.code != HttpResponseCode.ok) {
      setState(() {
        _actorName = AppLocalizations.of(context)!.actorReferenceInvalidLabel;
      });
      return;
    }

    final cineCameraName = response.body['ReturnValue'];
    if (cineCameraName is! String) {
      setState(() {
        _actorName = AppLocalizations.of(context)!.actorReferenceInvalidLabel;
      });
      return;
    }

    setState(() {
      _actorName = cineCameraName;
    });
  }
}
