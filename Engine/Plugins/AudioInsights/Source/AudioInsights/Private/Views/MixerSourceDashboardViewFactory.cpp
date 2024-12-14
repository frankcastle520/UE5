// Copyright Epic Games, Inc. All Rights Reserved.
#include "Views/MixerSourceDashboardViewFactory.h"

#include "Async/Async.h"
#include "AudioInsightsModule.h"
#include "AudioInsightsStyle.h"
#include "DSP/Dsp.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Docking/TabManager.h"
#include "Internationalization/Text.h"
#include "Providers/MixerSourceTraceProvider.h"
#include "SSimpleTimeSlider.h"
#include "Styling/AppStyle.h"
#include "Templates/SharedPointer.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/Input/SComboBox.h"

#if WITH_EDITOR
#include "Editor.h"
#else
#include "AudioInsightsComponent.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "AudioInsights"


namespace UE::Audio::Insights
{
	namespace MixerSourcePrivate
	{
		const FMixerSourceDashboardEntry& CastEntry(const IDashboardDataViewEntry& InData)
		{
			return static_cast<const FMixerSourceDashboardEntry&>(InData);
		};

		float GetLastEntryArrayValue(const ::Audio::TCircularAudioBuffer<FDataPoint>& EnvelopeDataPoints)
		{
			float LastValue = 0.0f;

			if (EnvelopeDataPoints.Num() > 0)
			{
				const ::Audio::DisjointedArrayView<const FDataPoint> EnvelopeDataPointsDisjointedArrayView = EnvelopeDataPoints.PeekInPlace(EnvelopeDataPoints.Num());
				LastValue = EnvelopeDataPointsDisjointedArrayView.FirstBuffer.Last().Value;
			}

			return LastValue;
		};

		const FText PlotColumnSelectDescription = LOCTEXT("AudioDashboard_MixerSources_SelectPlotColumnDescription", "Select a column from the table to plot.");
		const FText PlotsIconDescription = LOCTEXT("AudioDashboard_MixerSources_PlotsIconDescription", "Show/Hides the Mixer Sources Plots section.");

		const FText MixerSourcesWorkspaceName = LOCTEXT("MixerSourcesWorkspace_Name", "MixerSourcesWorkspace");

		const FName MixerSourcesTableTabName = "MixerSourcesTableTab";
		const FName MixerSourcesPlotsTabName = "MixerSourcesPlotsTab";

	} // namespace MixerSourcePrivate

	const double FMixerSourceDashboardViewFactory::MaxPlotHistorySeconds = 5.0;
	const int32 FMixerSourceDashboardViewFactory::MaxPlotSources = 16;

	FMixerSourceDashboardViewFactory::FMixerSourceDashboardViewFactory()
	{
		const FTraceModule& TraceModule = static_cast<FTraceModule&>(FAudioInsightsModule::GetChecked().GetTraceModule());
		Providers = TArray<TSharedPtr<FTraceProviderBase>>
		{
			TraceModule.FindAudioTraceProvider<FMixerSourceTraceProvider>()
		};
	}

	FName FMixerSourceDashboardViewFactory::GetName() const
	{
		return "MixerSources";
	}

	FText FMixerSourceDashboardViewFactory::GetDisplayName() const
	{
		return LOCTEXT("AudioDashboard_MixerSources_DisplayName", "Sources");
	}

	FSlateIcon FMixerSourceDashboardViewFactory::GetIcon() const
	{
		return FSlateStyle::Get().CreateIcon("AudioInsights.Icon.Sources");
	}

	EDefaultDashboardTabStack FMixerSourceDashboardViewFactory::GetDefaultTabStack() const
	{
		return EDefaultDashboardTabStack::Analysis;
	}

