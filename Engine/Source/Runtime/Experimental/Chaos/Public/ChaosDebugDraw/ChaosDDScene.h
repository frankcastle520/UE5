// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ChaosDebugDraw/ChaosDDTypes.h"
#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "Math/Sphere.h"

#if CHAOS_DEBUG_DRAW

namespace ChaosDD::Private
{
	class IChaosDDRenderer;
	class FChaosDDTimeline;

	//
	// Debug draw system for a world. In PIE there will be one of these for the server and each client.
	//
	// @todo(chaos): enable retention of debug draw frames and debug draw from a specific time
	class CHAOS_API FChaosDDScene : public TSharedFromThis<FChaosDDScene>
	{
	public:
		FChaosDDScene(const FString& InName, bool bIsServer);
		~FChaosDDScene();

		const FString& GetName() const
		{
			return Name;
		}

		bool IsServer() const;
		void SetRenderEnabled(bool bInRenderEnabled);
		bool IsRenderEnabled() const;

		// Specify the region of in which we wish to enable debug draw. A radius of zero means everywhere.
		void SetDrawRegion(const FSphere3d& InDrawRegion);

		// The region of interest
		const FSphere3d& GetDrawRegion() const;

		// Set the line budget for debug draw
		void SetCommandBudget(int32 InCommandBudget);

		// The number of commands we can draw (also max number of lines for now)
		int32 GetCommandBudget() const;

		// Create a new timeline. E.g., PT, GT, RBAN
		// The caller must hold a shared pointer to the timeline to keep it alive.
		FChaosDDTimelinePtr CreateTimeline(const FString& Name);

		// Collect all the latest complete frames for rendering
		TArray<FChaosDDFramePtr> GetLatestFrames();

	private:
		TArray<FChaosDDFramePtr> GetFrames();
		void PruneTimelines();

		mutable FCriticalSection TimelinesCS;

		FString Name;
		TArray<FChaosDDTimelineWeakPtr> Timelines;
		FSphere3d DrawRegion;
		int32 CommandBudget;
		bool bIsServer;
		bool bRenderEnabled;
	};
}

#endif