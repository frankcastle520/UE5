// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System;
using System.IO;


public class TextureGraphEngine : ModuleRules
{
    private void AddDefaultIncludePaths()
    {

        // Add all the public directories
        PublicIncludePaths.Add(ModuleDirectory);

        //Add Public dir to 
        string PublicDirectory = Path.Combine(ModuleDirectory, "Public");
        if (Directory.Exists(PublicDirectory))
        {
            PublicIncludePaths.Add(PublicDirectory);
        }

        // Add the base private directory for this module
        string PrivateDirectory = Path.Combine(ModuleDirectory, "Private");
        if (Directory.Exists(PrivateDirectory))
        {
            PrivateIncludePaths.Add(PrivateDirectory);
        }
	}


    public TextureGraphEngine(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseRTTI = true;
        bEnableExceptions = true;

		// Disable Clang analysis due to a crash in clang-cl
		if (Target.StaticAnalyzer != StaticAnalyzer.None
			&& Target.Platform.IsInGroup(UnrealPlatformGroup.Microsoft)
			&& Target.WindowsPlatform.Compiler.IsClang())
		{
			// https://developercommunity.visualstudio.com/t/clang-cl---analyze-crash-with-thread-i/10746623
			bDisableStaticAnalysis = true;
		}

		//PublicDefinitions.Add("WITH_MALLOC_STOMP=1");

		PrivateDependencyModuleNames.AddRange(new string[] 
		{ 
			"Slate", 
			"SlateCore",
			"UMG", 
			"MutableRuntime"
		});

        PublicDependencyModuleNames.AddRange(new string[]  
		{ 
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore",
			"XmlParser" ,
            "ProceduralMeshComponent",
			"Renderer",
			"RenderCore",
            "RHI",
            "HTTP",
            "Json",
            "JsonUtilities",
			"ImageCore",
			"ImageWrapper",
            "Projects",
            "ImageWriteQueue",
			"LibJpegTurbo",
            "Function2",
            "Continuable",
            "Sockets",
            "Networking", 
            "DeveloperSettings",
			"RenderDocPlugin",
		});

		if (Target.bBuildEditor == true)
		{
			//reference the module "MyModule"
			PublicDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",
                "AssetTools"
			});
		}

		AddDefaultIncludePaths();

		if (!ModuleDirectory.StartsWith(EngineDirectory))
			throw new BuildException("TextureGraphEngine module directory must be under engine");

		string ModuleRelativeToEngineDir = ModuleDirectory.Substring(EngineDirectory.Length + 1);

		string ModDirLiteral = ModuleRelativeToEngineDir.Replace('\\', '/');
		string defModuleName = "MODULE_DIR \"" + ModDirLiteral + "\"=";
		PrivateDefinitions.Add(defModuleName);
	}

}