	const TMap<FName, FTraceTableDashboardViewFactory::FColumnData>& FMixerSourceDashboardViewFactory::GetColumns() const
	{
		auto CreateColumnData = []()
		{
			return TMap<FName, FTraceTableDashboardViewFactory::FColumnData>
			{
				{
					"PlayOrder",
					{
						LOCTEXT("PlayOrder_PlayOrderColumnDisplayName", "Play Order"),
						[](const IDashboardDataViewEntry& InData) { return FText::AsNumber(MixerSourcePrivate::CastEntry(InData).PlayOrder); },
						true /* bDefaultHidden */,
						0.08f /* FillWidth */
					}
				},
				{
					"Name",
					{
						LOCTEXT("Source_NameColumnDisplayName", "Name"),
						[](const IDashboardDataViewEntry& InData) { return FText::FromString(*FSoftObjectPath(MixerSourcePrivate::CastEntry(InData).Name).GetAssetName()); },
						false /* bDefaultHidden */,
						0.75f /* FillWidth */
					}
				},
				{
					"Amplitude",
					{
						LOCTEXT("Source_EnvColumnDisplayName", "Amp (Peak)"),
						[](const IDashboardDataViewEntry& InData)
						{
							const ::Audio::TCircularAudioBuffer<FDataPoint>& EnvelopeDataPoints = MixerSourcePrivate::CastEntry(InData).EnvelopeDataPoints;
							return FText::AsNumber(MixerSourcePrivate::GetLastEntryArrayValue(EnvelopeDataPoints), FSlateStyle::Get().GetAmpFloatFormat());
						},
						false /* bDefaultHidden */,
						0.12f /* FillWidth */
					}
				},
				{
					"Volume",
					{
						LOCTEXT("Source_VolumeColumnDisplayName", "Volume"),
						[](const IDashboardDataViewEntry& InData)
						{
							const ::Audio::TCircularAudioBuffer<FDataPoint>& VolumeDataPoints = MixerSourcePrivate::CastEntry(InData).VolumeDataPoints;
							return FText::AsNumber(MixerSourcePrivate::GetLastEntryArrayValue(VolumeDataPoints), FSlateStyle::Get().GetAmpFloatFormat());
						},
						false /* bDefaultHidden */,
						0.07f /* FillWidth */
					}
				},
				{
					"DistanceAttenuation",
					{
						LOCTEXT("Source_AttenuationColumnDisplayName", "Distance Attenuation"),
						[](const IDashboardDataViewEntry& InData)
						{
							const ::Audio::TCircularAudioBuffer<FDataPoint>& DistanceAttenuationDataPoints = MixerSourcePrivate::CastEntry(InData).DistanceAttenuationDataPoints;
							return FText::AsNumber(MixerSourcePrivate::GetLastEntryArrayValue(DistanceAttenuationDataPoints), FSlateStyle::Get().GetAmpFloatFormat());
						},
						true  /* bDefaultHidden */,
						0.15f /* FillWidth */
					}
				},
				{
					"Pitch",
					{
						LOCTEXT("Source_PitchColumnDisplayName", "Pitch"),
						[](const IDashboardDataViewEntry& InData)
						{
							const ::Audio::TCircularAudioBuffer<FDataPoint>& PitchDataPoints = MixerSourcePrivate::CastEntry(InData).PitchDataPoints;
							return FText::AsNumber(MixerSourcePrivate::GetLastEntryArrayValue(PitchDataPoints), FSlateStyle::Get().GetPitchFloatFormat());
						},
						false /* bDefaultHidden */,
						0.06f /* FillWidth */
					}
				},
				{
					"LPF",
					{
						LOCTEXT("Source_LPFColumnDisplayName", "LPF Freq (Hz)"),
						[](const IDashboardDataViewEntry& InData)
						{
							const ::Audio::TCircularAudioBuffer<FDataPoint>& LPFFreqDataPoints = MixerSourcePrivate::CastEntry(InData).LPFFreqDataPoints;
							return FText::AsNumber(MixerSourcePrivate::GetLastEntryArrayValue(LPFFreqDataPoints), FSlateStyle::Get().GetFreqFloatFormat());
						},
						true  /* bDefaultHidden */,
						0.1f /* FillWidth */
					}
				},
				{
					"HPF",
					{
						LOCTEXT("Source_HPFColumnDisplayName", "HPF Freq (Hz)"),
						[](const IDashboardDataViewEntry& InData)
						{
							const ::Audio::TCircularAudioBuffer<FDataPoint>& HPFFreqDataPoints = MixerSourcePrivate::CastEntry(InData).HPFFreqDataPoints;
							return FText::AsNumber(MixerSourcePrivate::GetLastEntryArrayValue(HPFFreqDataPoints), FSlateStyle::Get().GetFreqFloatFormat());
						},
						true  /* bDefaultHidden */,
						0.1f /* FillWidth */
					}
				}
			};
		};
		static const TMap<FName, FTraceTableDashboardViewFactory::FColumnData> ColumnData = CreateColumnData();
		return ColumnData;
	}

	void FMixerSourceDashboardViewFactory::SortTable()
	{
		if (SortByColumn == "PlayOrder")
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return AData.PlayOrder < BData.PlayOrder;
				});
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return BData.PlayOrder < AData.PlayOrder;
				});
			}
		}
		else if (SortByColumn == "Name")
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return AData.GetDisplayName().CompareToCaseIgnored(BData.GetDisplayName()) < 0;
				});
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return BData.GetDisplayName().CompareToCaseIgnored(AData.GetDisplayName()) < 0;
				});
			}
		}
		else if (SortByColumn == "Amplitude")
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(AData.EnvelopeDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(BData.EnvelopeDataPoints);
				});
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(BData.EnvelopeDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(AData.EnvelopeDataPoints);
				});
			}
		}
		else if (SortByColumn == "Volume")
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(AData.VolumeDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(BData.VolumeDataPoints);
				});
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(BData.VolumeDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(AData.VolumeDataPoints);
				});
			}
		}
		else if (SortByColumn == "DistanceAttenuation")
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(AData.DistanceAttenuationDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(BData.DistanceAttenuationDataPoints);
				});
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(BData.DistanceAttenuationDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(AData.DistanceAttenuationDataPoints);
				});
			}
		}
		else if (SortByColumn == "Pitch")
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(AData.PitchDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(BData.PitchDataPoints);
				});
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(BData.PitchDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(AData.PitchDataPoints);
				});
			}
		}
		else if (SortByColumn == "LPF")
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(AData.LPFFreqDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(BData.LPFFreqDataPoints);
				});
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(BData.LPFFreqDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(AData.LPFFreqDataPoints);
				});
			}
		}
		else if (SortByColumn == "HPF")
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(AData.HPFFreqDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(BData.HPFFreqDataPoints);
				});
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FMixerSourceDashboardEntry& AData = MixerSourcePrivate::CastEntry(*A.Get());
					const FMixerSourceDashboardEntry& BData = MixerSourcePrivate::CastEntry(*B.Get());

					return MixerSourcePrivate::GetLastEntryArrayValue(BData.HPFFreqDataPoints) < MixerSourcePrivate::GetLastEntryArrayValue(AData.HPFFreqDataPoints);
				});
			}
		}
	}

	void FMixerSourceDashboardViewFactory::ResetPlots()
	{
		for (const auto& KVP : PlotWidgetCurveIdToPointDataMapPerColumn)
		{
			const TSharedPtr<FPointDataPerCurveMap>& PointDataPerCurveMap = KVP.Value;
			PointDataPerCurveMap->Empty();
		}

		if (PlotWidgetMetadataPerCurve.IsValid())
		{
			PlotWidgetMetadataPerCurve->Empty();
		}

		BeginTimestamp = TNumericLimits<double>::Max();
		CurrentTimestamp = 0;
	}

