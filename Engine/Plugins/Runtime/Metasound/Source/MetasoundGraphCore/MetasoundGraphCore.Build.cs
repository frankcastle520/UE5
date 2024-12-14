// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MetasoundGraphCore : ModuleRules
	{
        public MetasoundGraphCore(ReadOnlyTargetRules Target) : base(Target)
		{
            PublicDependencyModuleNames.AddRange(
				new string[] {
                    "Core",
					"SignalProcessing",
					"AudioExtensions"
				}
            );

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"MathCore",
					// ... add private dependencies that you statically link with here ...	
				}
			);
		}
	}
}
