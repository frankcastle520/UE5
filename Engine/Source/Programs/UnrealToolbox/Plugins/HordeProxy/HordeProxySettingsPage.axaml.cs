// Copyright Epic Games, Inc. All Rights Reserved.

using Avalonia.Controls;

namespace UnrealToolbox.Plugins.HordeProxy
{
	partial class HordeProxySettingsPage : UserControl
	{
		public HordeProxySettingsPage()
			: this(null)
		{
		}

		public HordeProxySettingsPage(HordeProxyPlugin? plugin)
		{
			InitializeComponent();

			DataContext = new HordeProxySettingsViewModel(plugin);
		}
	}
}