// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IoStoreHttpClient : ModuleRules
{
	public IoStoreHttpClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.Add("Core");
		PublicDependencyModuleNames.Add("TraceLog");
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"OpenSSL",
			}
		);

		UnsafeTypeCastWarningLevel = WarningLevel.Error; 
	}
}
