// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealGameSync.Forms
{
	partial class CombinedViewsWindow
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
			System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(CombinedViewsWindow));
			TopPanel = new System.Windows.Forms.Panel();
			ViewsPanel = new System.Windows.Forms.FlowLayoutPanel();
			Views = new System.Windows.Forms.ListBox();
			ControlPanel = new System.Windows.Forms.FlowLayoutPanel();
			CopyToClipBoard = new System.Windows.Forms.Button();
			label1 = new System.Windows.Forms.Label();
			SearchBox = new System.Windows.Forms.TextBox();
			ClearSearch = new System.Windows.Forms.Button();
			TopPanel.SuspendLayout();
			ViewsPanel.SuspendLayout();
			ControlPanel.SuspendLayout();
			SuspendLayout();
			// 
			// TopPanel
			// 
			TopPanel.AutoSize = true;
			TopPanel.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
			TopPanel.Controls.Add(ViewsPanel);
			TopPanel.Controls.Add(ControlPanel);
			TopPanel.Dock = System.Windows.Forms.DockStyle.Fill;
			TopPanel.Location = new System.Drawing.Point(0, 0);
			TopPanel.Name = "TopPanel";
			TopPanel.Size = new System.Drawing.Size(720, 635);
			TopPanel.TabIndex = 0;
			// 
			// ViewsPanel
			// 
			ViewsPanel.AutoSize = true;
			ViewsPanel.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
			ViewsPanel.Controls.Add(Views);
			ViewsPanel.Dock = System.Windows.Forms.DockStyle.Fill;
			ViewsPanel.Location = new System.Drawing.Point(0, 30);
			ViewsPanel.Name = "ViewsPanel";
			ViewsPanel.Size = new System.Drawing.Size(720, 605);
			ViewsPanel.TabIndex = 1;
			// 
			// Views
			// 
			Views.FormattingEnabled = true;
			Views.HorizontalScrollbar = true;
			Views.ItemHeight = 15;
			Views.Location = new System.Drawing.Point(3, 3);
			Views.Name = "Views";
			Views.Size = new System.Drawing.Size(717, 604);
			Views.TabIndex = 2;
			// 
			// ControlPanel
			// 
			ControlPanel.AutoSize = true;
			ControlPanel.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
			ControlPanel.Controls.Add(CopyToClipBoard);
			ControlPanel.Controls.Add(label1);
			ControlPanel.Controls.Add(SearchBox);
			ControlPanel.Controls.Add(ClearSearch);
			ControlPanel.Dock = System.Windows.Forms.DockStyle.Top;
			ControlPanel.Location = new System.Drawing.Point(0, 0);
			ControlPanel.Name = "ControlPanel";
			ControlPanel.Size = new System.Drawing.Size(720, 30);
			ControlPanel.TabIndex = 0;
			// 
			// CopyToClipBoard
			// 
			CopyToClipBoard.Location = new System.Drawing.Point(3, 3);
			CopyToClipBoard.Name = "CopyToClipBoard";
			CopyToClipBoard.Size = new System.Drawing.Size(54, 24);
			CopyToClipBoard.TabIndex = 4;
			CopyToClipBoard.Text = "Copy";
			CopyToClipBoard.UseVisualStyleBackColor = true;
			CopyToClipBoard.Click += CopyToClipBoard_Click;
			// 
			// label1
			// 
			label1.AutoSize = true;
			label1.Location = new System.Drawing.Point(63, 3);
			label1.Margin = new System.Windows.Forms.Padding(3);
			label1.Name = "label1";
			label1.Padding = new System.Windows.Forms.Padding(0, 5, 0, 0);
			label1.Size = new System.Drawing.Size(42, 20);
			label1.TabIndex = 6;
			label1.Text = "Search";
			label1.TextAlign = System.Drawing.ContentAlignment.MiddleCenter;
			// 
			// SearchBox
			// 
			SearchBox.Location = new System.Drawing.Point(111, 3);
			SearchBox.Name = "SearchBox";
			SearchBox.Size = new System.Drawing.Size(108, 23);
			SearchBox.TabIndex = 5;
			SearchBox.TextChanged += SearchBox_TextChanged;
			// 
			// ClearSearch
			// 
			ClearSearch.Location = new System.Drawing.Point(225, 3);
			ClearSearch.Name = "ClearSearch";
			ClearSearch.Size = new System.Drawing.Size(54, 24);
			ClearSearch.TabIndex = 7;
			ClearSearch.Text = "Clear";
			ClearSearch.UseVisualStyleBackColor = true;
			ClearSearch.Click += ClearSearch_Click;
			// 
			// CombinedViewsWindow
			// 
			AutoScaleDimensions = new System.Drawing.SizeF(7F, 15F);
			AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			ClientSize = new System.Drawing.Size(720, 635);
			Controls.Add(TopPanel);
			Icon = (System.Drawing.Icon)resources.GetObject("$this.Icon");
			Name = "CombinedViewsWindow";
			Text = "CombinedViewsWindow";
			TopPanel.ResumeLayout(false);
			TopPanel.PerformLayout();
			ViewsPanel.ResumeLayout(false);
			ControlPanel.ResumeLayout(false);
			ControlPanel.PerformLayout();
			ResumeLayout(false);
			PerformLayout();
		}

		#endregion

		private System.Windows.Forms.Panel TopPanel;
		private System.Windows.Forms.FlowLayoutPanel ControlPanel;
		private System.Windows.Forms.FlowLayoutPanel ViewsPanel;
		private System.Windows.Forms.Button CopyToClipBoard;
		private System.Windows.Forms.Label label1;
		private System.Windows.Forms.TextBox SearchBox;
		private System.Windows.Forms.ListBox Views;
		private System.Windows.Forms.Button ClearSearch;
	}
}