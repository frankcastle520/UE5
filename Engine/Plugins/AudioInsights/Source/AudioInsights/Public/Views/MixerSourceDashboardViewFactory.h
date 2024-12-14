// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Framework/Docking/TabManager.h"
#include "Messages/MixerSourceTraceMessages.h"
#include "Views/SAudioCurveView.h"
#include "Views/TableDashboardViewFactory.h"
#include "Widgets/Input/SCheckBox.h"

namespace UE::Audio::Insights
{
	class AUDIOINSIGHTS_API FMixerSourceDashboardViewFactory : public FTraceObjectTableDashboardViewFactory
	{
	public:
		FMixerSourceDashboardViewFactory();
		struct FPlotColumnInfo
		{
			const TFunctionRef<const ::Audio::TCircularAudioBuffer<FDataPoint>&(const IDashboardDataViewEntry& InData)> DataFunc;
			const FNumberFormattingOptions* FormatOptions;
		};

		virtual ~FMixerSourceDashboardViewFactory() = default;

		virtual FName GetName() const override;
		virtual FText GetDisplayName() const override;
		virtual FSlateIcon GetIcon() const override;
		virtual EDefaultDashboardTabStack GetDefaultTabStack() const override;
		virtual void ProcessEntries(FTraceTableDashboardViewFactory::EProcessReason Reason) override;
		virtual TSharedRef<SWidget> MakeWidget() override;

		// Maximum amount of data history kept for plots (in seconds)
		static const double MaxPlotHistorySeconds;
		// Maximum number of sources to plot at once 
		static const int32 MaxPlotSources; 

#if WITH_EDITOR
		DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnUpdateMuteSoloState, ECheckBoxState /*MuteState*/, ECheckBoxState /*SoloState*/, const FString& /*CurrentFilterString*/);
		inline static FOnUpdateMuteSoloState OnUpdateMuteSoloState;
#endif // WITH_EDITOR

	protected:
		virtual const TMap<FName, FTraceTableDashboardViewFactory::FColumnData>& GetColumns() const override;
		virtual void SortTable() override;

		TSharedRef<SWidget> MakePlotsWidget();

	private:
		void ResetPlots();

#if WITH_EDITOR
		void OnPIEStarted(bool bSimulating);
		void OnPIEStopped(bool bSimulating);
		void OnPIEPaused(bool bSimulating);
		void OnPIEResumed(bool bSimulating);
#else
		void OnAudioInsightsComponentTabSpawn();
#endif // WITH_EDITOR

		void UpdatePlotsWidgetsData();

#if WITH_EDITOR
		void UpdateMuteSoloState();
#endif // WITH_EDITOR

		// Column information used by plot widgets, keyed by column name. These keys should be a subset of the keys in GetColumns(). 
		const TMap<FName, FPlotColumnInfo>& GetPlotColumnInfo();
		const TFunctionRef<const ::Audio::TCircularAudioBuffer<FDataPoint>&(const IDashboardDataViewEntry& InData)> GetPlotColumnDataFunc(const FName& ColumnName);
		const FNumberFormattingOptions* GetPlotColumnNumberFormat(const FName& ColumnName);
		const FText GetPlotColumnDisplayName(const FName& ColumnName);

#if WITH_EDITOR
		TSharedRef<SWidget> MakeMuteSoloWidget();

		void ToggleMuteForAllItems(ECheckBoxState NewState);
		void ToggleSoloForAllItems(ECheckBoxState NewState);

		TSharedRef<FTabManager::FLayout> LoadLayoutFromConfig();
		void SaveLayoutToConfig();
#endif // WITH EDITOR

		TSharedRef<SWidget> MakePlotsButtonWidget();
		void TogglePlotsTabVisibility(ECheckBoxState InCheckboxState);

		TSharedRef<SDockTab> CreateMixerSourcesTab(const FSpawnTabArgs& Args);
		TSharedRef<SDockTab> CreatePlotsTab(const FSpawnTabArgs& Args);

		void RegisterTabSpawners();
		void UnregisterTabSpawners();

		TSharedRef<FTabManager::FLayout> GetDefaultTabLayout();

		using FPlotCurvePoint = SAudioCurveView::FCurvePoint;
		// Map of source id to data point array 
		using FPointDataPerCurveMap = TMap<int32, TArray<FPlotCurvePoint>>;
		using FPlotCurveMetadata = SAudioCurveView::FCurveMetadata;

		FCheckBoxStyle MuteToggleButtonStyle;
		FCheckBoxStyle SoloToggleButtonStyle;

		TSharedPtr<SCheckBox> MuteToggleButton;
		TSharedPtr<SCheckBox> SoloToggleButton;

		TSharedPtr<SCheckBox> PlotsButton;

		// Curve points per timestamp per source id per column name 
		TMap<FName, TSharedPtr<FPointDataPerCurveMap>> PlotWidgetCurveIdToPointDataMapPerColumn;
		// SourceId to metadata for the corresponding curve
		TSharedPtr<TMap<int32, FPlotCurveMetadata>> PlotWidgetMetadataPerCurve;
		
		// Column names for plot selector widget 
		TArray<FName> ColumnNames;
		
		enum class EGameState : uint8
		{
			Running,
			Stopped,
			Paused
		};

		EGameState GameState = EGameState::Stopped;

		double BeginTimestamp = TNumericLimits<double>::Max();
		double CurrentTimestamp = 0.0;

		const static int32 NumPlotWidgets = 1;
		TArray<FName> SelectedPlotColumnNames;
		TArray<TSharedPtr<SAudioCurveView>> PlotWidgets;

		TSharedPtr<SWidget> PlotsWidget;

		TSharedPtr<FTabManager> MixerSourcesTabManager;
		TSharedPtr<FWorkspaceItem> MixerSourcesWorkspace;

#if WITH_EDITOR
		// State of the mute and solo buttons
		ECheckBoxState MuteState = ECheckBoxState::Unchecked;
		ECheckBoxState SoloState = ECheckBoxState::Unchecked;
		FString CurrentFilterString;
#endif // WITH_EDITOR
	};
} // namespace UE::Audio::Insights
