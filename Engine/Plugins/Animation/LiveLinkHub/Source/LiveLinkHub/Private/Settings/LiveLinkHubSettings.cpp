// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkHubSettings.h"

#include "Config/LiveLinkHubFileUtilities.h"
#include "Config/LiveLinkHubTemplateTokens.h"

void ULiveLinkHubSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULiveLinkHubSettings, FilenameTemplate))
	{
		CalculateExampleOutput();
	}
}

void ULiveLinkHubSettings::CalculateExampleOutput()
{
	using namespace UE::LiveLinkHub::FileUtilities::Private;
	FFilenameTemplateData TemplateData;
	ParseFilenameTemplate(FilenameTemplate, TemplateData);
	FilenameOutput = TemplateData.FullPath;
}
