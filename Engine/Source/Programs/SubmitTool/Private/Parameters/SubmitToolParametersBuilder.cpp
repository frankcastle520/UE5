// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmitToolParametersBuilder.h"

#include "SubmitToolUtils.h"
#include "Configuration/Configuration.h"

#include "CoreGlobals.h"
#include "Models/ModelInterface.h"
#include "HAL/FileManager.h"
#include "Logging/SubmitToolLog.h"
#include "CommandLine/CmdLineParameters.h"
#include "Misc/ConfigContext.h"

FSubmitToolParametersBuilder::FSubmitToolParametersBuilder(const FString& InParametersXmlFile)
{
	ConfigHierarchy.Add({ TEXT("Base"), TEXT("{ENGINE}/Config/Base.ini") });
	ConfigHierarchy.Add( { TEXT("SubmitToolBase"), TEXT("{PROJECT}/Config/{TYPE}.ini") } );
	ConfigHierarchy.Add( { TEXT("Platform"), TEXT("{PROJECT}/Config/{PLATFORM}/{PLATFORM}{TYPE}.ini") } );

	FString RootDir;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::RootDir, RootDir);
	FPaths::NormalizeDirectoryName(RootDir);

	// allocate a string since the FConfigLayer expects static TCHAR*, so we can't let the string go away
	FString* RootBase = new FString(FPaths::Combine(RootDir, TEXT("/Config/{TYPE}.ini")));
	ConfigHierarchy.Add({ TEXT("RootBase"), **RootBase, EConfigLayerFlags::NoExpand });

	FString* RootPlatform = new FString(FPaths::Combine(RootDir, TEXT("/Config/{PLATFORM}/{PLATFORM}{TYPE}.ini")));
	ConfigHierarchy.Add({ TEXT("RootPlatform"), **RootPlatform, EConfigLayerFlags::NoExpand });

	if(!RootDir.IsEmpty())
	{
		IFileManager::Get().IterateDirectory(*FConfiguration::Substitute(TEXT("$(root)")),
			[this](const TCHAR* FileOrDir, bool bIsDir) -> bool
			{
				if(bIsDir)
				{
					FString Dir = { FileOrDir };
					if(Dir != TEXT("SubmitTool") && !Dir.Contains(TEXT("Engine"), ESearchCase::IgnoreCase))
					{
						const FString Extension = TEXT(".uproject");
						TArray<FString> UProjects;
						IFileManager::Get().FindFiles(UProjects, FileOrDir, *Extension);

						if(UProjects.Num() != 0)
						{
							// allocate a string since the FConfigLayer expects static TCHAR*, so we can't let the string go away
							FString* ProjectIni = new FString(FPaths::Combine(Dir, TEXT("/Config/{TYPE}.ini")));
							ConfigHierarchy.Add( { TEXT("Project"), **ProjectIni, EConfigLayerFlags::NoExpand } );

							FString* ProjectPlatform = new FString(FPaths::Combine(Dir, TEXT("/Config/{PLATFORM}/{PLATFORM}{TYPE}.ini")));
							ConfigHierarchy.Add({ TEXT("ProjectPlatform"), **ProjectPlatform, EConfigLayerFlags::NoExpand });

							// Let's hold on to this info for the preflight parameters
							for (const FString& UProjectName : UProjects)
							{
								ProjectNames.Add(UProjectName.LeftChop(Extension.Len()));
							}
						}
					}
				}

				return true;
			});
	}
		
	// allocate a string since the FConfigLayer expects static TCHAR*, so we can't let the string go away
	FString* UserIni = new FString(FPaths::Combine(FSubmitToolUtils::GetLocalAppDataPath(), "SubmitTool", "SubmitTool.ini"));
	ConfigHierarchy.Add( { TEXT("User"), **UserIni, EConfigLayerFlags::NoExpand } );
}

