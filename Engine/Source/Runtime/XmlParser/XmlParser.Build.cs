// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class XmlParser : ModuleRules
{
	public XmlParser( ReadOnlyTargetRules Target ) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] 
			{ 
				"Core",
			});
		UnsafeTypeCastWarningLevel = WarningLevel.Error;

		// PCH is disabled here otherwise the module will use Core's shared PCH and fail to compile due
		// to a clash with UE_DEFINE_LEGACY_MATH_CONSTANT_MACRO_NAMES being defined in UnrealMathUtility.h.
		PCHUsage = PCHUsageMode.NoPCHs;
		PrivateDefinitions.Add("UE_DEFINE_LEGACY_MATH_CONSTANT_MACRO_NAMES=0");
	}
}
