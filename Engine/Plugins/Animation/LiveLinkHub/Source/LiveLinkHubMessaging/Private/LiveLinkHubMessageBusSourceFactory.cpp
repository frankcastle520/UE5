// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkHubMessageBusSourceFactory.h"
#include "LiveLinkHubMessageBusSource.h"
#include "Misc/ConfigCacheIni.h"
#include "SLiveLinkMessageBusSourceFactory.h"


#define LOCTEXT_NAMESPACE "LiveLinkHubMessageBusSourceFactory"


FText ULiveLinkHubMessageBusSourceFactory::GetSourceDisplayName() const
{
	return LOCTEXT("SourceDisplayName", "Live Link Hub");
}

FText ULiveLinkHubMessageBusSourceFactory::GetSourceTooltip() const
{
	return LOCTEXT("SourceTooltip", "Creates a connection to a Live Link Hub instance.");
}

TSharedPtr<SWidget> ULiveLinkHubMessageBusSourceFactory::BuildCreationPanel(FOnLiveLinkSourceCreated InOnLiveLinkSourceCreated) const
{
	return SNew(SLiveLinkMessageBusSourceFactory)
		.OnSourceSelected(FOnLiveLinkMessageBusSourceSelected::CreateUObject(this, &ULiveLinkHubMessageBusSourceFactory::OnSourceSelected, InOnLiveLinkSourceCreated))
		.FactoryClass(GetClass());
}

TSharedPtr<FLiveLinkMessageBusSource> ULiveLinkHubMessageBusSourceFactory::MakeSource(const FText& Name,
																				   const FText& MachineName,
																				   const FMessageAddress& Address,
																				   double TimeOffset) const
{
	return MakeShared<FLiveLinkHubMessageBusSource>(Name, MachineName, Address, TimeOffset);
}

bool ULiveLinkHubMessageBusSourceFactory::IsEnabled() const
{
	return GConfig->GetBoolOrDefault(TEXT("LiveLinkHub"), TEXT("bEnableLLHMessageBusSourceFactory"), true, GEngineIni);
}


#undef LOCTEXT_NAMESPACE