#if WITH_EDITOR
	void FMixerSourceDashboardViewFactory::OnPIEStarted(bool bSimulating)
	{
		GameState = EGameState::Running;
	}

	void FMixerSourceDashboardViewFactory::OnPIEStopped(bool bSimulating)
	{
		ResetPlots();

		GameState = EGameState::Stopped;
	}

	void FMixerSourceDashboardViewFactory::OnPIEPaused(bool bSimulating)
	{
		GameState = EGameState::Paused;
	}

	void FMixerSourceDashboardViewFactory::OnPIEResumed(bool bSimulating)
	{
		GameState = EGameState::Running;
	}
#else
	void FMixerSourceDashboardViewFactory::OnAudioInsightsComponentTabSpawn()
	{
		const TSharedPtr<const FAudioInsightsComponent> AudioInsightsComponent = FAudioInsightsModule::GetChecked().GetAudioInsightsComponent();
		if (AudioInsightsComponent.IsValid())
		{
			GameState = AudioInsightsComponent->GetIsLiveSession() ? EGameState::Running : EGameState::Stopped;
		}
	}
#endif // WITH_EDITOR

#if WITH_EDITOR
	void FMixerSourceDashboardViewFactory::ToggleMuteForAllItems(ECheckBoxState NewState)
	{
		if (MuteState != NewState)
		{
			MuteState = NewState;
			UpdateMuteSoloState();
		}
	}

	void FMixerSourceDashboardViewFactory::ToggleSoloForAllItems(ECheckBoxState NewState)
	{
		if (SoloState != NewState)
		{
			SoloState = NewState;
			UpdateMuteSoloState();
		}
	}

	void FMixerSourceDashboardViewFactory::UpdateMuteSoloState()
	{
		OnUpdateMuteSoloState.Broadcast(MuteState, SoloState, CurrentFilterString);
	}
