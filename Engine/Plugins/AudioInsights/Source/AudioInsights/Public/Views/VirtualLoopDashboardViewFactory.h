// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Views/TableDashboardViewFactory.h"


namespace UE::Audio::Insights
{
	class AUDIOINSIGHTS_API FVirtualLoopDashboardViewFactory : public FTraceObjectTableDashboardViewFactory
	{
	public:
		FVirtualLoopDashboardViewFactory();
		virtual ~FVirtualLoopDashboardViewFactory() = default;

		virtual FName GetName() const override;
		virtual FText GetDisplayName() const override;
		virtual FSlateIcon GetIcon() const override;
		virtual EDefaultDashboardTabStack GetDefaultTabStack() const override;

#if WITH_EDITOR
		DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnDebugDrawEntries, float /*InElapsed*/, const TArray<TSharedPtr<IDashboardDataViewEntry>>& /*InSelectedItems*/, ::Audio::FDeviceId /*InAudioDeviceId*/);
		inline static FOnDebugDrawEntries OnDebugDrawEntries;
#endif // WITH_EDITOR

	protected:
		virtual void ProcessEntries(FTraceTableDashboardViewFactory::EProcessReason Reason) override;
		virtual const TMap<FName, FTraceTableDashboardViewFactory::FColumnData>& GetColumns() const override;

		virtual void SortTable() override;

#if WITH_EDITOR
		virtual bool IsDebugDrawEnabled() const override;
		virtual void DebugDraw(float InElapsed, const TArray<TSharedPtr<IDashboardDataViewEntry>>& InSelectedItems, ::Audio::FDeviceId InAudioDeviceId) const;
#endif // WITH_EDITOR
	};
} // namespace UE::Audio::Insights
