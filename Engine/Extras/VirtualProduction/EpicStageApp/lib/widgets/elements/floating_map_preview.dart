// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';

import 'floating_window.dart';
import 'lightcard_map.dart';

class FloatingMapPreview extends StatelessWidget {
  const FloatingMapPreview({super.key});

  @override
  Widget build(BuildContext context) {
    return FloatingWindow(
      settingsPrefix: 'floatingMapSettings',
      icon: AssetIcon(path: 'packages/epic_common/assets/icons/map.svg'),
      builder: (context) => StageMap(
        bIsControllable: false,
        bShowOnlySelectedPins: true,
        bForceShowFullMap: true,
        pinSize: 32,
      ),
    );
  }
}
