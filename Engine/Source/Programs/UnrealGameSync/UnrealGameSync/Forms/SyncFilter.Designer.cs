// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealGameSync
{
	partial class SyncFilter
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
			System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(SyncFilter));
			OkButton = new System.Windows.Forms.Button();
			CancButton = new System.Windows.Forms.Button();
			FilterTextGlobal = new System.Windows.Forms.TextBox();
			label1 = new System.Windows.Forms.Label();
			TabControl = new System.Windows.Forms.TabControl();
			GlobalTab = new System.Windows.Forms.TabPage();
			GlobalControl = new Controls.SyncFilterControl();
			WorkspaceTab = new System.Windows.Forms.TabPage();
			WorkspaceControl = new Controls.SyncFilterControl();
			ShowCombinedView = new System.Windows.Forms.Button();
			tableLayoutPanel1 = new System.Windows.Forms.TableLayoutPanel();
			tableLayoutPanel2 = new System.Windows.Forms.TableLayoutPanel();
			TabControl.SuspendLayout();
			GlobalTab.SuspendLayout();
			WorkspaceTab.SuspendLayout();
			tableLayoutPanel1.SuspendLayout();
			tableLayoutPanel2.SuspendLayout();
			SuspendLayout();
			// 
			// OkButton
			// 
			OkButton.Anchor = System.Windows.Forms.AnchorStyles.None;
			OkButton.Location = new System.Drawing.Point(868, 0);
			OkButton.Margin = new System.Windows.Forms.Padding(3, 0, 3, 0);
			OkButton.Name = "OkButton";
			OkButton.Size = new System.Drawing.Size(87, 26);
			OkButton.TabIndex = 2;
			OkButton.Text = "Ok";
			OkButton.UseVisualStyleBackColor = true;
			OkButton.Click += OkButton_Click;
			// 
			// CancButton
			// 
			CancButton.Anchor = System.Windows.Forms.AnchorStyles.None;
			CancButton.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			CancButton.Location = new System.Drawing.Point(961, 0);
			CancButton.Margin = new System.Windows.Forms.Padding(3, 0, 0, 0);
			CancButton.Name = "CancButton";
			CancButton.Size = new System.Drawing.Size(87, 26);
			CancButton.TabIndex = 3;
			CancButton.Text = "Cancel";
			CancButton.UseVisualStyleBackColor = true;
			CancButton.Click += CancButton_Click;
			// 
			// FilterTextGlobal
			// 
			FilterTextGlobal.AcceptsReturn = true;
			FilterTextGlobal.Anchor = System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left | System.Windows.Forms.AnchorStyles.Right;
			FilterTextGlobal.BorderStyle = System.Windows.Forms.BorderStyle.None;
			FilterTextGlobal.Font = new System.Drawing.Font("Courier New", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, 0);
			FilterTextGlobal.Location = new System.Drawing.Point(0, 7);
			FilterTextGlobal.Margin = new System.Windows.Forms.Padding(3, 4, 3, 4);
			FilterTextGlobal.Multiline = true;
			FilterTextGlobal.Name = "FilterTextGlobal";
			FilterTextGlobal.Size = new System.Drawing.Size(940, 361);
			FilterTextGlobal.TabIndex = 1;
			FilterTextGlobal.WordWrap = false;
			// 
			// label1
			// 
			label1.Anchor = System.Windows.Forms.AnchorStyles.Left;
			label1.AutoSize = true;
			label1.Location = new System.Drawing.Point(16, 16);
			label1.Margin = new System.Windows.Forms.Padding(0);
			label1.Name = "label1";
			label1.Size = new System.Drawing.Size(917, 15);
			label1.TabIndex = 4;
			label1.Text = "Files synced from Perforce may be filtered by a custom stream view, and list of predefined categories.  Settings for the current workspace override defaults for all workspaces.";
			// 
			// TabControl
			// 
			TabControl.Anchor = System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left | System.Windows.Forms.AnchorStyles.Right;
			TabControl.Controls.Add(GlobalTab);
			TabControl.Controls.Add(WorkspaceTab);
			TabControl.Location = new System.Drawing.Point(24, 43);
			TabControl.Margin = new System.Windows.Forms.Padding(8, 12, 8, 12);
			TabControl.Name = "TabControl";
			TabControl.SelectedIndex = 0;
			TabControl.Size = new System.Drawing.Size(1032, 629);
			TabControl.TabIndex = 5;
			// 
			// GlobalTab
			// 
			GlobalTab.Controls.Add(GlobalControl);
			GlobalTab.Location = new System.Drawing.Point(4, 24);
			GlobalTab.Name = "GlobalTab";
			GlobalTab.Padding = new System.Windows.Forms.Padding(3);
			GlobalTab.Size = new System.Drawing.Size(1024, 601);
			GlobalTab.TabIndex = 0;
			GlobalTab.Text = "All Workspaces";
			GlobalTab.UseVisualStyleBackColor = true;
			// 
			// GlobalControl
			// 
			GlobalControl.BackColor = System.Drawing.SystemColors.Window;
			GlobalControl.Dock = System.Windows.Forms.DockStyle.Fill;
			GlobalControl.Font = new System.Drawing.Font("Segoe UI", 8.25F);
			GlobalControl.Location = new System.Drawing.Point(3, 3);
			GlobalControl.Name = "GlobalControl";
			GlobalControl.Padding = new System.Windows.Forms.Padding(6);
			GlobalControl.Size = new System.Drawing.Size(1018, 595);
			GlobalControl.TabIndex = 0;
			// 
			// WorkspaceTab
			// 
			WorkspaceTab.Controls.Add(WorkspaceControl);
			WorkspaceTab.Location = new System.Drawing.Point(4, 24);
			WorkspaceTab.Name = "WorkspaceTab";
			WorkspaceTab.Padding = new System.Windows.Forms.Padding(3);
			WorkspaceTab.Size = new System.Drawing.Size(1024, 601);
			WorkspaceTab.TabIndex = 3;
			WorkspaceTab.Text = "Current Workspace";
			WorkspaceTab.UseVisualStyleBackColor = true;
			// 
			// WorkspaceControl
			// 
			WorkspaceControl.BackColor = System.Drawing.SystemColors.Window;
			WorkspaceControl.Dock = System.Windows.Forms.DockStyle.Fill;
			WorkspaceControl.Font = new System.Drawing.Font("Segoe UI", 8.25F);
			WorkspaceControl.Location = new System.Drawing.Point(3, 3);
			WorkspaceControl.Name = "WorkspaceControl";
			WorkspaceControl.Padding = new System.Windows.Forms.Padding(6);
			WorkspaceControl.Size = new System.Drawing.Size(1018, 595);
			WorkspaceControl.TabIndex = 0;
			// 
			// ShowCombinedView
			// 
			ShowCombinedView.Anchor = System.Windows.Forms.AnchorStyles.None;
			ShowCombinedView.Location = new System.Drawing.Point(0, 0);
			ShowCombinedView.Margin = new System.Windows.Forms.Padding(0, 0, 3, 0);
			ShowCombinedView.Name = "ShowCombinedView";
			ShowCombinedView.Size = new System.Drawing.Size(174, 26);
			ShowCombinedView.TabIndex = 6;
			ShowCombinedView.Text = "Show Combined Filter";
			ShowCombinedView.UseVisualStyleBackColor = true;
			ShowCombinedView.Click += ShowCombinedView_Click;
			// 
			// tableLayoutPanel1
			// 
			tableLayoutPanel1.ColumnCount = 1;
			tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
			tableLayoutPanel1.Controls.Add(label1, 0, 0);
			tableLayoutPanel1.Controls.Add(TabControl, 0, 1);
			tableLayoutPanel1.Controls.Add(tableLayoutPanel2, 0, 2);
			tableLayoutPanel1.Dock = System.Windows.Forms.DockStyle.Fill;
			tableLayoutPanel1.Location = new System.Drawing.Point(0, 0);
			tableLayoutPanel1.Margin = new System.Windows.Forms.Padding(0);
			tableLayoutPanel1.Name = "tableLayoutPanel1";
			tableLayoutPanel1.Padding = new System.Windows.Forms.Padding(16);
			tableLayoutPanel1.RowCount = 3;
			tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
			tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 100F));
			tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
			tableLayoutPanel1.Size = new System.Drawing.Size(1080, 726);
			tableLayoutPanel1.TabIndex = 7;
			// 
			// tableLayoutPanel2
			// 
			tableLayoutPanel2.AutoSize = true;
			tableLayoutPanel2.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
			tableLayoutPanel2.ColumnCount = 4;
			tableLayoutPanel2.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
			tableLayoutPanel2.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
			tableLayoutPanel2.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
			tableLayoutPanel2.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
			tableLayoutPanel2.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Absolute, 20F));
			tableLayoutPanel2.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Absolute, 20F));
			tableLayoutPanel2.Controls.Add(ShowCombinedView, 0, 0);
			tableLayoutPanel2.Controls.Add(CancButton, 3, 0);
			tableLayoutPanel2.Controls.Add(OkButton, 2, 0);
			tableLayoutPanel2.Dock = System.Windows.Forms.DockStyle.Fill;
			tableLayoutPanel2.Location = new System.Drawing.Point(16, 684);
			tableLayoutPanel2.Margin = new System.Windows.Forms.Padding(0);
			tableLayoutPanel2.Name = "tableLayoutPanel2";
			tableLayoutPanel2.RowCount = 1;
			tableLayoutPanel2.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 100F));
			tableLayoutPanel2.Size = new System.Drawing.Size(1048, 26);
			tableLayoutPanel2.TabIndex = 6;
			// 
			// SyncFilter
			// 
			AcceptButton = OkButton;
			AutoScaleDimensions = new System.Drawing.SizeF(96F, 96F);
			AutoScaleMode = System.Windows.Forms.AutoScaleMode.Dpi;
			CancelButton = CancButton;
			ClientSize = new System.Drawing.Size(1080, 726);
			Controls.Add(tableLayoutPanel1);
			Icon = (System.Drawing.Icon)resources.GetObject("$this.Icon");
			Margin = new System.Windows.Forms.Padding(3, 5, 3, 5);
			MaximizeBox = false;
			MinimizeBox = false;
			MinimumSize = new System.Drawing.Size(674, 340);
			Name = "SyncFilter";
			ShowInTaskbar = false;
			StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
			Text = "Sync Filter";
			TabControl.ResumeLayout(false);
			GlobalTab.ResumeLayout(false);
			WorkspaceTab.ResumeLayout(false);
			tableLayoutPanel1.ResumeLayout(false);
			tableLayoutPanel1.PerformLayout();
			tableLayoutPanel2.ResumeLayout(false);
			ResumeLayout(false);
		}

		#endregion

		private System.Windows.Forms.Button OkButton;
		private System.Windows.Forms.Button CancButton;
		private System.Windows.Forms.TextBox FilterTextGlobal;
		private System.Windows.Forms.Label label1;
		private System.Windows.Forms.TabControl TabControl;
		private System.Windows.Forms.TabPage GlobalTab;
		private System.Windows.Forms.Button ShowCombinedView;
		private System.Windows.Forms.TabPage WorkspaceTab;
		private Controls.SyncFilterControl GlobalControl;
		private Controls.SyncFilterControl WorkspaceControl;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel1;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel2;
	}
}