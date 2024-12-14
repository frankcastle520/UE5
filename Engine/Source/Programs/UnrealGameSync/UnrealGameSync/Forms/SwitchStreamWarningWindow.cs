// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Windows.Forms;

namespace UnrealGameSync.Forms
{
	public partial class SwitchStreamWarningWindow : Form
	{
		public SwitchStreamWarningWindow(List<string> openedFiles)
		{
			InitializeComponent();

			foreach (string openedFile in openedFiles)
			{
				OpenedFiles.Items.Add(openedFile);
			}
		}

		private void OkButton_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.OK;
		}

		private void CancButton_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
		}
	}
}