FSubmitToolParameters FSubmitToolParametersBuilder::Build()
{
	FConfigContext Context = FConfigContext::ReadIntoGConfig();
	Context.OverrideLayers = ConfigHierarchy;

	FString IniFilename;
	Context.Load(TEXT("SubmitTool"), IniFilename);
	SubmitToolConfig = GConfig->FindConfigFile(IniFilename);

	UE_LOG(LogSubmitTool, Verbose, TEXT("Loading config from the following files:"));
	for(const TPair<int32, FString>& OverrideLayer : Context.Branch->Hierarchy)
	{
		if(IFileManager::Get().FileExists(*OverrideLayer.Value))
		{
			UE_LOG(LogSubmitTool, Verbose, TEXT("%s"), *FPaths::ConvertRelativePathToFull(OverrideLayer.Value));
		}
	}

	FSubmitToolParameters Parameters;
	Parameters.GeneralParameters = BuildGeneralParameters();
	Parameters.JiraParameters = BuildJiraParameters();
	Parameters.Telemetry = GetTelemetryParameters();
	Parameters.IntegrationParameters = BuildIntegrationParameters();
	Parameters.AvailableTags = BuildAvailableTags();
	Parameters.Validators = BuildValidators();
	Parameters.PresubmitOperations = BuildPresubmitOperations();
	Parameters.CopyLogParameters = BuildCopyLogParameters();
	Parameters.P4LockdownParameters = BuildP4LockdownParameters();
	Parameters.OAuthParameters = BuildOAuthParameters();
	Parameters.IncompatibleFilesParams = BuildIncompatibleFilesParameters();
	Parameters.HordeParameters = BuildHordeParameters();
	Parameters.AutoUpdateParameters = BuildAutoUpdateParameters();
	return Parameters;
}

FGeneralParameters FSubmitToolParametersBuilder::BuildGeneralParameters()
{
	const FConfigSection* Section = SubmitToolConfig->FindSection(TEXT("SubmitTool.General"));
	FGeneralParameters Output;

	if(Section != nullptr)
	{
		FStringOutputDevice Errors;
		FGeneralParameters::StaticStruct()->ImportText(*SectionToText(*Section), &Output, nullptr, 0, &Errors, FGeneralParameters::StaticStruct()->GetName());

		if(!Errors.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			FModelInterface::SetErrorState();
		}
		else
		{
			Output.CacheFile = FConfiguration::SubstituteAndNormalizeFilename(Output.CacheFile);
		}
	}

	return Output;
}

FJiraParameters FSubmitToolParametersBuilder::BuildJiraParameters()
{
	const FConfigSection* Section = SubmitToolConfig->FindSection(TEXT("SubmitTool.Jira"));
	FJiraParameters Output;

	if(Section != nullptr)
	{
		FStringOutputDevice Errors;
		FJiraParameters::StaticStruct()->ImportText(*SectionToText(*Section), &Output, nullptr, 0, &Errors, FJiraParameters::StaticStruct()->GetName());

		if(!Errors.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			FModelInterface::SetErrorState();
		}
	}

	return Output;
}

 FTelemetryParameters FSubmitToolParametersBuilder::GetTelemetryParameters()
{
	static const TCHAR* Section = TEXT("SubmitTool.Telemetry");
	static const TCHAR* UrlKey = TEXT("URL");
	static const TCHAR* InstanceKey = TEXT("Instance");

	FTelemetryParameters Output;

	FString Url;
	if (SubmitToolConfig->GetString(Section, UrlKey, Url))
	{
		Output.Url = Url;
	}

	FString Instance;
	if (SubmitToolConfig->GetString(Section, InstanceKey, Instance))
	{
		Output.Instance = Instance;
	}

	return Output;
}

 FIntegrationParameters FSubmitToolParametersBuilder::BuildIntegrationParameters()
 {
	 const FConfigSection* Section = SubmitToolConfig->FindSection(TEXT("SubmitTool.FNIntegration"));
	 FIntegrationParameters Output;

	 if(Section != nullptr)
	 {
		 FStringOutputDevice Errors;
		 FIntegrationParameters::StaticStruct()->ImportText(*SectionToText(*Section), &Output, nullptr, 0, &Errors, FIntegrationParameters::StaticStruct()->GetName());

		 if(!Errors.IsEmpty())
		 {
			 UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			 FModelInterface::SetErrorState();
		 }
	 }

	 return Output;
 }

