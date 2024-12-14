// Copyright Epic Games, Inc. All Rights Reserved.

using System.Linq;
using System.Net;
using System.IO; // for Path
using UnrealBuildTool;

public class uLangCore : ModuleRules
{
	protected virtual string uLangDefaultPlatformName { get => Target.Platform.ToString(); }

	public uLangCore(ReadOnlyTargetRules Target) : base(Target)
	{
		FPSemantics = FPSemanticsMode.Precise;
		PCHUsage = PCHUsageMode.NoPCHs;
		bRequiresImplementModule = false;
		BinariesSubFolder = "NotForLicensees";

		string uLangPlatformName = uLangDefaultPlatformName;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("ULANG_PLATFORM_WINDOWS=1");
			uLangPlatformName = "Windows";
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicDefinitions.Add("ULANG_PLATFORM_MAC=1");
			uLangPlatformName = "Mac";
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicDefinitions.Add("ULANG_PLATFORM_LINUX=1");
			uLangPlatformName = "Linux";
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.IOS))
		{
			if (Target.Architecture == UnrealArch.IOSSimulator || Target.Architecture == UnrealArch.TVOSSimulator)
			{
				PublicDefinitions.Add("ULANG_PLATFORM_IOS_WITH_SIMULATOR=1");
			}
			else
			{
				PublicDefinitions.Add("ULANG_PLATFORM_IOS_WITH_SIMULATOR=0");
			}
		}

		PublicDefinitions.Add("ULANG_PLATFORM=" + uLangPlatformName);

		PublicDefinitions.Add("ULANG_DO_CHECK=1");

		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			PublicDefinitions.Add("ULANG_BUILD_SHIPPING=1");
		}
		else
		{
			PublicDefinitions.Add("ULANG_BUILD_SHIPPING=0");
		}

		// Monolithic vs DLL
		if (Target.LinkType == TargetLinkType.Monolithic)
		{
			PublicDefinitions.Add("ULANG_IS_MONOLITHIC=1");
		}
		else
		{
			PublicDefinitions.Add("ULANG_IS_MONOLITHIC=0");
		}

		if (Target.bUseAutoRTFMCompiler)
		{
			// bAllowAutoRTFMInstrumentation needs a dependency on Core for the runtime.
			PrivateDependencyModuleNames.Add("Core");
		}

		// uLang is built and placed in Engine/Binaries/<Platform>/NotForLicensees. We want to add this to others that depend on us
		string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicRuntimeLibraryPaths.Add(Path.Combine(EngineDir, "Binaries", Target.Platform.ToString()));
		}
	}
}