#endif // WITH_EDITOR

	void FMixerSourceDashboardViewFactory::UpdatePlotsWidgetsData()
	{
		if (!PlotWidgetMetadataPerCurve.IsValid() || DataViewEntries.Num() <= 0)
		{
			return;
		}

		// Process new data
		bool bHasNewMetadata = false;
		for (const TSharedPtr<IDashboardDataViewEntry>& DataEntry : DataViewEntries)
		{
			const FMixerSourceDashboardEntry& SourceDataPoint = MixerSourcePrivate::CastEntry(*DataEntry);
			const uint32 SourceId = SourceDataPoint.SourceId; 

			if (SourceId == INDEX_NONE)
			{
				break;
			}
			
			// Only add new sources if there are less than the max 
			const bool bCanAddNewSources = PlotWidgetMetadataPerCurve->Num() < MaxPlotSources;

			// For each column, get the array for this data point's source id and add the value to that data array
			for (const auto& [ColumnName, DataMap] : PlotWidgetCurveIdToPointDataMapPerColumn)
			{
				// Add new data point array
				if (bCanAddNewSources && !DataMap->Contains(SourceId))
				{
					DataMap->Add(SourceId);
				}

				// Get the data point array for this source id, add new point
				TArray<FPlotCurvePoint>* DataPoints = DataMap->Find(SourceId);
				if (DataPoints)
				{
					auto DataFunc = GetPlotColumnDataFunc(ColumnName);
					const ::Audio::TCircularAudioBuffer<FDataPoint>& TimeStampedValues = (DataFunc)(SourceDataPoint);

					const ::Audio::DisjointedArrayView<const FDataPoint> TimeStampedValuesDisjointedArrayView = TimeStampedValues.PeekInPlace(TimeStampedValues.Num());
					
					for (const auto& [Timestamp, Value] : TimeStampedValuesDisjointedArrayView.FirstBuffer)
					{
						BeginTimestamp   = FMath::Min(BeginTimestamp, Timestamp);
						CurrentTimestamp = FMath::Max(CurrentTimestamp, Timestamp);

						const double DataPointTime = Timestamp - BeginTimestamp;

						DataPoints->Emplace(DataPointTime, Value);
					}
				}
			}

			// Create metadata for this curve if necessary 
			if (bCanAddNewSources && !PlotWidgetMetadataPerCurve->Contains(SourceId))
			{
				FPlotCurveMetadata& NewMetadata = PlotWidgetMetadataPerCurve->Add(SourceId);
				NewMetadata.CurveId = SourceId;
				NewMetadata.CurveColor = FLinearColor(FColor::MakeRandomColor()); 
				NewMetadata.DisplayName = FText::FromString(*FSoftObjectPath(SourceDataPoint.Name).GetAssetName());
				bHasNewMetadata = true;
			}
		}

		// Set metadata for each widget if updated
		if (bHasNewMetadata)
		{
			for (TSharedPtr<SAudioCurveView> PlotWidget : PlotWidgets)
			{
				PlotWidget->SetCurvesMetadata(PlotWidgetMetadataPerCurve);
			}
		}

		// Remove old points and set curve data for each widget 
		const double PlotDrawLimitTimestamp = CurrentTimestamp - BeginTimestamp - (MaxPlotHistorySeconds + 0.2 /* extra grace time to avoid curve cuts being displayed */);

		for (int32 WidgetIndex = 0; WidgetIndex < NumPlotWidgets; ++WidgetIndex)
		{
			const FName& SelectedPlotColumn = SelectedPlotColumnNames[WidgetIndex];

			const TSharedPtr<FPointDataPerCurveMap> CurveDataMapPtr = *PlotWidgetCurveIdToPointDataMapPerColumn.Find(SelectedPlotColumn);
			if (CurveDataMapPtr.IsValid())
			{
				// Remove points that are older than max history limit from the most recent timestamp
				for (auto& [CurveId, CurvePoints] : *CurveDataMapPtr)
				{
					const int32 FoundIndex = CurvePoints.IndexOfByPredicate([&PlotDrawLimitTimestamp](const FDataPoint& InDataPoint)
					{
						return InDataPoint.Key >= PlotDrawLimitTimestamp;
					});

					if (FoundIndex > 0)
					{
						CurvePoints.RemoveAt(0, FoundIndex, EAllowShrinking::No);
					}
				}

				PlotWidgets[WidgetIndex]->SetCurvesPointData(CurveDataMapPtr);
			}
		}
	}

	const TMap<FName, FMixerSourceDashboardViewFactory::FPlotColumnInfo>& FMixerSourceDashboardViewFactory::GetPlotColumnInfo()
	{
		auto CreatePlotColumnInfo = []()
		{
			return TMap<FName, FMixerSourceDashboardViewFactory::FPlotColumnInfo>
			{
				{
					"Amplitude",
					{
						[](const IDashboardDataViewEntry& InData) -> const ::Audio::TCircularAudioBuffer<FDataPoint>& { return MixerSourcePrivate::CastEntry(InData).EnvelopeDataPoints; },
						FSlateStyle::Get().GetAmpFloatFormat()
					}
				},
				{
					"Volume",
					{

						[](const IDashboardDataViewEntry& InData) -> const ::Audio::TCircularAudioBuffer<FDataPoint>& { return MixerSourcePrivate::CastEntry(InData).VolumeDataPoints; },
						FSlateStyle::Get().GetAmpFloatFormat()
					}
				},
				{
					"DistanceAttenuation",
					{
						[](const IDashboardDataViewEntry& InData) -> const ::Audio::TCircularAudioBuffer<FDataPoint>& { return MixerSourcePrivate::CastEntry(InData).DistanceAttenuationDataPoints; },
						FSlateStyle::Get().GetAmpFloatFormat()
					}
				},
				{
					"Pitch",
					{
						[](const IDashboardDataViewEntry& InData) -> const ::Audio::TCircularAudioBuffer<FDataPoint>& { return MixerSourcePrivate::CastEntry(InData).PitchDataPoints; },
						FSlateStyle::Get().GetPitchFloatFormat()
					}
				},
				{
					"LPF",
					{
						[](const IDashboardDataViewEntry& InData) -> const ::Audio::TCircularAudioBuffer<FDataPoint>& { return MixerSourcePrivate::CastEntry(InData).LPFFreqDataPoints; },
						FSlateStyle::Get().GetFreqFloatFormat()
					}
				},
				{
					"HPF",
					{
						[](const IDashboardDataViewEntry& InData) -> const ::Audio::TCircularAudioBuffer<FDataPoint>& { return MixerSourcePrivate::CastEntry(InData).HPFFreqDataPoints; },
						FSlateStyle::Get().GetFreqFloatFormat()
					}
				}
			};
		};
		static const TMap<FName, FMixerSourceDashboardViewFactory::FPlotColumnInfo> ColumnInfo = CreatePlotColumnInfo();
		return ColumnInfo;
	}

	const FNumberFormattingOptions* FMixerSourceDashboardViewFactory::GetPlotColumnNumberFormat(const FName& ColumnName)
	{
		if (const FMixerSourceDashboardViewFactory::FPlotColumnInfo* PlotColumnInfo = GetPlotColumnInfo().Find(ColumnName))
		{
			return PlotColumnInfo->FormatOptions;
		}
		return nullptr;
	}

	const TFunctionRef<const ::Audio::TCircularAudioBuffer<FDataPoint>&(const IDashboardDataViewEntry& InData)> FMixerSourceDashboardViewFactory::GetPlotColumnDataFunc(const FName& ColumnName)
	{
		return GetPlotColumnInfo().Find(ColumnName)->DataFunc;
	}

	const FText FMixerSourceDashboardViewFactory::GetPlotColumnDisplayName(const FName& ColumnName)
	{
		if (const FTraceTableDashboardViewFactory::FColumnData* ColumnInfo = GetColumns().Find(ColumnName))
		{
			return ColumnInfo->DisplayName;
		}
		return FText::GetEmpty();
	}

	TSharedRef<SWidget> FMixerSourceDashboardViewFactory::MakePlotsWidget()
	{
		// Initialize column options and initially selected columns 
		GetPlotColumnInfo().GenerateKeyArray(ColumnNames);
		if (SelectedPlotColumnNames.IsEmpty() && ColumnNames.Num() > 3)
		{
			SelectedPlotColumnNames.Add(ColumnNames[0]); // Amplitude
			SelectedPlotColumnNames.Add(ColumnNames[3]); // Pitch
		}

		// Initialize curve data and metadata
		if (!PlotWidgetMetadataPerCurve.IsValid())
		{
			PlotWidgetMetadataPerCurve = MakeShared<TMap<int32, SAudioCurveView::FCurveMetadata>>();
			for (const FName& ColumnName : ColumnNames)
			{
				TSharedPtr<FPointDataPerCurveMap> PointDataPerCurveMap = MakeShared<FPointDataPerCurveMap>();
				PlotWidgetCurveIdToPointDataMapPerColumn.Emplace(ColumnName, MoveTemp(PointDataPerCurveMap));
			}
		}

		// Create plot widgets
		auto GetViewRange = [this]()
		{
			if (GameState == EGameState::Stopped || BeginTimestamp == TNumericLimits<double>::Max())
			{
				return TRange<double>(0, MaxPlotHistorySeconds);
			}

			if (GameState == EGameState::Running)
			{
				const FTraceModule& TraceModule = static_cast<FTraceModule&>(FAudioInsightsModule::GetChecked().GetTraceModule());
				const double FirstTimestamp = TraceModule.GetFirstTimeStamp();

				const double CurrentTime = FPlatformTime::Seconds();
				const double RelativeCurrentTime = CurrentTime - FirstTimestamp;
				const double TimestampsDiff = RelativeCurrentTime - (CurrentTimestamp - BeginTimestamp);

				CurrentTimestamp += TimestampsDiff;
			}

			return TRange<double>(CurrentTimestamp - MaxPlotHistorySeconds - BeginTimestamp, CurrentTimestamp - BeginTimestamp);
		};

		if (PlotWidgets.IsEmpty())
		{
			PlotWidgets.AddDefaulted(NumPlotWidgets);
			for (int32 WidgetNum = 0; WidgetNum < NumPlotWidgets; ++WidgetNum)
			{
				SAssignNew(PlotWidgets[WidgetNum], SAudioCurveView)
				.ViewRange_Lambda(GetViewRange)
				.PixelSnappingMethod(EWidgetPixelSnapping::Disabled);
			}
		}

		// Create plot column combo box widgets  
		auto CreatePlotColumnComboBoxWidget = [this](int32 PlotWidgetIndex)
		{
			return SNew(SComboBox<FName>)
				.ToolTipText(MixerSourcePrivate::PlotColumnSelectDescription)
				.OptionsSource(&ColumnNames)
				.OnGenerateWidget_Lambda([this](const FName& ColumnName)
				{
					return SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(GetPlotColumnDisplayName(ColumnName));
				})
				.OnSelectionChanged_Lambda([this, PlotWidgetIndex](FName NewColumnName, ESelectInfo::Type)
				{
					SelectedPlotColumnNames[PlotWidgetIndex] = NewColumnName;
					if (TSharedPtr<FPointDataPerCurveMap>* DataMap = PlotWidgetCurveIdToPointDataMapPerColumn.Find(NewColumnName))
					{
						PlotWidgets[PlotWidgetIndex]->SetCurvesPointData(*DataMap);
						PlotWidgets[PlotWidgetIndex]->SetYValueFormattingOptions(*GetPlotColumnNumberFormat(NewColumnName));
					}
				})
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text_Lambda([this, PlotWidgetIndex]()
					{
						return GetPlotColumnDisplayName(SelectedPlotColumnNames[PlotWidgetIndex]);
					})
				];
		};

		return SNew(SVerticalBox)
			.Clipping(EWidgetClipping::ClipToBounds)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				SNew(SSimpleTimeSlider)
				.ViewRange_Lambda(GetViewRange)
				.ClampRangeHighlightSize(0.0f) // Hide clamp range
				.ScrubPosition_Lambda([]() { return TNumericLimits<double>::Lowest(); }) // Hide scrub
				.PixelSnappingMethod(EWidgetPixelSnapping::Disabled)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				CreatePlotColumnComboBoxWidget(0)
			]
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			[
				PlotWidgets[0].ToSharedRef()
			];
	}

