// Copyright Epic Games, Inc. All Rights Reserved.

#include "StudioTelemetry.h"
#include "StudioTelemetryLog.h"

#if WITH_EDITOR
#include "Horde.h"
#endif

#include "Analytics.h"
#include "AnalyticsProviderMulticast.h"
#include "AnalyticsTracer.h"
#include "BuildSettings.h"
#include "RHI.h"

#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Thread.h"

#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/EngineBuildSettings.h"

#include "GenericPlatform/GenericPlatformMisc.h"

DEFINE_LOG_CATEGORY(LogStudioTelemetry);

IMPLEMENT_MODULE(FStudioTelemetry, StudioTelemetry)

FStudioTelemetry& FStudioTelemetry::Get()
{
	static FStudioTelemetry StudioTelemetryInstance;
	return StudioTelemetryInstance;
}

void FStudioTelemetry::SetRecordEventCallback(OnRecordEventCallback Callback )
{
	RecordEventCallback = Callback;

	// If the provider already exists then set the callback
	if (AnalyticsProvider.IsValid())
	{	
		AnalyticsProvider->SetRecordEventCallback(RecordEventCallback);
	}
}

void FStudioTelemetry::StartupModule()
{
#if !UE_BUILD_SHIPPING
	UE_LOG(LogStudioTelemetry, Display, TEXT("Starting StudioTelemetry Module"));

	// Load the configuration
	FStudioTelemetry::Get().LoadConfiguration();

	// Create the provider and start the analytics session
	FStudioTelemetry::Get().StartSession();
#endif
}

void FStudioTelemetry::ShutdownModule()
{
#if !UE_BUILD_SHIPPING
	// End the session and destroy analytics provider
	FStudioTelemetry::Get().EndSession();

	UE_LOG(LogStudioTelemetry, Display, TEXT("Shutdown StudioTelemetry Module"));
#endif
}

void FStudioTelemetry::EndSession()
{
	OnEndSession.Broadcast();

	// End session for the tracer and the provider
	if (AnalyticsTracer.IsValid())
	{
		AnalyticsTracer->EndSession();
		AnalyticsTracer.Reset();
	}

	if (AnalyticsProvider.IsValid())
	{
		AnalyticsProvider->EndSession();
		AnalyticsProvider.Reset();

		UE_LOG(LogStudioTelemetry, Log, TEXT("Ended StudioTelemetry Session"));
	}
}

void FStudioTelemetry::LoadConfiguration()
{
	const FString TelemetryConfigurationSection(TEXT("StudioTelemetry.Config"));

	// Look for the configuration settings in the Engine.ini files
	TArray<FString> SectionNames;

	if (GConfig->GetSectionNames(GEngineIni, SectionNames))
	{
		for (const FString& SectionName : SectionNames)
		{
			if (SectionName.Find(TelemetryConfigurationSection) != INDEX_NONE)
			{
				GConfig->GetBool(*SectionName, TEXT("SendTelemetry="), Config.bSendTelemetry, GEngineIni);
				GConfig->GetBool(*SectionName, TEXT("SendUserData="), Config.bSendUserData, GEngineIni);
				GConfig->GetBool(*SectionName, TEXT("SendHardwareData="), Config.bSendHardwareData, GEngineIni);
				GConfig->GetBool(*SectionName, TEXT("SendOSData="), Config.bSendOSData, GEngineIni);
			}
		}
	}

	// Parse the commandline for any local configuration overrides
	FParse::Bool(FCommandLine::Get(), TEXT("ST_SendTelemetry="), Config.bSendTelemetry);
	FParse::Bool(FCommandLine::Get(), TEXT("ST_SendUserData="), Config.bSendUserData);
	FParse::Bool(FCommandLine::Get(), TEXT("ST_SendHardwareData="), Config.bSendHardwareData);
	FParse::Bool(FCommandLine::Get(), TEXT("ST_SendOSData="), Config.bSendOSData);
}

