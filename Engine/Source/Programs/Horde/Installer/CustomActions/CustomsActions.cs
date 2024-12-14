// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Deployment.WindowsInstaller;
using System.Windows.Forms;
using static System.Environment;
using System.IO;

namespace HordeInstaller
{
	public class InstallerActions
	{
		[DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
		static extern IntPtr GetForegroundWindow();

		[CustomAction]
		public static ActionResult SelectDataDir(Session session)
		{
			try
			{
				Thread task = new Thread(() => GetDataDir(session));
				task.SetApartmentState(ApartmentState.STA);
				task.Start();
				task.Join();
			}
			catch (Exception ex)
			{
				session.Log("SelectDataDir Exception: {0}\n StackTrace: {1}", ex.Message, ex.StackTrace);
				return ActionResult.Failure;
			}
			return ActionResult.Success;
		}

		private static void GetDataDir(Session session)
		{
			using (FolderBrowserDialog dialog = new FolderBrowserDialog())
			{				
				string defaultPath = @"C:\ProgramData\Epic\Horde\Server";

				if (!Directory.Exists(defaultPath))
				{
					Directory.CreateDirectory(defaultPath);
				}

				dialog.RootFolder = SpecialFolder.Desktop;				
				dialog.SelectedPath = @"C:\ProgramData\Epic\Horde\Server";
				dialog.Description = @"Please select a folder for Horde Server data";
				
				DialogResult result = dialog.ShowDialog(Control.FromHandle(GetForegroundWindow()));

				if (result == DialogResult.OK)
				{
					session["HORDE_DATA_DIR"] = dialog.SelectedPath;
				}
			}
		}
	}
}
