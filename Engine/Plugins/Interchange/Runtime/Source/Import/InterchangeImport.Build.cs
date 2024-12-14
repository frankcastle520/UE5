// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InterchangeImport : ModuleRules
	{
		public InterchangeImport(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"ClothingSystemRuntimeCommon",
					"Core",
					"CoreUObject",
					"Engine",
					"InterchangeCore",
					"InterchangeCommon",
					"InterchangeDispatcher",
					"InterchangeEngine",
					"InterchangeFactoryNodes",
					"InterchangeNodes",
					"LevelSequence",
					"MeshDescription",
					"MovieScene",
					"MovieSceneTracks",
					"StaticMeshDescription",
					"SkeletalMeshDescription",
				}
			);
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetRegistry",
					"CinematicCamera",
					"ClothingSystemRuntimeCommon",
					"GLTFCore",
					"IESFile",
					"ImageCore",
					"ImageWrapper",
					"InterchangeCommonParser",
					"InterchangeMessages",
					"Json",
					"RenderCore",
					"RHI",
					"TextureUtilitiesCommon",
					"UnrealUSDWrapper",
					"USDClasses",
					"USDUtilities",
					"VariantManagerContent",
				}
			);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"BSPUtils",
						"InterchangeFbxParser",
						"MaterialEditor",
						"SkeletalMeshUtilitiesCommon",
						"UnrealEd",
						"VariantManager",
					}
				);

				if (Target.Platform == UnrealTargetPlatform.Win64 || Target.LinkType == TargetLinkType.Monolithic)
				{
					PrivateDependencyModuleNames.AddRange(
						new string[]
						{
							"MaterialX"
						}
					);
				}
			}
		}
	}
}
