// Copyright Epic Games, Inc. All Rights Reserved.

using Avalonia;
using Avalonia.Controls;
using EpicGames.Core;

namespace UnrealToolbox
{
	static class Program
	{
		const string MutexName = "UnrealToolbox-Mutex";
		public const string CloseEventName = "UnrealToolbox-Close";
		public const string RefreshEventName = "UnrealToolbox-Refresh"; // Note: this event is set by InstallerCustomActions when agent is installed.
		public const string SettingsEventName = "UnrealToolbox-Settings";

		public static SelfUpdateState? Update { get; private set; }

		public static FileReference? LogFile { get; } = GetLogFile();

		[STAThread]
		public static int Main(string[] args)
		{
			if (!args.Any(x => x.Equals("-NoUpdate", StringComparison.OrdinalIgnoreCase)))
			{
				Update = SelfUpdateState.TryCreate("Unreal Toolbox", args);
			}

			for (; ; )
			{
				if (Update != null && Update.TryLaunchLatest())
				{
					return 0;
				}

				int result = RealMain(args);
				if (Update == null || !Update.IsUpdatePending())
				{
					return result;
				}
			}
		}

		static int RealMain(string[] args)
		{
			using EventWaitHandle closeEvent = new EventWaitHandle(false, EventResetMode.AutoReset, CloseEventName);
			if (args.Any(x => x.Equals("-Close", StringComparison.OrdinalIgnoreCase)))
			{
				closeEvent.Set();
				return 0;
			}

			using EventWaitHandle refreshEvent = new EventWaitHandle(false, EventResetMode.AutoReset, RefreshEventName);
			if (args.Any(x => x.Equals("-Refresh", StringComparison.OrdinalIgnoreCase)))
			{
				refreshEvent.Set();
				return 0;
			}

			using EventWaitHandle settingsEvent = new EventWaitHandle(false, EventResetMode.AutoReset, SettingsEventName);
			if (!args.Any(x => x.Equals("-Quiet", StringComparison.OrdinalIgnoreCase)))
			{
				settingsEvent.Set();
			}

			using SingleInstanceMutex mutex = new SingleInstanceMutex(MutexName);
			if (!mutex.Wait(0))
			{
				return 1;
			}

			if (LogFile != null)
			{
				Log.AddFileWriter("Default", LogFile);
			}

			AppBuilder builder = BuildAvaloniaApp();
			try
			{
				builder.StartWithClassicDesktopLifetime(args, ShutdownMode.OnExplicitShutdown);
			}
			finally
			{
				if (builder.Instance is App app)
				{
					Task.Run(async () => await app.DisposeAsync()).Wait();
				}
			}

			return 0;
		}

		// Avalonia configuration, don't remove; also used by visual designer.
		public static AppBuilder BuildAvaloniaApp()
		{
			return AppBuilder.Configure<App>()
				.UsePlatformDetect()
				.LogToTrace();
		}

		static FileReference? GetLogFile()
		{
			DirectoryReference? appDataDir = DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.LocalApplicationData);
			if (appDataDir == null)
			{
				return null;
			}
			return FileReference.Combine(appDataDir, "Epic Games", "Unreal Toolbox", "Log.txt");
		}
	}

	class SingleInstanceMutex : IDisposable
	{
		readonly Mutex _mutex;
		bool _locked;

		public SingleInstanceMutex(string name)
		{
			_mutex = new Mutex(false, name);
		}

		public void Release()
		{
			if (_locked)
			{
				_mutex.ReleaseMutex();
				_locked = false;
			}
		}

		public bool Wait(int timeout)
		{
			if (!_locked)
			{
				try
				{
					_locked = _mutex.WaitOne(timeout);
				}
				catch (AbandonedMutexException)
				{
					_locked = true;
				}
			}
			return _locked;
		}

		public void Dispose()
		{
			Release();
			_mutex.Dispose();
		}
	}
}
