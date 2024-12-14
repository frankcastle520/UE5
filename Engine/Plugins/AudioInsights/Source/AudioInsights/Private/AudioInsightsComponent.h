// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "Insights/IUnrealInsightsModule.h"

namespace UE::Audio::Insights
{
	/**
	 * The component that makes Audio Insights available inside Unreal Insights.
	 */
	class FAudioInsightsComponent : public IInsightsComponent, public TSharedFromThis<FAudioInsightsComponent>
	{
	public:
		FAudioInsightsComponent() = default;
		virtual ~FAudioInsightsComponent();

		static TSharedPtr<FAudioInsightsComponent> CreateInstance();

		// IInsightsComponent overrides
		virtual void Initialize(IUnrealInsightsModule& InsightsModule) override;
		virtual void Shutdown() override;
		virtual void RegisterMajorTabs(IUnrealInsightsModule& InsightsModule) override;
		virtual void UnregisterMajorTabs() override;

		bool GetIsLiveSession() const;
		bool GetIsEditorTrace() const { return bIsEditorTrace; }

		DECLARE_MULTICAST_DELEGATE(FOnTabSpawn);
		inline static FOnTabSpawn OnTabSpawn;

	private:
		bool CanSpawnTab(const FSpawnTabArgs& Args) const;
		TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

		bool Tick(float DeltaTime);

		bool bIsInitialized = false;
		bool bIsEditorTrace = false;
		bool bCanSpawnTab   = false;

		FTickerDelegate OnTick;
		FTSTicker::FDelegateHandle OnTickHandle;

		inline static TSharedPtr<FAudioInsightsComponent> Instance;
	};
} // namespace UE::Audio::Insights
