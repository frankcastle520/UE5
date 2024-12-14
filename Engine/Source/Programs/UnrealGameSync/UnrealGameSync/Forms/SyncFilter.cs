// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;
using UnrealGameSync.Forms;

namespace UnrealGameSync
{
	partial class SyncFilter : Form
	{
		class CheckListItem
		{
			public string Name => GetName();
			public Guid UniqueId { get; set; } = Guid.Empty;
			public string CategoryName { get; set; } = String.Empty;
			public bool Locked { get; set; } = false;
			public string Preset { get; set; } = String.Empty;
			public bool Enabled { get; set; } = false;

			public override string ToString()
			{
				return GetName();
			}

			private string GetName()
			{
				if (String.IsNullOrWhiteSpace(Preset))
				{
					return CategoryName;
				}

				string suffix = Locked ? " - Locked" : String.Empty;
				return $"{CategoryName} [Preset: {Preset}{suffix}]";
			}
		}

		private readonly Dictionary<Guid, WorkspaceSyncCategory> _uniqueIdToCategory;
		private readonly string _roleName;
		private readonly IDictionary<string, Preset> _roles;
		private readonly ConfigSection? _perforceSection;
		
		public FilterSettings GlobalFilter;
		public FilterSettings WorkspaceFilter;
		
		public SyncFilter(
			Dictionary<Guid, WorkspaceSyncCategory> uniqueIdToCategory,
			string roleName,
			IDictionary<string, Preset> roles,
			FilterSettings globalFilter,
			FilterSettings workspaceFilter,
			ConfigSection? perforceSection)
		{
			InitializeComponent();
			Font = new System.Drawing.Font("Segoe UI", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));

			_uniqueIdToCategory = uniqueIdToCategory;
			_roleName = roleName;
			_roles = roles;
			_perforceSection = perforceSection;
			
			GlobalFilter = globalFilter;
			WorkspaceFilter = workspaceFilter;

			Preset? role = new Preset();

			_roles.TryGetValue(_roleName, out role);
			
			Dictionary<Guid, bool> syncCategories = WorkspaceSyncCategory.GetDefault(_uniqueIdToCategory.Values);

			WorkspaceSyncCategory.ApplyDelta(syncCategories, GlobalFilter.GetCategories());
			GlobalControl.SetView(GlobalFilter.View.ToArray());
			SetExcludedCategories(GlobalControl.CategoriesCheckList, _uniqueIdToCategory, role, syncCategories);
			GlobalControl.SyncAllProjects.Checked = GlobalFilter.AllProjects ?? false;
			GlobalControl.IncludeAllProjectsInSolution.Checked = GlobalFilter.AllProjectsInSln ?? false;
			GlobalControl.GenerateUprojectSpecificSolution.Checked = GlobalFilter.UprojectSpecificSln ?? false;

			WorkspaceSyncCategory.ApplyDelta(syncCategories, WorkspaceFilter.GetCategories());
			WorkspaceControl.SetView(WorkspaceFilter.View.ToArray());
			SetExcludedCategories(WorkspaceControl.CategoriesCheckList, _uniqueIdToCategory, role, syncCategories);
			WorkspaceControl.SyncAllProjects.Checked = WorkspaceFilter.AllProjects ?? GlobalFilter.AllProjects ?? false;
			WorkspaceControl.IncludeAllProjectsInSolution.Checked = WorkspaceFilter.AllProjectsInSln ?? GlobalFilter.AllProjectsInSln ?? false;
			WorkspaceControl.GenerateUprojectSpecificSolution.Checked = WorkspaceFilter.UprojectSpecificSln ?? GlobalFilter.UprojectSpecificSln ?? false;

