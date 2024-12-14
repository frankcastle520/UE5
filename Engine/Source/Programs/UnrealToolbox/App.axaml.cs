// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using EpicGames.Core;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using UnrealToolbox.Plugins.HordeAgent;
using UnrealToolbox.Plugins.HordeProxy;

namespace UnrealToolbox
{
	/// <summary>
	/// Main application class
	/// </summary>
	public sealed partial class App : Application, ITrayAppHost, IAsyncDisposable
	{
		readonly ServiceProvider _serviceProvider;
		readonly ILogger _logger;

		WindowIcon? _normalIcon;
		WindowIcon? _busyIcon;
		WindowIcon? _pausedIcon;
		WindowIcon? _errorIcon;

		SettingsWindow? _settingsWindow;

		Thread? _settingsThread;
		ManualResetEvent? _settingsThreadStop;
		bool _shutdownOnClose;

		/// <summary>
		/// Constructor
		/// </summary>
		public App()
		{
			ServiceCollection serviceCollection = new ServiceCollection();
			serviceCollection.AddLogging(builder => builder.AddEpicDefault());
			serviceCollection.AddSingleton<IHordeClientProvider, HordeClientProvider>();
			serviceCollection.AddSingleton<ToolCatalog>();
			serviceCollection.AddSingleton<IToolCatalog, ToolCatalog>(sp => sp.GetRequiredService<ToolCatalog>());
			serviceCollection.AddSingleton<ITrayAppHost>(this);
			serviceCollection.AddSingleton<SelfUpdateService>();
			serviceCollection.AddSingleton<ToolboxNotificationManager>();

			// Configure plugins
			serviceCollection.AddSingleton<ITrayAppPlugin, HordeAgentPlugin>();
			serviceCollection.AddSingleton<ITrayAppPlugin, HordeProxyPlugin>();

			_serviceProvider = serviceCollection.BuildServiceProvider();
			_logger = _serviceProvider.GetRequiredService<ILogger<App>>();
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			if (_settingsThreadStop != null)
			{
				_settingsThreadStop.Set();
				_settingsThread?.Join();
				_settingsThreadStop.Dispose();
				_settingsThreadStop = null;
			}
			await _serviceProvider.DisposeAsync();
		}

		/// <inheritdoc/>
		public override void Initialize()
		{
			AvaloniaXamlLoader.Load(this);

			_normalIcon = (WindowIcon)Resources["StatusNormal"]!;
			_busyIcon = (WindowIcon)Resources["StatusBusy"]!;
			_pausedIcon = (WindowIcon)Resources["StatusPaused"]!;
			_errorIcon = (WindowIcon)Resources["StatusError"]!;

			RefreshPlugins();
			UpdateMenu();

			ToolCatalog toolCatalog = _serviceProvider.GetRequiredService<ToolCatalog>();
			toolCatalog.OnItemsChanged += UpdateMenu;
			toolCatalog.Start();

			SelfUpdateService selfUpdateService = _serviceProvider.GetRequiredService<SelfUpdateService>();
			selfUpdateService.OnUpdateReady += UpdateReady;
			selfUpdateService.Start();

			ToolboxNotificationManager notificationManager = _serviceProvider.GetRequiredService<ToolboxNotificationManager>();
			notificationManager.Start();

			_settingsThreadStop = new ManualResetEvent(false);
			_settingsThread = new Thread(WaitForEvents);
			_settingsThread.Start();
		}

		private void WaitForEvents()
		{
			using EventWaitHandle closeEvent = new EventWaitHandle(false, EventResetMode.AutoReset, Program.CloseEventName);
			using EventWaitHandle refreshEvent = new EventWaitHandle(false, EventResetMode.AutoReset, Program.RefreshEventName);
			using EventWaitHandle settingsEvent = new EventWaitHandle(false, EventResetMode.AutoReset, Program.SettingsEventName);
			for (; ; )
			{
				int index = WaitHandle.WaitAny(new[] { closeEvent, refreshEvent, settingsEvent, _settingsThreadStop! });
				if (index == 0)
				{
					Dispatcher.UIThread.Post(() => CloseMainThread());
				}
				else if (index == 1)
				{
					Dispatcher.UIThread.Post(() => RefreshPlugins());
				}
				else if (index == 2)
				{
					Dispatcher.UIThread.Post(() => OpenSettings());
				}
				else
				{
					break;
				}
			}
		}

		void CloseMainThread()
		{
			((IClassicDesktopStyleApplicationLifetime)ApplicationLifetime!).Shutdown();
		}

		private void UpdateReady()
		{
			Dispatcher.UIThread.Post(() => UpdateReadyMainThread());
		}

		void UpdateReadyMainThread()
		{
			if (_settingsWindow == null)
			{
				_logger.LogInformation("Shutting down to install update");
				((IClassicDesktopStyleApplicationLifetime)ApplicationLifetime!).Shutdown();
			}
			else
			{
				_logger.LogInformation("Scheduling update when settings window closes");
				_shutdownOnClose = true;
			}
		}

