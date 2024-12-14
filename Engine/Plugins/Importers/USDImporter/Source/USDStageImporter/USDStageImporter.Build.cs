// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDStageImporter : ModuleRules
	{
		public USDStageImporter(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ApplicationCore",
					"Core",
					"CoreUObject",
					"EditorFramework",
					"EditorStyle", // For the font style on the stage actor customization
					"Engine",
					"GeometryCache",
					"InputCore",
					"JsonUtilities",
					"LevelSequence",
					"MainFrame",
					"MessageLog",
					"MovieScene",
					"PropertyEditor", // For the import options's details customization
					"RenderCore", // So that we can release resources of reimported meshes
					"Slate",
					"SlateCore",
					"UnrealEd",
					"UnrealUSDWrapper",
					"USDClasses",
					"USDSchemas",
					"USDStage",
					"USDUtilities",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"HairStrandsCore",
				}
			);

			PrivateDefinitions.Add("SUPPRESS_PER_MODULE_INLINE_FILE"); // This module does not use core's standard operator new/delete overloads
		}
	}
}
