// Copyright Epic Games, Inc. All Rights Reserved.

#include "StandAloneTelemetry.h"
#include "Misc/Build.h"
#include "Logging/SubmitToolLog.h"
#include "Version/AppVersion.h"
#include "AnalyticsEventAttribute.h"

FStandAloneTelemetry::FStandAloneTelemetry(const FString& InUrl, const FGuid& InSessionID)
{
	FAnalyticsET::Config Config;

	// this is a huge bucket where we all !shipping builds are considered local dev builds
#if UE_BUILD_SHIPPING || UE_BUILD_DEVELOPMENT
	Config.APIKeyET = TEXT("SubmitToolStandalone.Live");
#else
	Config.APIKeyET = TEXT("SubmitToolStandalone.Debug");
#endif

	Config.APIServerET = InUrl;

	// This will become the AppVersion URL parameter. It can be whatever makes sense for your app.
	Config.AppVersionET = FAppVersion::GetVersion();

	// This will become the Environment URL parameter. It can be arbitrary.
	Config.AppEnvironment = TEXT("SubmitTool.Standalone");

	// There are other things to configure, but the default are usually fine.
	
	this->Provider = FAnalyticsET::Get().CreateAnalyticsProvider(Config);
	checkf(Provider.IsValid(), TEXT("Failure constructing analytics provider!"));

	Provider->SetUserID(InSessionID.ToString());
}

FStandAloneTelemetry::~FStandAloneTelemetry()
{

}

void FStandAloneTelemetry::Start(const FString& InCurrentStream) const
{
	if (this->Provider == nullptr)
	{
		return;
	}

	Provider->RecordEvent(
		TEXT("SubmitTool.StandAlone.Start"),
		MakeAnalyticsEventAttributeArray(
			TEXT("Version"), FAppVersion::GetVersion(),
			TEXT("Stream"), InCurrentStream
		)
	);
}

void FStandAloneTelemetry::BlockFlush(float InTimeout) const
{
	if (Provider == nullptr)
	{
		return;
	}

	Provider->BlockUntilFlushed(InTimeout);
}
void FStandAloneTelemetry::CustomEvent(const FString& InEventId, const TArray<FAnalyticsEventAttribute>& InAttribs) const
{
	if(this->Provider == nullptr)
	{
		return;
	}

	Provider->RecordEvent(InEventId, InAttribs);
}

void FStandAloneTelemetry::SubmitSucceeded() const
{
	if (this->Provider == nullptr)
	{
		return;
	}

	Provider->RecordEvent(
		TEXT("SubmitTool.StandAlone.Submit.Succeeded"),
		MakeAnalyticsEventAttributeArray(
			TEXT("Version"), FAppVersion::GetVersion()
		)
	);
}