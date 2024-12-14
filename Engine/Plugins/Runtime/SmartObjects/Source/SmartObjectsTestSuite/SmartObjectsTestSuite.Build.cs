// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SmartObjectsTestSuite : ModuleRules
	{
		public SmartObjectsTestSuite(ReadOnlyTargetRules Target) : base(Target)
		{
			UnsafeTypeCastWarningLevel = WarningLevel.Warning;

			PublicIncludePaths.AddRange(
				new string[] {
				}
			);

			PublicDependencyModuleNames.AddRange(
				new[] {
					"Core",
					"CoreUObject",
					"Engine",
					"SmartObjectsModule",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new[] {
					"AITestSuite",
					"GameplayTags",
					"WorldConditions",
				}
			);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("EditorFramework");
				PrivateDependencyModuleNames.Add("UnrealEd");
			}
		}
	}
}