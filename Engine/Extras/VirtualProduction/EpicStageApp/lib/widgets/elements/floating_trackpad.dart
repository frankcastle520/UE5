// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/theme.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';

import 'floating_window.dart';
import 'lightcard_trackpad.dart';

class FloatingTrackpad extends StatelessWidget {
  const FloatingTrackpad({super.key});

  /// The height of the draggable area at the top of the trackpad.
  static const double _dragBarHeight = 40;

  @override
  Widget build(BuildContext context) {
    final size = FloatingWindow.getDefaultSize(context) + const Offset(0, _dragBarHeight);

    return FloatingWindow(
      settingsPrefix: 'floatingTrackpad',
      icon: AssetIcon(path: 'assets/images/icons/trackpad.svg'),
      builder: (context) => Column(
        children: [
          Container(
            height: _dragBarHeight,
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 10),
            alignment: Alignment.centerLeft,
            color: Theme.of(context).colorScheme.surfaceTint,
            child: Row(
              children: [
                AssetIcon(
                  path: 'assets/images/icons/drag_handle.svg',
                  color: UnrealColors.gray56,
                  width: 18,
                  height: 24,
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    AppLocalizations.of(context)!.trackpadTitle.toUpperCase(),
                    style: Theme.of(context).textTheme.headlineSmall!.copyWith(color: UnrealColors.white),
                  ),
                ),
              ],
            ),
          ),
          Expanded(
            child: LightCardTrackpad(),
          ),
        ],
      ),
      draggableInsets: EdgeInsets.only(bottom: size.height - _dragBarHeight),
      size: size,
    );
  }
}
