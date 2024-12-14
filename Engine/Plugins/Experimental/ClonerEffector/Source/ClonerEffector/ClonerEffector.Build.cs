// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ClonerEffector : ModuleRules
{
	public ClonerEffector(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"ClonerEffectorMeshBuilder",
				"Core",
				"CoreUObject",
				"Engine",
				"Niagara"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DeveloperSettings",
				"GeometryCore",
				"GeometryFramework",
				"GeometryScriptingCore",
				"NiagaraCore"
			}
		);

		if (Target.Type == TargetRules.TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"Slate",
				"SlateCore"
			});
		}

		ShortName = "CE";
	}
}