			GlobalControl.CategoriesCheckList.ItemCheck += GlobalControl_CategoriesCheckList_ItemCheck;
			GlobalControl.SyncAllProjects.CheckStateChanged += GlobalControl_SyncAllProjects_CheckStateChanged;
			GlobalControl.IncludeAllProjectsInSolution.CheckStateChanged += GlobalControl_IncludeAllProjectsInSolution_CheckStateChanged;
			GlobalControl.GenerateUprojectSpecificSolution.CheckStateChanged += GlobalControl_GenerateUprojectSpecificSolution_CheckStateChanged;
		}

		private void GlobalControl_CategoriesCheckList_ItemCheck(object? sender, ItemCheckEventArgs e)
		{
			// do not allow changing the value of locked categories
			if (sender is System.Windows.Forms.CheckedListBox listBox)
			{
				if (listBox.Items[e.Index] is CheckListItem item && item.Locked)
				{
					e.NewValue = e.CurrentValue;
					return;
				}
			}

			WorkspaceControl.CategoriesCheckList.SetItemCheckState(e.Index, e.NewValue);
		}

		private void GlobalControl_SyncAllProjects_CheckStateChanged(object? sender, EventArgs e)
		{
			WorkspaceControl.SyncAllProjects.Checked = GlobalControl.SyncAllProjects.Checked;
		}

		private void GlobalControl_IncludeAllProjectsInSolution_CheckStateChanged(object? sender, EventArgs e)
		{
			WorkspaceControl.IncludeAllProjectsInSolution.Checked = GlobalControl.IncludeAllProjectsInSolution.Checked;
		}

		private void GlobalControl_GenerateUprojectSpecificSolution_CheckStateChanged(object? sender, EventArgs e)
		{
			WorkspaceControl.GenerateUprojectSpecificSolution.Checked = GlobalControl.GenerateUprojectSpecificSolution.Checked;
		}

		private static void SetExcludedCategories(
			CheckedListBox listBox, 
			Dictionary<Guid, WorkspaceSyncCategory> uniqueIdToFilter,
			Preset? role,
			Dictionary<Guid, bool> categoryIdToSetting)
		{
			listBox.Items.Clear();
			
			foreach (WorkspaceSyncCategory filter in uniqueIdToFilter.Values)
			{
				if (filter.Hidden)
				{
					continue;
				}

				CheckState state = CheckState.Checked;
				if (!categoryIdToSetting[filter.UniqueId])
				{
					state = CheckState.Unchecked;
				}
					
				CheckListItem item = new CheckListItem()
				{
					UniqueId = filter.UniqueId,
					CategoryName = filter.Name,
					Locked = false,
					Preset = String.Empty,
					Enabled = filter.Enable
				};

				if (role != null && role.Categories.TryGetValue(filter.UniqueId, out RoleCategory? category))
				{
					item.Preset = role.Name;
					item.Locked = true;
					item.Enabled = category.Enabled;
					state = category.Enabled ? CheckState.Checked : CheckState.Unchecked;
				}
					
				listBox.Items.Add(item, state);
			}
		}

