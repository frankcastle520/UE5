// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCollectionDepNodes : ModuleRules
	{
        public GeometryCollectionDepNodes(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"ChaosCore",
					"Chaos",
					"DataflowCore",
					"DataflowEngine",
					"DataflowEnginePlugin",
					"GeometryCollectionEngine",
					"GeometryCollectionNodes",
					"Engine"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] 
				{
				}
			);
		}
	}
}