void FStudioTelemetry::StartSession()
{
	if (Config.bSendTelemetry == false)
	{
		// We did not wish to send any telemetry events
		return;
	}

	AnalyticsProvider = FAnalyticsProviderMulticast::CreateAnalyticsProvider();

	if (AnalyticsProvider.IsValid())
	{
		TArray<FAnalyticsEventAttribute> DefaultEventAttributes;

		const FString UserID = FPlatformProcess::UserName(false);
		const FString ProjectName = FApp::GetProjectName();
		FString ComputerName = FPlatformProcess::ComputerName();
		
		FString ProjectIDString;
		GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectID"), ProjectIDString, GGameIni);

		FGuid ProjectID;

		TArray<FString> Elements;
		if (ProjectIDString.ParseIntoArray(Elements, TEXT("=")) == 5) 
		{
			ProjectID = FGuid(FCString::Atoi(*(Elements[1])), FCString::Atoi(*(Elements[2])), FCString::Atoi(*(Elements[3])), FCString::Atoi(*(Elements[4])));
		}

		FGuid SessionID = FApp::GetInstanceId();
		
		FString SessionLabel;
		FParse::Value(FCommandLine::Get(), TEXT("SessionLabel="), SessionLabel);

		// Set the default event attributes, these will always be sent to telemetry for every event
		DefaultEventAttributes.Emplace(TEXT("ProjectName"), ProjectName);
		DefaultEventAttributes.Emplace(TEXT("ProjectID"), ProjectID);

		DefaultEventAttributes.Emplace(TEXT("Session_ID"), SessionID.ToString(EGuidFormats::DigitsWithHyphensInBraces));
		DefaultEventAttributes.Emplace(TEXT("Session_Label"), SessionLabel);
		DefaultEventAttributes.Emplace(TEXT("Session_StartUTC"), FDateTime::UtcNow().ToUnixTimestampDecimal());

		DefaultEventAttributes.Emplace(TEXT("Build_Configuration"), LexToString(FApp::GetBuildConfiguration()));
		DefaultEventAttributes.Emplace(TEXT("Build_IsInternalBuild"), FEngineBuildSettings::IsInternalBuild());
		DefaultEventAttributes.Emplace(TEXT("Build_IsPerforceBuild"), FEngineBuildSettings::IsPerforceBuild());
		DefaultEventAttributes.Emplace(TEXT("Build_IsPromotedBuild"), FApp::GetEngineIsPromotedBuild() == 0 ? false : true);
		DefaultEventAttributes.Emplace(TEXT("Build_BranchName"), FApp::GetBranchName().ToLower());
		DefaultEventAttributes.Emplace(TEXT("Build_Changelist"), BuildSettings::GetCurrentChangelist());

		DefaultEventAttributes.Emplace(TEXT("Config_IsEditor"), GIsEditor);
		DefaultEventAttributes.Emplace(TEXT("Config_IsUnattended"), FApp::IsUnattended());
		DefaultEventAttributes.Emplace(TEXT("Config_IsBuildMachine"), GIsBuildMachine);
		DefaultEventAttributes.Emplace(TEXT("Config_IsRunningCommandlet"), IsRunningCommandlet());
		DefaultEventAttributes.Emplace(TEXT("Config_IsDebuggerPresent"), FPlatformMisc::IsDebuggerPresent());

		// Only send user data if requested
		if (Config.bSendUserData == true)
		{
			DefaultEventAttributes.Emplace(TEXT("User_ID"), UserID);
			DefaultEventAttributes.Emplace(TEXT("Application_Commandline"), FCommandLine::Get());
		}

		// Only send hardware data if requested
		if (Config.bSendHardwareData == true)
		{
			DefaultEventAttributes.Emplace(TEXT("Hardware_Platform"), FString(FPlatformProperties::IniPlatformName()));
			DefaultEventAttributes.Emplace(TEXT("Hardware_GPU"), GRHIAdapterName);
			DefaultEventAttributes.Emplace(TEXT("Hardware_CPU"), FPlatformMisc::GetCPUBrand());
			DefaultEventAttributes.Emplace(TEXT("Hardware_CPU_Cores_Physical"), FPlatformMisc::NumberOfCores());
			DefaultEventAttributes.Emplace(TEXT("Hardware_CPU_Cores_Logical"), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
			DefaultEventAttributes.Emplace(TEXT("Hardware_RAM"), static_cast<uint64>(FPlatformMemory::GetStats().TotalPhysical));
			DefaultEventAttributes.Emplace(TEXT("Hardware_ComputerName"), ComputerName);
		}

		// Only send OS data if requested
		if (Config.bSendOSData==true)
		{
			FString OSVersionLabel;
			FString OSSubVersionLabel;

			FPlatformMisc::GetOSVersions(OSVersionLabel, OSSubVersionLabel);

			DefaultEventAttributes.Emplace(TEXT("OS_Version"), FPlatformMisc::GetOSVersion());
			DefaultEventAttributes.Emplace(TEXT("OS_VersionLabel"), OSVersionLabel);
			DefaultEventAttributes.Emplace(TEXT("OS_VersionSubLabel"), OSSubVersionLabel);
			DefaultEventAttributes.Emplace(TEXT("OS_ID"), FPlatformMisc::GetOperatingSystemId());	
		}
		
#if WITH_EDITOR
		if (!FHorde::GetJobId().IsEmpty())
		{
			// Only send Horde data if applicable
			DefaultEventAttributes.Emplace(TEXT("Horde_ServerURL"), FHorde::GetServerURL());
			DefaultEventAttributes.Emplace(TEXT("Horde_TemplateID"), FHorde::GetTemplateId());
			DefaultEventAttributes.Emplace(TEXT("Horde_TemplateName"), FHorde::GetTemplateName());
			DefaultEventAttributes.Emplace(TEXT("Horde_JobURL"), FHorde::GetJobURL());
			DefaultEventAttributes.Emplace(TEXT("Horde_JobID"), FHorde::GetJobId());
			DefaultEventAttributes.Emplace(TEXT("Horde_StepName"), FHorde::GetStepName());
			DefaultEventAttributes.Emplace(TEXT("Horde_StepID"), FHorde::GetStepId());
			DefaultEventAttributes.Emplace(TEXT("Horde_StepURL"), FHorde::GetStepURL());
			DefaultEventAttributes.Emplace(TEXT("Horde_BatchID"), FHorde::GetBatchId());
		}
#endif
		
		// Set up the analytics provider
		AnalyticsProvider->SetUserID(UserID);
		AnalyticsProvider->SetSessionID(SessionID.ToString(EGuidFormats::DigitsWithHyphensInBraces));
		AnalyticsProvider->SetDefaultEventAttributes(MoveTemp(DefaultEventAttributes));
		AnalyticsProvider->SetRecordEventCallback(RecordEventCallback);
		
		// Start the analytics session
		AnalyticsProvider->StartSession();

		// Create the IAnalyticsTracer interface
		AnalyticsTracer = FAnalytics::Get().CreateAnalyticsTracer();
		AnalyticsTracer->SetProvider(AnalyticsProvider);
		AnalyticsTracer->StartSession();

		// Bind the pre-exit callback
		FCoreDelegates::OnEnginePreExit.AddRaw(&FStudioTelemetry::Get(), &FStudioTelemetry::EndSession);

		OnStartSession.Broadcast();

		UE_LOG(LogStudioTelemetry, Log, TEXT("Started StudioTelemetry Session"));
	}
}

void FStudioTelemetry::RecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (AnalyticsProvider.IsValid())
	{
		FScopeLock ScopeLock(&CriticalSection);
		AnalyticsProvider->RecordEvent(EventName, Attributes);
	}

	OnRecordEvent.Broadcast(EventName, Attributes);
}