#if WITH_EDITOR
	TSharedRef<SWidget> FMixerSourceDashboardViewFactory::MakeMuteSoloWidget()
	{
		// Mute/Solo labels generation
		auto GenerateToggleButtonLabelWidget = [](const FText& InLabel = FText::GetEmpty(), const FName& InTextStyle = TEXT("ButtonText")) -> TSharedRef<SWidget>
		{
       		TSharedPtr<SHorizontalBox> HBox = SNew(SHorizontalBox);
			
       		if (!InLabel.IsEmpty())
       		{
       			HBox->AddSlot()	
				.Padding(0.0f, 0.5f, 0.0f, 0.0f)
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle( &FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>(InTextStyle))
					.Justification(ETextJustify::Center)
					.Text(InLabel)
				];
       		}

       		return SNew(SBox)
				.HeightOverride(16.0f)
				[
					HBox.ToSharedRef()
				];
		};
		
		const FSlateColor WhiteColor(FColor::White);

		// Mute button style
		MuteToggleButtonStyle = FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckBox");
		MuteToggleButtonStyle.BorderBackgroundColor = FSlateColor(FColor(200, 0, 0));

		MuteToggleButtonStyle.CheckedHoveredImage.TintColor = WhiteColor;
		MuteToggleButtonStyle.CheckedImage.TintColor        = WhiteColor;
		MuteToggleButtonStyle.CheckedPressedImage.TintColor = WhiteColor;

		// Solo button style
		SoloToggleButtonStyle = FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckBox");
		SoloToggleButtonStyle.BorderBackgroundColor = FSlateColor(FColor(255, 200, 0));

		SoloToggleButtonStyle.CheckedHoveredImage.TintColor = WhiteColor;
		SoloToggleButtonStyle.CheckedImage.TintColor        = WhiteColor;
		SoloToggleButtonStyle.CheckedPressedImage.TintColor = WhiteColor;

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Margin(FMargin(0.0, 2.0, 0.0, 0.0))
				.Text(LOCTEXT("TableDashboardView_GlobalMuteSoloText", "Global Mute/Solo:"))
			]
			+ SHorizontalBox::Slot()
			.MaxWidth(2.0f)
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(EHorizontalAlignment::HAlign_Center)
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SAssignNew(MuteToggleButton, SCheckBox)
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.Style(&MuteToggleButtonStyle)
				.ToolTip(FSlateApplicationBase::Get().MakeToolTip(LOCTEXT("TableDashboardView_MuteButtonTooltipText", "Mute/Unmute all the items in the list.")))
				.IsChecked(ECheckBoxState::Unchecked)
				.OnCheckStateChanged(this, &FMixerSourceDashboardViewFactory::ToggleMuteForAllItems)
				[
					GenerateToggleButtonLabelWidget(LOCTEXT("TableDashboardView_MuteButtonText", "M"), "SmallButtonText")
				]
			]
			+ SHorizontalBox::Slot()
			.MaxWidth(2.0f)
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(EHorizontalAlignment::HAlign_Center)
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SAssignNew(SoloToggleButton, SCheckBox)
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.Style(&SoloToggleButtonStyle)
				.ToolTip(FSlateApplicationBase::Get().MakeToolTip(LOCTEXT("TableDashboardView_SoloButtonTooltipText", "Enabled/Disable Solo on all the items in the list.")))
				.IsChecked(ECheckBoxState::Unchecked)
				.OnCheckStateChanged(this, &FMixerSourceDashboardViewFactory::ToggleSoloForAllItems)
				[
					GenerateToggleButtonLabelWidget(LOCTEXT("TableDashboardView_SoloButtonText", "S"), "SmallButtonText")
				]
			];
	}

	TSharedRef<FTabManager::FLayout> FMixerSourceDashboardViewFactory::LoadLayoutFromConfig()
	{
		return FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, GetDefaultTabLayout());
	}

	void FMixerSourceDashboardViewFactory::SaveLayoutToConfig()
	{
		if (MixerSourcesTabManager.IsValid())
		{
			FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, MixerSourcesTabManager->PersistLayout());
		}
	}
