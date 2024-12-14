// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkManager.h"

#include "BuiltinProviders/VCamPixelStreamingSession.h"

#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "PixelStreamingVCamLog.h"
#include "LiveLink/VCamPixelStreamingLiveLink.h"

namespace UE::PixelStreamingVCam
{
	void FLiveLinkManager::CreateOrRefreshSubjectFor(const UVCamPixelStreamingSession& Session)
	{
		if (FPixelStreamingLiveLinkSource* Source = GetOrCreateLiveLinkSource())
		{
			FStreamingSessionData* Data = SessionData.Find(&Session);
			if (Data)
			{
				LiveLinkSource->RemoveSubject(Data->LastSubjectName);
			}
			else
			{
				Data = &SessionData.Add(&Session);
			}
		
			const FName SubjectName = FName(Session.StreamerId);
			Data->LastSubjectName = SubjectName;
			Source->CreateSubject(SubjectName);
			Source->PushTransformForSubject(SubjectName, FTransform::Identity);
		}
	}

	void FLiveLinkManager::DestroySubjectFor(const UVCamPixelStreamingSession& Session)
	{
		if (!LiveLinkSource)
		{
			return;
		}
		
		FStreamingSessionData Data;
		if (SessionData.RemoveAndCopyValue(&Session, Data))
		{
			LiveLinkSource->RemoveSubject(Data.LastSubjectName);
		}
	}

	void FLiveLinkManager::PushTransformForSubject(const UVCamPixelStreamingSession& Session, const FTransform& Transform, double Timestamp)
	{
		if (const FStreamingSessionData* Data = SessionData.Find(&Session); Data && ensure(LiveLinkSource))
		{
			LiveLinkSource->PushTransformForSubject(Data->LastSubjectName, Transform, Timestamp);
		}
		else
		{
			UE_LOG(LogPixelStreamingVCam, Error, TEXT("Session %s has not registered any Live Link subject!"), *Session.GetPathName());
		}
	}

	FPixelStreamingLiveLinkSource* FLiveLinkManager::GetOrCreateLiveLinkSource()
	{
		IModularFeatures& ModularFeatures = IModularFeatures::Get();
		if (!ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
		{
			UE_LOG(LogPixelStreamingVCam, Warning, TEXT("Live Link is not enabled."))
			return nullptr;
		}
		ILiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		
		LiveLinkSource = LiveLinkSource ? LiveLinkSource : MakeShared<FPixelStreamingLiveLinkSource>();
		// HasSourceBeenAdded is obviously false right after MakeShared is called.
		// However, when in subsequent GetOrCreateLiveLinkSource calls, the user may have manually removed the live link source in the UI.
		// It must be re-added, or we won't get any LiveLink for Pixel Streaming for the rest of the editor session.
		if (!LiveLinkClient->HasSourceBeenAdded(LiveLinkSource))
		{
			LiveLinkClient->AddSource(LiveLinkSource);
		}
		
		return LiveLinkSource.Get();
	}
}
