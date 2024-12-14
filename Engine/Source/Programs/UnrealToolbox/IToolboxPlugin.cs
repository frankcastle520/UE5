// Copyright Epic Games, Inc. All Rights Reserved.

using Avalonia.Controls;
using FluentAvalonia.UI.Controls;

namespace UnrealToolbox
{
	enum TrayAppPluginState
	{
		Undefined,
		Ok,
		Busy,
		Paused,
		Error
	}

	/// <summary>
	/// Reported status of a plugin
	/// </summary>
	record class TrayAppPluginStatus(TrayAppPluginState State, string? Message = null)
	{
		public static TrayAppPluginStatus Default { get; } = new TrayAppPluginStatus(TrayAppPluginState.Undefined);
	}

	/// <summary>
	/// Plugin for the tray app
	/// </summary>
	interface ITrayAppPlugin : IAsyncDisposable
	{
		/// <summary>
		/// Name of the plugin to show in the settings page
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Icon to show on the settings page
		/// </summary>
		IconSource Icon { get; }

		/// <summary>
		/// Refresh the state of this plugin. Called when the application is activated or focussed.
		/// </summary>
		/// <returns>True if the plugin status has changed, and the context menu needs to be rebuilt</returns>
		bool Refresh();

		/// <summary>
		/// Get the current status of this plugin
		/// </summary>
		TrayAppPluginStatus GetStatus();

		/// <summary>
		/// Determines if the plugin should be shown in the settings page
		/// </summary>
		bool HasSettingsPage();

		/// <summary>
		/// Create a settings page for this plugin
		/// </summary>
		Control CreateSettingsPage(SettingsContext context);

		/// <summary>
		/// Allow the plugin to customize the tray icon context menu
		/// </summary>
		void PopulateContextMenu(NativeMenu contextMenu);
	}
}
