// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealGameSync.Forms
{
	partial class SwitchStreamWarningWindow
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
			flowLayoutPanel1 = new System.Windows.Forms.FlowLayoutPanel();
			Title = new System.Windows.Forms.Label();
			OpenedFiles = new System.Windows.Forms.ListBox();
			Question = new System.Windows.Forms.Label();
			flowLayoutPanel2 = new System.Windows.Forms.FlowLayoutPanel();
			CancButton = new System.Windows.Forms.Button();
			OkButton = new System.Windows.Forms.Button();
			flowLayoutPanel1.SuspendLayout();
			flowLayoutPanel2.SuspendLayout();
			SuspendLayout();
			// 
			// flowLayoutPanel1
			// 
			flowLayoutPanel1.Controls.Add(Title);
			flowLayoutPanel1.Controls.Add(OpenedFiles);
			flowLayoutPanel1.Controls.Add(Question);
			flowLayoutPanel1.Controls.Add(flowLayoutPanel2);
			flowLayoutPanel1.Dock = System.Windows.Forms.DockStyle.Fill;
			flowLayoutPanel1.FlowDirection = System.Windows.Forms.FlowDirection.TopDown;
			flowLayoutPanel1.Location = new System.Drawing.Point(0, 0);
			flowLayoutPanel1.Margin = new System.Windows.Forms.Padding(10);
			flowLayoutPanel1.Name = "flowLayoutPanel1";
			flowLayoutPanel1.Size = new System.Drawing.Size(990, 453);
			flowLayoutPanel1.TabIndex = 0;
			// 
			// Title
			// 
			Title.AutoSize = true;
			Title.Location = new System.Drawing.Point(5, 5);
			Title.Margin = new System.Windows.Forms.Padding(5);
			Title.Name = "Title";
			Title.Size = new System.Drawing.Size(963, 25);
			Title.TabIndex = 0;
			Title.Text = "You have files open for edit in this workspace. If you continue, you will not be able to submit them until you switch back.";
			// 
			// OpenedFiles
			// 
			OpenedFiles.FormattingEnabled = true;
			OpenedFiles.HorizontalScrollbar = true;
			OpenedFiles.ItemHeight = 25;
			OpenedFiles.Location = new System.Drawing.Point(5, 40);
			OpenedFiles.Margin = new System.Windows.Forms.Padding(5);
			OpenedFiles.Name = "OpenedFiles";
			OpenedFiles.Size = new System.Drawing.Size(985, 254);
			OpenedFiles.Sorted = true;
			OpenedFiles.TabIndex = 1;
			// 
			// Question
			// 
			Question.AutoSize = true;
			Question.Location = new System.Drawing.Point(5, 304);
			Question.Margin = new System.Windows.Forms.Padding(5);
			Question.Name = "Question";
			Question.Size = new System.Drawing.Size(237, 25);
			Question.TabIndex = 2;
			Question.Text = "Continue switching streams?";
			// 
			// flowLayoutPanel2
			// 
			flowLayoutPanel2.Controls.Add(CancButton);
			flowLayoutPanel2.Controls.Add(OkButton);
			flowLayoutPanel2.FlowDirection = System.Windows.Forms.FlowDirection.RightToLeft;
			flowLayoutPanel2.Location = new System.Drawing.Point(5, 339);
			flowLayoutPanel2.Margin = new System.Windows.Forms.Padding(5);
			flowLayoutPanel2.Name = "flowLayoutPanel2";
			flowLayoutPanel2.Size = new System.Drawing.Size(960, 47);
			flowLayoutPanel2.TabIndex = 3;
			// 
			// CancButton
			// 
			CancButton.Anchor = System.Windows.Forms.AnchorStyles.None;
			CancButton.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			CancButton.Location = new System.Drawing.Point(830, 0);
			CancButton.Margin = new System.Windows.Forms.Padding(4, 0, 0, 0);
			CancButton.Name = "CancButton";
			CancButton.Size = new System.Drawing.Size(130, 39);
			CancButton.TabIndex = 5;
			CancButton.Text = "Cancel";
			CancButton.UseVisualStyleBackColor = true;
			CancButton.Click += CancButton_Click;
			// 
			// OkButton
			// 
			OkButton.Anchor = System.Windows.Forms.AnchorStyles.None;
			OkButton.Location = new System.Drawing.Point(692, 0);
			OkButton.Margin = new System.Windows.Forms.Padding(4, 0, 4, 0);
			OkButton.Name = "OkButton";
			OkButton.Size = new System.Drawing.Size(130, 39);
			OkButton.TabIndex = 4;
			OkButton.Text = "Ok";
			OkButton.UseVisualStyleBackColor = true;
			OkButton.Click += OkButton_Click;
			// 
			// SwitchStreamWarningWindow
			// 
			AutoScaleDimensions = new System.Drawing.SizeF(10F, 25F);
			AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			ClientSize = new System.Drawing.Size(990, 453);
			Controls.Add(flowLayoutPanel1);
			Name = "SwitchStreamWarningWindow";
			Text = "Opened files in the current workspace";
			flowLayoutPanel1.ResumeLayout(false);
			flowLayoutPanel1.PerformLayout();
			flowLayoutPanel2.ResumeLayout(false);
			ResumeLayout(false);
		}

		#endregion

		private System.Windows.Forms.FlowLayoutPanel flowLayoutPanel1;
		private System.Windows.Forms.Label Title;
		private System.Windows.Forms.ListBox OpenedFiles;
		private System.Windows.Forms.Label Question;
		private System.Windows.Forms.FlowLayoutPanel flowLayoutPanel2;
		private System.Windows.Forms.Button CancButton;
		private System.Windows.Forms.Button OkButton;
	}
}