void FStudioTelemetry::RecordEvent(const FName CategoryName, const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (AnalyticsProvider.IsValid())
	{
		FScopeLock ScopeLock(&CriticalSection);
		AnalyticsProvider->RecordEvent(EventName, Attributes);
	}

	OnRecordEvent.Broadcast(EventName, Attributes);
}

void FStudioTelemetry::RecordEventToProvider(const FString& ProviderName, const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	FScopeLock ScopeLock(&CriticalSection);
	TSharedPtr<IAnalyticsProvider> NamedProvider = GetProvider(ProviderName).Pin();

	if (NamedProvider.IsValid())
	{
		NamedProvider->RecordEvent(EventName, Attributes);
	}
}

TWeakPtr<IAnalyticsProvider> FStudioTelemetry::GetProvider()
{
	return AnalyticsProvider;
}

TWeakPtr<IAnalyticsProvider> FStudioTelemetry::GetProvider(const FString& Name)
{
	return AnalyticsProvider.IsValid()? AnalyticsProvider->GetAnalyticsProvider(Name) : TWeakPtr<IAnalyticsProvider>();
}

TWeakPtr<IAnalyticsTracer> FStudioTelemetry::GetTracer()
{
	return AnalyticsTracer;
}

TSharedPtr<IAnalyticsSpan> FStudioTelemetry::StartSpan(const FName Name, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	return AnalyticsTracer.IsValid() ? AnalyticsTracer->StartSpan(Name, AdditionalAttributes) : TSharedPtr<IAnalyticsSpan>();
}

TSharedPtr<IAnalyticsSpan> FStudioTelemetry::StartSpan(const FName Name, TSharedPtr<IAnalyticsSpan> ParentSpan, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	return AnalyticsTracer.IsValid() ? AnalyticsTracer->StartSpan(Name, ParentSpan, AdditionalAttributes)  : TSharedPtr<IAnalyticsSpan>();
}

bool FStudioTelemetry::EndSpan(TSharedPtr<IAnalyticsSpan> Span, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	return AnalyticsTracer.IsValid() ? AnalyticsTracer->EndSpan(Span, AdditionalAttributes) : false;
}

bool FStudioTelemetry::EndSpan(const FName Name, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes)
{
	return AnalyticsTracer.IsValid() ? AnalyticsTracer->EndSpan(Name, AdditionalAttributes) : false;
}

TSharedPtr<IAnalyticsSpan> FStudioTelemetry::GetSpan(const FName Name)
{
	return AnalyticsTracer.IsValid() ? AnalyticsTracer->GetSpan(Name) : TSharedPtr<IAnalyticsSpan>();
}

TSharedPtr<IAnalyticsSpan> FStudioTelemetry::GetSessionSpan() const
{
	return AnalyticsTracer.IsValid() ? AnalyticsTracer->GetSessionSpan() : TSharedPtr<IAnalyticsSpan>();
}


