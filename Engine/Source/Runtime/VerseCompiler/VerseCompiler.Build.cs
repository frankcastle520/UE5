// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO; // for Path

public class VerseCompiler : ModuleRules
{
	public VerseCompiler(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bRequiresImplementModule = false;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"uLangCore",
				"uLangJSON"
			}
		);

		// See no reference to this elsewhere. Remove after green-tests.
		//
		//string ModuleName = this.GetType().Name.ToUpper();
		//if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		//{
		//	PublicDefinitions.Add(ModuleName + "_TESTAPI=" + ModuleName + "_API");
		//}
		//else
		//{
		//	PublicDefinitions.Add(ModuleName + "_TESTAPI=");
		//}

		bDisableAutoRTFMInstrumentation = true;
	}
}
