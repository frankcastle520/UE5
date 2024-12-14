// Copyright Epic Games, Inc. All Rights Reserved.

using Avalonia.Controls;

namespace UnrealToolbox.Plugins.HordeAgent
{
	partial class HordeAgentSettingsPage : UserControl
	{
		public HordeAgentSettingsPage()
			: this(SettingsContext.Default, null)
		{
		}

		public HordeAgentSettingsPage(SettingsContext context, HordeAgentPlugin? plugin)
		{
			InitializeComponent();

			DataContext = new HordeAgentSettingsViewModel(context, plugin);
		}
	}
}