TArray<FTagDefinition> FSubmitToolParametersBuilder::BuildAvailableTags()
{
	static const TCHAR* TagsSectionName = TEXT("Tags.");
	TArray<FTagDefinition> Output;

	for(const TPair<FString, FConfigSection>& Section : AsConst(*SubmitToolConfig))
	{
		if(Section.Key.StartsWith(TagsSectionName))
		{
			FTagDefinition Definition;
			FStringOutputDevice Errors;
			FTagDefinition::StaticStruct()->ImportText(*SectionToText(Section.Value), &Definition, nullptr, 0, &Errors, FValidatorDefinition::StaticStruct()->GetName());

			if (Definition.bIsDisabled)
			{
				UE_LOG(LogSubmitToolDebug, Verbose, TEXT("Skipped tag due to it being disabled %s"), *Definition.TagId);
				continue;
			}

			if(!Definition.DocumentationUrl.IsEmpty())
			{
				Definition.ToolTip += TEXT("\nClick the icon for more information.");
			}

			if(!Errors.IsEmpty())
			{
				UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
				FModelInterface::SetErrorState();
			}
			else
			{
				UE_LOG(LogSubmitToolDebug, Verbose, TEXT("Added Tag %s"), *Definition.TagId);
				Output.Add(Definition);
			}
		}
	}

	Output.Sort([](const FTagDefinition& A, const FTagDefinition& B)
		{
			return A.OrdinalOverride <= B.OrdinalOverride;
		});

	return Output;
}

TMap<FString, FString> FSubmitToolParametersBuilder::BuildValidators()
{
	static const TCHAR* ValidatorsSectionName = TEXT("Validator.");

	TMap<FString, FString> Output;

	for(const TPair<FString, FConfigSection>& Section : AsConst(*SubmitToolConfig))
	{
		if(Section.Key.StartsWith(ValidatorsSectionName))
		{
			Output.Add(Section.Key.Replace(ValidatorsSectionName, TEXT("")), SectionToText(Section.Value));
		}
	}

	return Output;
}

TMap<FString, FString> FSubmitToolParametersBuilder::BuildPresubmitOperations()
{
	static const TCHAR* PresubmitOperationsSectionName = TEXT("PresubmitOperation.");

	TMap<FString, FString> Output;

	for(const TPair<FString, FConfigSection>& Section : AsConst(*SubmitToolConfig))
	{
		if(Section.Key.StartsWith(PresubmitOperationsSectionName))
		{
			Output.Add(Section.Key.Replace(PresubmitOperationsSectionName, TEXT("")), SectionToText(Section.Value));
		}
	}

	return Output;
}

FCopyLogParameters FSubmitToolParametersBuilder::BuildCopyLogParameters()
{
	const FConfigSection* Section = SubmitToolConfig->FindSection(TEXT("SubmitTool.CopyLog"));
	FCopyLogParameters Output;
	
	if(Section != nullptr)
	{
		FStringOutputDevice Errors;
		FCopyLogParameters::StaticStruct()->ImportText(*SectionToText(*Section), &Output, nullptr, 0, &Errors, FCopyLogParameters::StaticStruct()->GetName());

		if(!Errors.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			FModelInterface::SetErrorState();
		}
	}

	return Output;
}
FP4LockdownParameters FSubmitToolParametersBuilder::BuildP4LockdownParameters()
{
	const FConfigSection* Section = SubmitToolConfig->FindSection(TEXT("SubmitTool.P4Lockdown"));
	FP4LockdownParameters Output;

	if(Section != nullptr)
	{
		FStringOutputDevice Errors;
		FP4LockdownParameters::StaticStruct()->ImportText(*SectionToText(*Section), &Output, nullptr, 0, &Errors, FP4LockdownParameters::StaticStruct()->GetName());

		if(!Errors.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			FModelInterface::SetErrorState();
		}
	}

	return Output;
}

