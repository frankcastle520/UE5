// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';

import 'settings_log_view.dart';

/// Page showing the list of log files.
class SettingsLogList extends StatelessWidget {
  const SettingsLogList({Key? key}) : super(key: key);

  static const String route = '/logs';

  @override
  Widget build(BuildContext context) {
    return LogListSettingsPage(
      AppLocalizations.of(context)!.settingsDialogApplicationLogLabel,
      SettingsLogView.route,
    );
  }
}