#endif // WITH_EDITOR

	TSharedRef<SWidget> FMixerSourceDashboardViewFactory::MakePlotsButtonWidget()
	{
		return SAssignNew(PlotsButton, SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckBox"))
			.OnCheckStateChanged(this, &FMixerSourceDashboardViewFactory::TogglePlotsTabVisibility)
			.ToolTipText(MixerSourcePrivate::PlotsIconDescription)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FSlateStyle::Get().GetBrush("AudioInsights.Icon.Sources.Plots"))
				]
			];
	}

	void FMixerSourceDashboardViewFactory::TogglePlotsTabVisibility(ECheckBoxState InCheckboxState)
	{
		using namespace MixerSourcePrivate;

		if (!MixerSourcesTabManager.IsValid())
		{
			return;
		}

		if (InCheckboxState == ECheckBoxState::Checked)
		{
			MixerSourcesTabManager->TryInvokeTab(MixerSourcesPlotsTabName);
		}
		else if (InCheckboxState == ECheckBoxState::Unchecked)
		{
			const TSharedPtr<SDockTab> PlotsTab = MixerSourcesTabManager->FindExistingLiveTab(MixerSourcesPlotsTabName);
			if (PlotsTab.IsValid())
			{
				PlotsTab->RequestCloseTab();
			}
		}

#if WITH_EDITOR
		SaveLayoutToConfig();
#endif // WITH_EDITOR
	}

	TSharedRef<SDockTab> FMixerSourceDashboardViewFactory::CreateMixerSourcesTab(const FSpawnTabArgs& Args)
	{
#if !WITH_EDITOR
		const TSharedPtr<const FAudioInsightsComponent> AudioInsightsComponent = FAudioInsightsModule::GetChecked().GetAudioInsightsComponent();
#endif // !WITH_EDITOR

		return SNew(SDockTab)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Fill)
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SNew(SHorizontalBox)
#if WITH_EDITOR
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					[
						MakeMuteSoloWidget()
					]
