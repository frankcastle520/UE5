// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using EpicGames.Core;

public class GoogleGameSDK : ModuleRules
{
	public GoogleGameSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		string GoogleGameSDKBasePath = Target.UEThirdPartySourceDirectory + "GoogleGameSDK/gamesdk";

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			string GoogleGameSDKLibPath = GoogleGameSDKBasePath + "/libs";
			if (Target.Configuration == UnrealTargetConfiguration.Debug)
			{
				GoogleGameSDKLibPath = GoogleGameSDKLibPath + "/debug";
			}
			else
			{
				GoogleGameSDKLibPath = GoogleGameSDKLibPath + "/release";
			}
			string Arm64GameSDKPath = GoogleGameSDKLibPath + "/arm64-v8a/";
			string x86_64GameSDKPath = GoogleGameSDKLibPath + "/x86_64/";

			string StaticLibName = "libswappy_static.a";

			bool UseStaticLib = false;
			if (UseStaticLib)
			{
				PublicAdditionalLibraries.Add(Arm64GameSDKPath + StaticLibName);
				PublicAdditionalLibraries.Add(x86_64GameSDKPath + StaticLibName);
			}

			PublicSystemIncludePaths.Add(GoogleGameSDKBasePath + "/include");
		}
	}
}