		private void UpdateMenu()
		{
			NativeMenuItem settingsMenuItem = new NativeMenuItem("Settings...");
			settingsMenuItem.Click += TrayIcon_Settings;

			NativeMenuItem exitMenuItem = new NativeMenuItem("Exit");
			exitMenuItem.Click += TrayIcon_Exit;

			NativeMenu contextMenu = new NativeMenu();

			TrayIcon trayIcon = TrayIcon.GetIcons(this)![0];
			trayIcon.Menu = contextMenu;

			foreach (ITrayAppPlugin plugin in _serviceProvider.GetServices<ITrayAppPlugin>())
			{
				plugin.PopulateContextMenu(contextMenu);
			}

			int numItems = contextMenu.Items.Count;
			if (numItems > 0)
			{
				contextMenu.Items.Add(new NativeMenuItemSeparator());
				numItems++;
			}

			IToolCatalog toolCatalog = _serviceProvider.GetRequiredService<IToolCatalog>();
			foreach (IToolCatalogItem item in toolCatalog.Items)
			{
				CurrentToolDeploymentInfo? current = item.Current;
				if (current != null)
				{
					ToolConfig? toolConfig = current.Config;
					if (toolConfig != null && toolConfig.PopupMenu != null)
					{
						AddMenuItems(item, toolConfig.PopupMenu, contextMenu);
					}
				}
			}

			if (contextMenu.Items.Count > numItems)
			{
				contextMenu.Items.Add(new NativeMenuItemSeparator());
			}
			contextMenu.Items.Add(settingsMenuItem);
			contextMenu.Items.Add(exitMenuItem);
		}

		static void AddMenuItems(IToolCatalogItem item, ToolMenuItem toolMenuItem, NativeMenu menu)
		{
			if (!String.IsNullOrEmpty(toolMenuItem.Label))
			{
				NativeMenuItem menuItem = new NativeMenuItem(toolMenuItem.Label);
				if (toolMenuItem.Children != null && toolMenuItem.Children.Count > 0)
				{
					NativeMenu subMenu = new NativeMenu();
					foreach (ToolMenuItem child in toolMenuItem.Children)
					{
						AddMenuItems(item, child, subMenu);
					}
					menuItem.Menu = subMenu;
				}
				else
				{
					if (toolMenuItem.Command != null)
					{
						menuItem.Click += (_, _) => TrayIcon_RunCommand(item, toolMenuItem.Command);
					}
				}
				menu.Items.Add(menuItem);
			}
		}

		static void TrayIcon_RunCommand(IToolCatalogItem item, ToolCommand command)
		{
			ProcessStartInfo startInfo = new ProcessStartInfo(command.FileName);
			if (command.Arguments != null)
			{
				foreach (string argument in command.Arguments)
				{
					startInfo.ArgumentList.Add(argument);
				}
			}
			startInfo.WorkingDirectory = item.Current!.Dir.FullName;
			Process.Start(startInfo);
		}

		private void RefreshPlugins()
		{
			bool update = false;
			foreach (ITrayAppPlugin plugin in _serviceProvider.GetServices<ITrayAppPlugin>())
			{
				update |= plugin.Refresh();
			}
			if (update)
			{
				UpdateMenu();
				_settingsWindow?.Refresh();
			}
		}

		private void TrayIcon_Click(object? sender, EventArgs e)
		{
			RefreshPlugins();
			OpenSettings();
		}

		private void TrayIcon_Settings(object? sender, EventArgs e)
		{
			OpenSettings();
		}

		[DllImport("user32.dll")]
		static extern uint SetForegroundWindow(nint hWnd);

		private void OpenSettings()
		{
			if (_settingsWindow != null)
			{
				_settingsWindow.WindowState &= ~WindowState.Minimized;
				_settingsWindow.BringIntoView();
			}
			else
			{
				_settingsWindow = new SettingsWindow(_serviceProvider);
				_settingsWindow.Closed += SettingsWindow_Closed;
			}

			_settingsWindow.Activate();
			_settingsWindow.Show();

			if (OperatingSystem.IsWindows())
			{
				// Avalonia doesn't seem to activate the window despite the call above, so fall back to pinvoke
				nint? handle = _settingsWindow.TryGetPlatformHandle()?.Handle;
				if (handle.HasValue)
				{
#pragma warning disable CA1806
					SetForegroundWindow(handle.Value);
#pragma warning restore CA1806
				}
			}
		}

		private void SettingsWindow_Closed(object? sender, EventArgs e)
		{
			_settingsWindow = null;

			if (_shutdownOnClose)
			{
				_logger.LogInformation("Running update due to settings window closing");
				((IClassicDesktopStyleApplicationLifetime)ApplicationLifetime!).Shutdown();
			}
		}

		private void TrayIcon_Exit(object? sender, EventArgs e)
		{
			((IClassicDesktopStyleApplicationLifetime)ApplicationLifetime!).Shutdown();
		}

		/// <inheritdoc/>
		public void UpdateStatus()
		{
			Dispatcher.UIThread.Post(() => UpdateStatusMainThread());
		}

		private void UpdateStatusMainThread()
		{
			TrayAppPluginState state = TrayAppPluginState.Undefined;
			List<string> messages = new List<string>();

			foreach (ITrayAppPlugin plugin in _serviceProvider.GetServices<ITrayAppPlugin>())
			{
				TrayAppPluginStatus status = plugin.GetStatus();

				if (status.State >= state)
				{
					if (status.State > state)
					{
						messages.Clear();
						state = status.State;
					}
					if (status.Message != null)
					{
						messages.Add(status.Message);
					}
				}
			}

			TrayIcon trayIcon = TrayIcon.GetIcons(this)![0];
			trayIcon!.Icon = state switch
			{
				TrayAppPluginState.Busy => _busyIcon,
				TrayAppPluginState.Paused => _pausedIcon,
				TrayAppPluginState.Error => _errorIcon,
				_ => _normalIcon
			};
			trayIcon.ToolTipText = String.Join("\n", messages);
		}
	}
}
