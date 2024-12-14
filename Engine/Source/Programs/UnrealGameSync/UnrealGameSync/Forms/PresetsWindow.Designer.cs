// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealGameSync.Properties;

namespace UnrealGameSync.Forms
{
	partial class PresetsWindow
	{
		/// <summary>
		/// Required designer variable.
		/// </summary>
		private System.ComponentModel.IContainer components = null;

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}
			base.Dispose(disposing);
		}

		#region Windows Form Designer generated code

		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
			System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle3 = new System.Windows.Forms.DataGridViewCellStyle();
			System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle4 = new System.Windows.Forms.DataGridViewCellStyle();
			System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(PresetsWindow));
			RoleSelector = new System.Windows.Forms.ComboBox();
			SyncFilters = new System.Windows.Forms.DataGridView();
			Category = new System.Windows.Forms.DataGridViewTextBoxColumn();
			Include = new System.Windows.Forms.DataGridViewTextBoxColumn();
			Paths = new System.Windows.Forms.DataGridViewTextBoxColumn();
			Views = new System.Windows.Forms.ListView();
			label3 = new System.Windows.Forms.Label();
			label4 = new System.Windows.Forms.Label();
			CurrentPreset = new System.Windows.Forms.Label();
			SelectPreset = new System.Windows.Forms.Button();
			CancButton = new System.Windows.Forms.Button();
			OkButton = new System.Windows.Forms.Button();
			CopyToGlobal = new System.Windows.Forms.Button();
			CopyToWorkspace = new System.Windows.Forms.Button();
			groupBox1 = new System.Windows.Forms.GroupBox();
			groupBox2 = new System.Windows.Forms.GroupBox();
			((System.ComponentModel.ISupportInitialize)SyncFilters).BeginInit();
			groupBox1.SuspendLayout();
			groupBox2.SuspendLayout();
			SuspendLayout();
			// 
			// RoleSelector
			// 
			RoleSelector.FormattingEnabled = true;
			RoleSelector.Location = new System.Drawing.Point(17, 108);
			RoleSelector.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
			RoleSelector.Name = "RoleSelector";
			RoleSelector.Size = new System.Drawing.Size(171, 33);
			RoleSelector.TabIndex = 0;
			// 
			// SyncFilters
			// 
			SyncFilters.AllowUserToAddRows = false;
			SyncFilters.AllowUserToDeleteRows = false;
			SyncFilters.BackgroundColor = System.Drawing.SystemColors.Window;
			SyncFilters.CausesValidation = false;
			SyncFilters.ClipboardCopyMode = System.Windows.Forms.DataGridViewClipboardCopyMode.EnableAlwaysIncludeHeaderText;
			dataGridViewCellStyle3.Alignment = System.Windows.Forms.DataGridViewContentAlignment.MiddleLeft;
			dataGridViewCellStyle3.BackColor = System.Drawing.SystemColors.Control;
			dataGridViewCellStyle3.Font = new System.Drawing.Font("Segoe UI", 9F);
			dataGridViewCellStyle3.ForeColor = System.Drawing.SystemColors.WindowText;
			dataGridViewCellStyle3.SelectionBackColor = System.Drawing.SystemColors.Highlight;
			dataGridViewCellStyle3.SelectionForeColor = System.Drawing.SystemColors.HighlightText;
			dataGridViewCellStyle3.WrapMode = System.Windows.Forms.DataGridViewTriState.True;
			SyncFilters.ColumnHeadersDefaultCellStyle = dataGridViewCellStyle3;
			SyncFilters.ColumnHeadersHeightSizeMode = System.Windows.Forms.DataGridViewColumnHeadersHeightSizeMode.AutoSize;
			SyncFilters.Columns.AddRange(new System.Windows.Forms.DataGridViewColumn[] { Category, Include, Paths });
			dataGridViewCellStyle4.Alignment = System.Windows.Forms.DataGridViewContentAlignment.MiddleLeft;
			dataGridViewCellStyle4.BackColor = System.Drawing.SystemColors.Window;
			dataGridViewCellStyle4.Font = new System.Drawing.Font("Segoe UI", 9F);
			dataGridViewCellStyle4.ForeColor = System.Drawing.SystemColors.ControlText;
			dataGridViewCellStyle4.SelectionBackColor = System.Drawing.SystemColors.Highlight;
			dataGridViewCellStyle4.SelectionForeColor = System.Drawing.SystemColors.HighlightText;
			dataGridViewCellStyle4.WrapMode = System.Windows.Forms.DataGridViewTriState.False;
			SyncFilters.DefaultCellStyle = dataGridViewCellStyle4;
			SyncFilters.Location = new System.Drawing.Point(19, 37);
			SyncFilters.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
			SyncFilters.Name = "SyncFilters";
			SyncFilters.ReadOnly = true;
			SyncFilters.RowHeadersVisible = false;
			SyncFilters.RowHeadersWidth = 62;
			SyncFilters.Size = new System.Drawing.Size(920, 333);
			SyncFilters.TabIndex = 1;
			SyncFilters.TabStop = false;
			// 
			// Category
			// 
			Category.HeaderText = "Category";
			Category.MinimumWidth = 128;
			Category.Name = "Category";
			Category.ReadOnly = true;
			Category.SortMode = System.Windows.Forms.DataGridViewColumnSortMode.NotSortable;
			Category.Width = 256;
			// 
			// Include
			// 
			Include.HeaderText = "Include";
			Include.MinimumWidth = 64;
			Include.Name = "Include";
			Include.ReadOnly = true;
			Include.SortMode = System.Windows.Forms.DataGridViewColumnSortMode.NotSortable;
			Include.Width = 64;
			// 
			// Paths
			// 
			Paths.HeaderText = "Paths";
			Paths.MinimumWidth = 128;
			Paths.Name = "Paths";
			Paths.ReadOnly = true;
			Paths.SortMode = System.Windows.Forms.DataGridViewColumnSortMode.NotSortable;
			Paths.Width = 320;
			// 
			// Views
			// 
			Views.CausesValidation = false;
			Views.GridLines = true;
			Views.HeaderStyle = System.Windows.Forms.ColumnHeaderStyle.None;
			Views.Location = new System.Drawing.Point(19, 37);
			Views.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
			Views.Name = "Views";
			Views.ShowGroups = false;
			Views.Size = new System.Drawing.Size(918, 184);
			Views.TabIndex = 4;
			Views.UseCompatibleStateImageBehavior = false;
			Views.View = System.Windows.Forms.View.List;
			// 
			// label3
			// 
			label3.AutoSize = true;
			label3.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
			label3.Location = new System.Drawing.Point(17, 15);
			label3.Margin = new System.Windows.Forms.Padding(4, 0, 4, 0);
			label3.Name = "label3";
			label3.Size = new System.Drawing.Size(139, 25);
			label3.TabIndex = 5;
			label3.Text = "Current Preset:";
			// 
			// label4
			// 
			label4.AutoSize = true;
			label4.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
			label4.Location = new System.Drawing.Point(17, 78);
			label4.Margin = new System.Windows.Forms.Padding(4, 0, 4, 0);
			label4.Name = "label4";
			label4.Size = new System.Drawing.Size(126, 25);
			label4.TabIndex = 6;
			label4.Text = "Select Preset:";
			// 
			// CurrentPreset
			// 
			CurrentPreset.AutoSize = true;
			CurrentPreset.Font = new System.Drawing.Font("Segoe UI", 9F);
			CurrentPreset.Location = new System.Drawing.Point(141, 15);
			CurrentPreset.Margin = new System.Windows.Forms.Padding(4, 0, 4, 0);
			CurrentPreset.Name = "CurrentPreset";
			CurrentPreset.Size = new System.Drawing.Size(0, 25);
			CurrentPreset.TabIndex = 7;
			// 
			// SelectPreset
			// 
			SelectPreset.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
			SelectPreset.Location = new System.Drawing.Point(199, 108);
			SelectPreset.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
			SelectPreset.Name = "SelectPreset";
			SelectPreset.Size = new System.Drawing.Size(70, 38);
			SelectPreset.TabIndex = 8;
			SelectPreset.Text = "select";
			SelectPreset.UseVisualStyleBackColor = true;
			SelectPreset.Click += SelectRole_Click;
			// 
			// CancButton
			// 
			CancButton.Anchor = System.Windows.Forms.AnchorStyles.None;
			CancButton.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			CancButton.Location = new System.Drawing.Point(821, 857);
			CancButton.Margin = new System.Windows.Forms.Padding(4, 0, 0, 0);
			CancButton.Name = "CancButton";
			CancButton.Size = new System.Drawing.Size(124, 43);
			CancButton.TabIndex = 10;
			CancButton.Text = "Cancel";
			CancButton.UseVisualStyleBackColor = true;
			CancButton.Click += CancButton_Click;
			// 
			// OkButton
			// 
			OkButton.Anchor = System.Windows.Forms.AnchorStyles.None;
			OkButton.Location = new System.Drawing.Point(689, 857);
			OkButton.Margin = new System.Windows.Forms.Padding(4, 0, 4, 0);
			OkButton.Name = "OkButton";
			OkButton.Size = new System.Drawing.Size(124, 43);
			OkButton.TabIndex = 9;
			OkButton.Text = "Ok";
			OkButton.UseVisualStyleBackColor = true;
			OkButton.Click += OkButton_Click;
			// 
			// CopyToGlobal
			// 
			CopyToGlobal.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
			CopyToGlobal.Location = new System.Drawing.Point(17, 802);
			CopyToGlobal.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
			CopyToGlobal.Name = "CopyToGlobal";
			CopyToGlobal.Size = new System.Drawing.Size(200, 38);
			CopyToGlobal.TabIndex = 11;
			CopyToGlobal.Text = "Copy to Global";
			CopyToGlobal.UseVisualStyleBackColor = true;
			CopyToGlobal.Click += CopyToGlobal_Click;
			// 
			// CopyToWorkspace
			// 
			CopyToWorkspace.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
			CopyToWorkspace.Location = new System.Drawing.Point(225, 802);
			CopyToWorkspace.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
			CopyToWorkspace.Name = "CopyToWorkspace";
			CopyToWorkspace.Size = new System.Drawing.Size(200, 38);
			CopyToWorkspace.TabIndex = 12;
			CopyToWorkspace.Text = "Copy to Workspace";
			CopyToWorkspace.UseVisualStyleBackColor = true;
			CopyToWorkspace.Click += CopyToWorkspace_Click;
			// 
			// groupBox1
			// 
			groupBox1.Controls.Add(SyncFilters);
			groupBox1.Location = new System.Drawing.Point(17, 157);
			groupBox1.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
			groupBox1.Name = "groupBox1";
			groupBox1.Padding = new System.Windows.Forms.Padding(4, 5, 4, 5);
			groupBox1.Size = new System.Drawing.Size(960, 380);
			groupBox1.TabIndex = 13;
			groupBox1.TabStop = false;
			groupBox1.Text = "Categories";
			// 
			// groupBox2
			// 
			groupBox2.Controls.Add(Views);
			groupBox2.Location = new System.Drawing.Point(17, 547);
			groupBox2.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
			groupBox2.Name = "groupBox2";
			groupBox2.Padding = new System.Windows.Forms.Padding(4, 5, 4, 5);
			groupBox2.Size = new System.Drawing.Size(960, 233);
			groupBox2.TabIndex = 14;
			groupBox2.TabStop = false;
			groupBox2.Text = "Custom View";
			// 
			// PresetsWindow
			// 
			AutoScaleDimensions = new System.Drawing.SizeF(10F, 25F);
			AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			ClientSize = new System.Drawing.Size(1007, 915);
			Controls.Add(groupBox2);
			Controls.Add(groupBox1);
			Controls.Add(CopyToWorkspace);
			Controls.Add(CopyToGlobal);
			Controls.Add(CancButton);
			Controls.Add(OkButton);
			Controls.Add(SelectPreset);
			Controls.Add(CurrentPreset);
			Controls.Add(label4);
			Controls.Add(label3);
			Controls.Add(RoleSelector);
			Font = new System.Drawing.Font("Segoe UI", 9F);
			Icon = (System.Drawing.Icon)resources.GetObject("$this.Icon");
			Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
			Name = "PresetsWindow";
			Text = "Presets";
			((System.ComponentModel.ISupportInitialize)SyncFilters).EndInit();
			groupBox1.ResumeLayout(false);
			groupBox2.ResumeLayout(false);
			ResumeLayout(false);
			PerformLayout();
		}

		#endregion

		private System.Windows.Forms.ComboBox RoleSelector;
		private System.Windows.Forms.DataGridView SyncFilters;
		private System.Windows.Forms.DataGridViewTextBoxColumn Category;
		private System.Windows.Forms.DataGridViewTextBoxColumn Include;
		private System.Windows.Forms.DataGridViewTextBoxColumn Paths;
		private System.Windows.Forms.ListView Views;
		private System.Windows.Forms.Label label3;
		private System.Windows.Forms.Label label4;
		private System.Windows.Forms.Label CurrentPreset;
		private System.Windows.Forms.Button SelectPreset;
		private System.Windows.Forms.Button CancButton;
		private System.Windows.Forms.Button OkButton;
		private System.Windows.Forms.Button CopyToGlobal;
		private System.Windows.Forms.Button CopyToWorkspace;
		private System.Windows.Forms.GroupBox groupBox1;
		private System.Windows.Forms.GroupBox groupBox2;
	}
}