#endif // WITH_EDITOR
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					[
						SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					[
#if WITH_EDITOR
						MakePlotsButtonWidget()
#else
						AudioInsightsComponent.IsValid() && AudioInsightsComponent->GetIsLiveSession() ? MakePlotsButtonWidget() : SNullWidget::NullWidget
#endif // WITH_EDITOR
					]
				]
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				[
					FTraceTableDashboardViewFactory::MakeWidget()
				]
			];
	}

	TSharedRef<SDockTab> FMixerSourceDashboardViewFactory::CreatePlotsTab(const FSpawnTabArgs& Args)
	{
		if (PlotsButton.IsValid())
		{
			PlotsButton->SetIsChecked(ECheckBoxState::Checked);
		}

		return SNew(SDockTab)
			.Clipping(EWidgetClipping::ClipToBounds)
			.OnTabClosed_Lambda([this](TSharedRef<SDockTab> InDockTab)
			{				
				if (PlotsButton.IsValid())
				{
					PlotsButton->SetIsChecked(ECheckBoxState::Unchecked);
#if WITH_EDITOR
					// Can't save layout immediately (it won't save the tab closed state), needs to be done a bit later
					AsyncTask(ENamedThreads::GameThread, [this]()
					{
						SaveLayoutToConfig();
					});
#endif // WITH_EDITOR
				}
			})
			[
				PlotsWidget ? PlotsWidget.ToSharedRef() : SNullWidget::NullWidget
			];
	}

	void FMixerSourceDashboardViewFactory::RegisterTabSpawners()
	{
		using namespace MixerSourcePrivate;

		if (!MixerSourcesTabManager.IsValid())
		{
			return;
		}

		MixerSourcesTabManager->RegisterTabSpawner(MixerSourcesTableTabName, FOnSpawnTab::CreateSP(this, &FMixerSourceDashboardViewFactory::CreateMixerSourcesTab))
			.SetDisplayName(LOCTEXT("MixerSourceTab_MixerSourcesTable_Name", "Mixer Sources"))
			.SetGroup(MixerSourcesWorkspace.ToSharedRef())
			.SetMenuType(ETabSpawnerMenuType::Hidden);
#if !WITH_EDITOR
		const TSharedPtr<const FAudioInsightsComponent> AudioInsightsComponent = FAudioInsightsModule::GetChecked().GetAudioInsightsComponent();
		if (AudioInsightsComponent.IsValid() && AudioInsightsComponent->GetIsLiveSession())
#endif // !WITH_EDITOR
		{
			MixerSourcesTabManager->RegisterTabSpawner(MixerSourcesPlotsTabName, FOnSpawnTab::CreateSP(this, &FMixerSourceDashboardViewFactory::CreatePlotsTab))
				.SetDisplayName(LOCTEXT("MixerSourceTab_PlotsTab_Name", "Mixer Sources Plots"))
				.SetGroup(MixerSourcesWorkspace.ToSharedRef())
				.SetMenuType(ETabSpawnerMenuType::Hidden);
		}
	}

	void FMixerSourceDashboardViewFactory::UnregisterTabSpawners()
	{
		using namespace MixerSourcePrivate;
		
		if (MixerSourcesTabManager.IsValid())
		{
			MixerSourcesTabManager->UnregisterTabSpawner(MixerSourcesTableTabName);
			MixerSourcesTabManager->UnregisterTabSpawner(MixerSourcesPlotsTabName);
		}
	}

	TSharedRef<FTabManager::FLayout> FMixerSourceDashboardViewFactory::GetDefaultTabLayout()
	{
		using namespace MixerSourcePrivate;

		float SizeCoefficient = 0.7f;

#if !WITH_EDITOR
		const TSharedPtr<const FAudioInsightsComponent> AudioInsightsComponent = FAudioInsightsModule::GetChecked().GetAudioInsightsComponent();
		if (AudioInsightsComponent.IsValid() && !AudioInsightsComponent->GetIsLiveSession())
		{
			SizeCoefficient = 1.0f;
		}
#endif // !WITH_EDITOR

		const TSharedRef<FTabManager::FSplitter> TabSplitter = FTabManager::NewSplitter()
			->SetOrientation(Orient_Vertical)
			->SetSizeCoefficient(SizeCoefficient)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(SizeCoefficient)
				->SetHideTabWell(true)
				->AddTab(MixerSourcesTableTabName, ETabState::OpenedTab)
			);