		private void GetSettings(out FilterSettings newGlobalFilter, out FilterSettings newWorkspaceFilter)
		{
			Dictionary<Guid, bool> defaultSyncCategories = WorkspaceSyncCategory.GetDefault(_uniqueIdToCategory.Values);

			newGlobalFilter = new FilterSettings();
			newGlobalFilter.View.AddRange(GlobalControl.GetView());
			newGlobalFilter.AllProjects = GlobalControl.SyncAllProjects.Checked;
			newGlobalFilter.AllProjectsInSln = GlobalControl.IncludeAllProjectsInSolution.Checked;
			newGlobalFilter.UprojectSpecificSln = GlobalControl.GenerateUprojectSpecificSolution.Checked;

			Dictionary<Guid, bool> globalSyncCategories = GetCategorySettings(GlobalControl.CategoriesCheckList, GlobalFilter.GetCategories());
			newGlobalFilter.SetCategories(WorkspaceSyncCategory.GetDelta(defaultSyncCategories, globalSyncCategories));

			newWorkspaceFilter = new FilterSettings();
			newWorkspaceFilter.View.AddRange(WorkspaceControl.GetView());
			newWorkspaceFilter.AllProjects = (WorkspaceControl.SyncAllProjects.Checked == newGlobalFilter.AllProjects) ? (bool?)null : WorkspaceControl.SyncAllProjects.Checked;
			newWorkspaceFilter.AllProjectsInSln = (WorkspaceControl.IncludeAllProjectsInSolution.Checked == newGlobalFilter.AllProjectsInSln) ? (bool?)null : WorkspaceControl.IncludeAllProjectsInSolution.Checked;
			newWorkspaceFilter.UprojectSpecificSln = (WorkspaceControl.GenerateUprojectSpecificSolution.Checked == newGlobalFilter.UprojectSpecificSln) ? (bool?)null : WorkspaceControl.GenerateUprojectSpecificSolution.Checked;

			Dictionary<Guid, bool> workspaceSyncCategories = GetCategorySettings(WorkspaceControl.CategoriesCheckList, WorkspaceFilter.GetCategories());
			newWorkspaceFilter.SetCategories(WorkspaceSyncCategory.GetDelta(globalSyncCategories, workspaceSyncCategories));
		}

		private Dictionary<Guid, bool> GetCategorySettings(CheckedListBox listBox, IEnumerable<KeyValuePair<Guid, bool>> originalSettings)
		{
			Dictionary<Guid, bool> result = new Dictionary<Guid, bool>();
			for (int idx = 0; idx < listBox.Items.Count; idx++)
			{
				if (listBox.Items[idx] is CheckListItem item)
				{
					Guid uniqueId = item.UniqueId;
					if (!result.ContainsKey(uniqueId))
					{
						result[uniqueId] = listBox.GetItemCheckState(idx) == CheckState.Checked;
					}	
				}
			}
			foreach (KeyValuePair<Guid, bool> originalSetting in originalSettings)
			{
				if (!_uniqueIdToCategory.ContainsKey(originalSetting.Key))
				{
					result[originalSetting.Key] = originalSetting.Value;
				}
			}
			return result;
		}

		private void OkButton_Click(object sender, EventArgs e)
		{
			GetSettings(out FilterSettings newGlobalFilter, out FilterSettings newWorkspaceFilter);

			if (newGlobalFilter.View.Any(x => x.Contains("//", StringComparison.Ordinal)) || newWorkspaceFilter.View.Any(x => x.Contains("//", StringComparison.Ordinal)))
			{
				if (MessageBox.Show(this, "Custom views should be relative to the stream root (eg. -/Engine/...).\r\n\r\nFull depot paths (eg. //depot/...) will not match any files.\r\n\r\nAre you sure you want to continue?", "Invalid view", MessageBoxButtons.OKCancel) != System.Windows.Forms.DialogResult.OK)
				{
					return;
				}
			}

			GlobalFilter = newGlobalFilter;
			WorkspaceFilter = newWorkspaceFilter;

			DialogResult = DialogResult.OK;
		}

		private void CancButton_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
		}

		private void ShowCombinedView_Click(object sender, EventArgs e)
		{
			GetSettings(out FilterSettings newGlobalFilter, out FilterSettings newWorkspaceFilter);

			string[] filter = UserSettings.GetCombinedSyncFilter(_uniqueIdToCategory, _roleName, _roles, newGlobalFilter, newWorkspaceFilter, _perforceSection);
			if (filter.Length == 0)
			{
				filter = new string[] { "All files will be synced." };
			}
			
#pragma warning disable CA2000 // Dispose objects before losing scope
			CombinedViewsWindow combinedViewsWindow = new CombinedViewsWindow(filter);
#pragma warning restore CA2000

			combinedViewsWindow.FormBorderStyle = FormBorderStyle.FixedDialog;
			combinedViewsWindow.ShowDialog();
		}
	}
}
