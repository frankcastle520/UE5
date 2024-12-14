// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;

namespace UnrealBuildTool
{
	class VisionOSToolChainSettings : IOSToolChainSettings
	{
		public VisionOSToolChainSettings(ILogger Logger) : base("XROS", "XRSimulator", "xros", Logger)
		{
		}
	}

	class VisionOSToolChain : IOSToolChain
	{
		private VisionOSToolChainSettings Settings => (VisionOSToolChainSettings)ToolChainSettings.Value;

		public VisionOSToolChain(ReadOnlyTargetRules InTarget, VisionOSProjectSettings InProjectSettings, ILogger InLogger)
			: base(InTarget, InProjectSettings, () => new VisionOSToolChainSettings(InLogger), ClangToolChainOptions.None, InLogger)
		{
		}

		public override string GetXcodeMinVersionParam(UnrealArch Architecture)
		{
			return "";
		}

		public float GetSDKVersionFloat()
		{
			return Settings.SDKVersionFloat;
		}
	}
}
