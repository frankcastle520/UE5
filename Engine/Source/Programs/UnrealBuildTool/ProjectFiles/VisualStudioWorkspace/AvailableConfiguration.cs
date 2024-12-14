// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;

namespace UnrealBuildTool
{
	internal class AvailableConfiguration
	{
		public HashSet<string> Configurations { get; set; } = new();

		public HashSet<string> Platforms { get; set; } = new();

		public string TargetPath { get; set; } = string.Empty;

		public string ProjectPath { get; set; } = string.Empty;

		public string TargetType { get; set; } = string.Empty;
	}
}
