// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Windows.Forms;

namespace UnrealGameSync.Forms
{
	public partial class PresetsWindow : Form
	{
		private readonly IDictionary<Guid, WorkspaceSyncCategory> _uniqueIdToCategory;
		private readonly IDictionary<string, Preset> _roles;

		private readonly Action<Preset>? _copyToGlobal;
		private readonly Action<Preset>? _copyToWorkspace;

		public string PresetName { get; set; } = String.Empty;

		public PresetsWindow(
			IDictionary<Guid, WorkspaceSyncCategory> uniqueIdToCategory,
			string presetName,
			IDictionary<string, Preset> roles,
			Action<Preset>? copyToGlobal = null,
			Action<Preset>? copyToWorkspace = null
			)
		{
			_uniqueIdToCategory = uniqueIdToCategory;
			_roles = roles;
			_copyToGlobal = copyToGlobal;
			_copyToWorkspace = copyToWorkspace;

			InitializeComponent();

			// add an empty string to be able to clear the current role
			RoleSelector.Items.Add(String.Empty);
			foreach (string rolesKey in roles.Keys)
			{
				RoleSelector.Items.Add(rolesKey);
			}

			RoleSelector.SelectedItem = presetName;
			RoleSelector.SelectedIndexChanged += RoleSelector_SelectedIndexChanged;

			SyncFilters.SelectionMode = DataGridViewSelectionMode.CellSelect;

			SetCurrentPreset(presetName);
		}

		private void RoleSelector_SelectedIndexChanged(object? sender, System.EventArgs e)
		{
			if (sender is ComboBox senderComboBox)
			{
				string? name = senderComboBox.SelectedItem?.ToString();
				PopulateGrid(name ?? String.Empty);
				PopulateViews(name ?? String.Empty);
			}
		}

		private void PopulateGrid(string name)
		{
			if (String.IsNullOrWhiteSpace(name))
			{
				SyncFilters.Rows.Clear();
			}
			else
			{
				if (!_roles.TryGetValue(name, out Preset? role))
				{
					return;
				}

				SyncFilters.Rows.Clear();

				foreach (KeyValuePair<Guid, RoleCategory> category in role.Categories)
				{
					if (!_uniqueIdToCategory.TryGetValue(category.Key, out WorkspaceSyncCategory? workspaceSyncCategory))
					{
						continue;
					}

					string[] row = new[]
					{
						workspaceSyncCategory.Name, category.Value.Enabled ? "True" : "False",
						String.Join('\n', workspaceSyncCategory.Paths)
					};

					SyncFilters.Rows.Add(row);
				}
			}
		}

		private void PopulateViews(string name)
		{
			if (String.IsNullOrWhiteSpace(name))
			{
				Views.Items.Clear();
			}
			else
			{
				if (_roles.TryGetValue(name, out Preset? role))
				{
					Views.Items.Clear();

					foreach (string view in role.Views)
					{
						ListViewItem item = new ListViewItem(view);
						Views.Items.Add(item);
					}
				}
			}
		}

		private void SelectRole_Click(object sender, EventArgs e)
		{
			string? newPreset = RoleSelector?.SelectedItem?.ToString();

			SetCurrentPreset(newPreset);
		}

		private void SetCurrentPreset(string? newPreset)
		{
			PresetName = String.IsNullOrWhiteSpace(newPreset) ? String.Empty : newPreset;

			PopulateGrid(PresetName);
			PopulateViews(PresetName);
			CurrentPreset.Text = PresetName;
		}

		private void OkButton_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.OK;
		}

		private void CancButton_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
		}

		private void CopyToGlobal_Click(object sender, EventArgs e)
		{
			CopyTo(_copyToGlobal);
		}

		private void CopyToWorkspace_Click(object sender, EventArgs e)
		{
			CopyTo(_copyToWorkspace);
		}

		private void CopyTo(Action<Preset>? action)
		{
			if (action == null)
			{
				return;
			}

			string? currentRoleName = RoleSelector?.SelectedItem?.ToString();
			if (String.IsNullOrWhiteSpace(currentRoleName))
			{
				return;
			}

			if (_roles.TryGetValue(currentRoleName, out Preset? role))
			{
				action.Invoke(role);
			}
		}
	}
}
