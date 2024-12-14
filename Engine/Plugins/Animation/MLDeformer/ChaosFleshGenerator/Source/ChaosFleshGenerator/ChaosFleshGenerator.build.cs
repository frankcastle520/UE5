// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosFleshGenerator : ModuleRules
{
	public ChaosFleshGenerator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"ChaosCore",
				"Chaos",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"ChaosFlesh",
				"ChaosFleshEngine",
				"ChaosFleshNodes",
				"DataflowCore",
				"DataflowEditor",
				"DataflowEngine",
				"DataflowSimulation",
				"Engine",
				"GeometryCache",
				"MLDeformerFramework",
				"MLDeformerFrameworkEditor",
				"PropertyEditor",
				"RenderCore",
				"Slate",
				"SlateCore",
				"UnrealEd"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
