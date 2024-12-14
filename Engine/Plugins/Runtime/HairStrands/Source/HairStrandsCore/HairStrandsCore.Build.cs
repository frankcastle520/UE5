// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class HairStrandsCore : ModuleRules
	{
		public HairStrandsCore(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"GeometryCache",
					"Projects",
					"MeshDescription",
					"MovieScene",
					"NiagaraCore",
					"NiagaraShader",
					"RenderCore",
					"Renderer",
					"VectorVM",
					"RHI",
					"StaticMeshDescription"
				});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Niagara"
				});

			PrivateIncludePathModuleNames.AddRange(
				new string[]
				{
					"Shaders",
				});

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"Eigen",
						"DerivedDataCache",
					});

				PrivateIncludePathModuleNames.AddRange(
					new string[]
					{
						"DerivedDataCache",
					});
			}
		}
	}
}
