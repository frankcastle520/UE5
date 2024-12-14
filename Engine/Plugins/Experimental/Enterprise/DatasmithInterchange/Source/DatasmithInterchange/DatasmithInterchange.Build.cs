// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{	public class DatasmithInterchange : ModuleRules
	{
		public DatasmithInterchange(ReadOnlyTargetRules Target)
			: base(Target)
		{

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CADTools",
					"DatasmithContent",
					"DatasmithCore",
					"DatasmithTranslator",
					"Engine",
					"ExternalSource",
					"InterchangeEngine",
					"InterchangeNodes",
					"InterchangeCommonParser",
					"InterchangeFactoryNodes",
					"InterchangeImport",
					"InterchangePipelines",
					"MeshDescription",
					"ParametricSurface",
					"StaticMeshDescription"
				}
			);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"DatasmithImporter",
						"DesktopPlatform",
						"InputCore",
						"InterchangeEditorPipelines",
						"PropertyEditor",
						"Slate",
						"SlateCore",
						"ToolMenus",
						"UnrealEd",
					}
				);
			}

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"InterchangeCore",
					"InterchangeImport",
				}
			);
		}
	}
}