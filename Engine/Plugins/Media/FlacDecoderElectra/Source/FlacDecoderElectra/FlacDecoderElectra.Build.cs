// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class FlacDecoderElectra : ModuleRules
	{
		public FlacDecoderElectra(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"ElectraCodecFactory",
					"ElectraDecoders",
					"libFLAC"
                });
		}
	}
}
