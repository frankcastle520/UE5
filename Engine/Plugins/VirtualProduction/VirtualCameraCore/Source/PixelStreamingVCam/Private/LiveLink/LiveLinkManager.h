// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "VCamPixelStreamingLiveLink.h"

class UVCamPixelStreamingSession;

namespace UE::PixelStreamingVCam
{
	/** Manages the live link subjects for the UVCamPixelStreamingSession. */
	class FLiveLinkManager : public FNoncopyable
	{
	public:

		/** If Live Link is enabled, creates a live link subject (if not already) and sets its name to the StreamerId of Session. */
		void CreateOrRefreshSubjectFor(const UVCamPixelStreamingSession& Session);
		/** If a subject is associated with Session, destroys it. */
		void DestroySubjectFor(const UVCamPixelStreamingSession& Session);

		/** Pushes transform data for the given Session. */
		void PushTransformForSubject(const UVCamPixelStreamingSession& Session, const FTransform& Transform, double Timestamp);
		
	private:

		/** Holds all the live link subjects for Pixel Streaming. Created when the first subject is created. */
		TSharedPtr<FPixelStreamingLiveLinkSource> LiveLinkSource;

		struct FStreamingSessionData
		{
			/** The last subject name used by the associated UVCamPixelStreamingSession. */
			FName LastSubjectName;
		};

		/** Maps the last subject names used by each session, so it can be cleaned up. */
		TMap<TWeakObjectPtr<const UVCamPixelStreamingSession>, FStreamingSessionData> SessionData;

		/** Inits LiveLinkSource if not yet created. */
		FPixelStreamingLiveLinkSource* GetOrCreateLiveLinkSource();
	};
}


