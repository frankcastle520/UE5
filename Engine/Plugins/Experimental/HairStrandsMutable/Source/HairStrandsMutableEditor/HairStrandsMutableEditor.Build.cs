// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class HairStrandsMutableEditor : ModuleRules
	{
        public HairStrandsMutableEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add(ModuleDirectory + "/Private");
			PublicIncludePaths.Add(ModuleDirectory + "/Public");
			
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"CustomizableObjectEditor",
					"MutableTools",
					"MutableRuntime"
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"HairStrandsMutable",
					"HairStrandsCore",
					"StructUtils"
				});
		}
	}
}