#if !WITH_EDITOR
		if (AudioInsightsComponent.IsValid() && AudioInsightsComponent->GetIsLiveSession())
#endif // !WITH_EDITOR
		{
			TabSplitter->Split(FTabManager::NewStack()
				->SetSizeCoefficient(0.3f)
				->AddTab(MixerSourcesPlotsTabName, ETabState::OpenedTab)
				->SetHideTabWell(true));
		}

		return FTabManager::NewLayout("MixerSourceTabsLayout_v2")
			->AddArea
			(
				FTabManager::NewPrimaryArea()
				->SetOrientation(Orient_Vertical)
				->Split
				(
					TabSplitter
				)
			);
	}

	TSharedRef<SWidget> FMixerSourceDashboardViewFactory::MakeWidget()
	{
		using namespace MixerSourcePrivate;

#if WITH_EDITOR
		FEditorDelegates::PostPIEStarted.AddSP(this, &FMixerSourceDashboardViewFactory::OnPIEStarted);
		FEditorDelegates::EndPIE.AddSP(this, &FMixerSourceDashboardViewFactory::OnPIEStopped);
		FEditorDelegates::PausePIE.AddSP(this, &FMixerSourceDashboardViewFactory::OnPIEPaused);
		FEditorDelegates::ResumePIE.AddSP(this, &FMixerSourceDashboardViewFactory::OnPIEResumed);
#else
		FAudioInsightsComponent::OnTabSpawn.AddSP(this, &FMixerSourceDashboardViewFactory::OnAudioInsightsComponentTabSpawn);
#endif // WITH_EDITOR

		PlotsWidget = MakePlotsWidget();

		const TSharedRef<SDockTab> DockTab = SNew(SDockTab);
		MixerSourcesTabManager = FGlobalTabmanager::Get()->NewTabManager(DockTab);

#if WITH_EDITOR
		MixerSourcesTabManager->SetOnPersistLayout(FTabManager::FOnPersistLayout::CreateStatic([](const TSharedRef<FTabManager::FLayout>& InLayout)
		{
			if (InLayout->GetPrimaryArea().Pin().IsValid())
			{
				FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, InLayout);
			}
		}));
#endif // WITH_EDITOR

		DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([this](TSharedRef<SDockTab> TabClosed)
		{
			UnregisterTabSpawners();

#if WITH_EDITOR
			SaveLayoutToConfig();
#endif // WITH_EDITOR

			if (MixerSourcesTabManager.IsValid())
			{
				MixerSourcesTabManager->CloseAllAreas();

				MixerSourcesTabManager.Reset();
				MixerSourcesWorkspace.Reset();
			}
		}));

		
		MixerSourcesWorkspace = MixerSourcesTabManager->AddLocalWorkspaceMenuCategory(MixerSourcesWorkspaceName);

		RegisterTabSpawners();

#if WITH_EDITOR
		const TSharedRef<FTabManager::FLayout> TabLayout = LoadLayoutFromConfig();
#else
		const TSharedRef<FTabManager::FLayout> TabLayout = GetDefaultTabLayout();
#endif // WITH_EDITOR

		return MixerSourcesTabManager->RestoreFrom(TabLayout, TSharedPtr<SWindow>()).ToSharedRef();
	}	

	void FMixerSourceDashboardViewFactory::ProcessEntries(FTraceTableDashboardViewFactory::EProcessReason InReason)
	{
		const FString FilterString = GetSearchFilterText().ToString();
		FTraceTableDashboardViewFactory::FilterEntries<FMixerSourceTraceProvider>([&FilterString](const IDashboardDataViewEntry& Entry)
		{
			const FMixerSourceDashboardEntry& MixerSourceEntry = MixerSourcePrivate::CastEntry(Entry);
			if (MixerSourceEntry.GetDisplayName().ToString().Contains(FilterString))
			{
				return false;
			}

			return true;
		});

		UpdatePlotsWidgetsData();

#if WITH_EDITOR
		// Update the mute and solo states if the filter string changes
		if (CurrentFilterString != FilterString)
		{
			CurrentFilterString = FilterString;
			UpdateMuteSoloState();
		}
#endif // WITH_EDITOR
	}
} // namespace UE::Audio::Insights

#undef LOCTEXT_NAMESPACE
