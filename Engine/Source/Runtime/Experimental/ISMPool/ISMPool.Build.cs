// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ISMPool : ModuleRules
	{
		public ISMPool(ReadOnlyTargetRules Target) : base(Target)
		{
			SetupModulePhysicsSupport(Target);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
				}
				);

			if (Target.bBuildEditor)
			{
				PublicDependencyModuleNames.Add("UnrealEd");
			}

			bDisableAutoRTFMInstrumentation = true;
		}
	}
}