FOAuthTokenParams FSubmitToolParametersBuilder::BuildOAuthParameters()
{
	const FConfigSection* Section = SubmitToolConfig->FindSection(TEXT("SubmitTool.OAuthToken"));
	FOAuthTokenParams Output;

	if(Section != nullptr)
	{
		FStringOutputDevice Errors;
		FOAuthTokenParams::StaticStruct()->ImportText(*SectionToText(*Section), &Output, nullptr, 0, &Errors, FOAuthTokenParams::StaticStruct()->GetName());

		if(!Errors.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			FModelInterface::SetErrorState();
		}
		else
		{
			Output.OAuthFile = FConfiguration::Substitute(Output.OAuthFile);
			Output.OAuthTokenTool = FConfiguration::Substitute(Output.OAuthTokenTool);
			Output.OAuthArgs = FConfiguration::Substitute(Output.OAuthArgs); 
		}
	}

	return Output;
}


FIncompatibleFilesParams FSubmitToolParametersBuilder::BuildIncompatibleFilesParameters()
{
	const FConfigSection* Section = SubmitToolConfig->FindSection(TEXT("SubmitTool.IncompatibleFiles"));
	FIncompatibleFilesParams Output;

	if(Section != nullptr)
	{
		FStringOutputDevice Errors;
		FIncompatibleFilesParams::StaticStruct()->ImportText(*SectionToText(*Section), &Output, nullptr, 0, &Errors, FIncompatibleFilesParams::StaticStruct()->GetName());

		if(!Errors.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			FModelInterface::SetErrorState();
		}
	}

	return Output;
}

FHordeParameters FSubmitToolParametersBuilder::BuildHordeParameters()
{
	const FConfigSection* Section = SubmitToolConfig->FindSection(TEXT("SubmitTool.Horde"));
	FHordeParameters Output;

	if(Section != nullptr)
	{
		FStringOutputDevice Errors;
		FHordeParameters::StaticStruct()->ImportText(*SectionToText(*Section), &Output, nullptr, 0, &Errors, FHordeParameters::StaticStruct()->GetName());

		if (!Errors.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			FModelInterface::SetErrorState();
		}
	}
	return Output;
}

FAutoUpdateParameters FSubmitToolParametersBuilder::BuildAutoUpdateParameters()
{
	const FConfigSection* Section = SubmitToolConfig->FindSection(TEXT("SubmitTool.AutoUpdate"));
	FAutoUpdateParameters Output;

	if(Section != nullptr)
	{
		FStringOutputDevice Errors;
		FAutoUpdateParameters::StaticStruct()->ImportText(*SectionToText(*Section), &Output, nullptr, 0, &Errors, FAutoUpdateParameters::StaticStruct()->GetName());

		if (!Errors.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			FModelInterface::SetErrorState();
		}
		else
		{
			Output.DeployIdFilePath = FConfiguration::Substitute(Output.DeployIdFilePath);
			Output.LocalDownloadZip = FConfiguration::Substitute(Output.LocalDownloadZip);
			Output.LocalVersionFile = FConfiguration::Substitute(Output.LocalVersionFile);
			Output.AutoUpdateScript = FConfiguration::Substitute(Output.AutoUpdateScript);
			Output.LocalAutoUpdateScript = FConfiguration::Substitute(Output.LocalAutoUpdateScript);
		}

	}

	return Output;
}

FString FSubmitToolParametersBuilder::SectionToText(const FConfigSection& InSection) const
{
	TArray<FString> lines;
	for(const TPair<FName, FConfigValue>& Item : InSection.Array())
	{
		FString Value = Item.Value.GetValue();

		// If it's an array/map/struct, we only need to quote the key, otherwise quote key and value
		if((Value.IsNumeric() && !Value.Equals(TEXT("-"))) || (Value.StartsWith(TEXT("(")) && Value.EndsWith(TEXT(")")) && !Item.Key.ToString().Contains(TEXT("Regex"), ESearchCase::IgnoreCase)))
		{
			lines.Add(FString::Printf(TEXT("\"%s\"=%s"), *Item.Key.ToString(), *Item.Value.GetValue()));
		}
		else
		{
			lines.Add(FString::Printf(TEXT("\"%s\"=\"%s\""), *Item.Key.ToString(), *Item.Value.GetValue()));
		}
	}

	FString FinalText = TEXT("(") + FString::Join(lines, TEXT(",")) + TEXT(")");
	return FinalText;
}

