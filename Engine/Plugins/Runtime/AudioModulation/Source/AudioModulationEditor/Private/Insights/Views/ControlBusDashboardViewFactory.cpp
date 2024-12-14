// Copyright Epic Games, Inc. All Rights Reserved.
#include "ControlBusDashboardViewFactory.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AudioDefines.h"
#include "AudioDeviceManager.h"
#include "IAudioInsightsEditorModule.h"
#include "AudioInsightsStyle.h"
#include "AudioInsightsTraceModule.h"
#include "AudioInsightsTraceProviderBase.h"
#include "Editor.h"
#include "Internationalization/Text.h"
#include "Insights/Providers/ControlBusTraceProvider.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Templates/SharedPointer.h"

#define LOCTEXT_NAMESPACE "AudioModulationInsights"

namespace AudioModulationEditor
{
	namespace ControlBusPrivate
	{
		const FControlBusDashboardEntry& CastEntry(const UE::Audio::Insights::IDashboardDataViewEntry& InData)
		{
			return static_cast<const FControlBusDashboardEntry&>(InData);
		};

	} // namespace ControlBusPrivate

	FControlBusDashboardViewFactory::FControlBusDashboardViewFactory()
	{
		IAudioInsightsEditorModule& InsightsModule = IAudioInsightsEditorModule::GetChecked();
		IAudioInsightsTraceModule& InsightsTraceModule = InsightsModule.GetTraceModule();

		TSharedPtr<FControlBusTraceProvider> ControlBusProvider = MakeShared<FControlBusTraceProvider>(InsightsTraceModule.GetChannelManager());
		InsightsTraceModule.AddTraceProvider(StaticCastSharedPtr<UE::Audio::Insights::FTraceProviderBase>(ControlBusProvider));

		Providers =
		{
			StaticCastSharedPtr<UE::Audio::Insights::FTraceProviderBase>(ControlBusProvider)
		};
	}

	FControlBusDashboardViewFactory::~FControlBusDashboardViewFactory()
	{
	}

	FName FControlBusDashboardViewFactory::GetName() const
	{
		return "ControlBuses";
	}

	FText FControlBusDashboardViewFactory::GetDisplayName() const
	{
		return LOCTEXT("AudioInsights_ModulationControlBus_DisplayName", "Control Buses");
	}

	void FControlBusDashboardViewFactory::ProcessEntries(UE::Audio::Insights::FTraceTableDashboardViewFactory::EProcessReason Reason)
	{
		using namespace UE::Audio::Insights;

		const FString FilterString = GetSearchFilterText().ToString();
		FTraceTableDashboardViewFactory::FilterEntries<FControlBusTraceProvider>([&FilterString](const IDashboardDataViewEntry& Entry)
		{
			const FControlBusDashboardEntry& ControlBusEntry = ControlBusPrivate::CastEntry(Entry);
			if (ControlBusEntry.GetDisplayName().ToString().Contains(FilterString))
			{
				return false;
			}

			return true;
		});
	}

	FSlateIcon FControlBusDashboardViewFactory::GetIcon() const
	{
		return { "AudioModulationStyle", "ClassIcon.SoundControlBus" };
	}

	UE::Audio::Insights::EDefaultDashboardTabStack FControlBusDashboardViewFactory::GetDefaultTabStack() const
	{
		return UE::Audio::Insights::EDefaultDashboardTabStack::Analysis;
	}

	TSharedRef<SWidget> FControlBusDashboardViewFactory::MakeWidget()
	{
		TSharedRef<SWidget> TableDashboardWidget = UE::Audio::Insights::FTraceTableDashboardViewFactory::MakeWidget();
		TSharedRef<SWidget> BusWatchWidget = MakeControlBusWatchWidget();

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				// Dashboard and plots area
				SNew(SSplitter)
					.Orientation(Orient_Horizontal)
					+ SSplitter::Slot()
					.Value(0.55f)
					[
						TableDashboardWidget
					]
					+ SSplitter::Slot()
					.Value(0.45f)
					[
						BusWatchWidget
					]
			];
	}

	const TMap<FName, UE::Audio::Insights::FTraceTableDashboardViewFactory::FColumnData>& FControlBusDashboardViewFactory::GetColumns() const
	{
		auto CreateColumnData = []()
		{
			using namespace UE::Audio::Insights;

			return TMap<FName, FTraceTableDashboardViewFactory::FColumnData>
			{
				{
					"BusId",
					{
						LOCTEXT("ControlBus_BusIdColumnDisplayName", "Control Bus ID"),
						[](const IDashboardDataViewEntry& InData) { return FText::AsNumber(ControlBusPrivate::CastEntry(InData).ControlBusId); },
						true /* bDefaultHidden */,
						0.08f /* FillWidth */
					}
				},
				{
					"Name",
					{
						LOCTEXT("ControlBus_NameColumnDisplayName", "Name"),
						[](const IDashboardDataViewEntry& InData) { return FText::FromString(ControlBusPrivate::CastEntry(InData).Name); },
						false /* bDefaultHidden */,
						0.75f /* FillWidth */
					}
				},
				{
					"ParameterName",
					{
						LOCTEXT("ControlBus_ParamNameColumnDisplayName", "Parameter"),
						[](const IDashboardDataViewEntry& InData) { return FText::FromString(ControlBusPrivate::CastEntry(InData).ParamName); },
						false /* bDefaultHidden */,
						0.15f /* FillWidth */
					}
				},
				{
					"Value",
					{
						LOCTEXT("ControlBus_ValueColumnDisplayName", "Value"),
						[](const IDashboardDataViewEntry& InData) { return FText::AsNumber(ControlBusPrivate::CastEntry(InData).Value, FSlateStyle::Get().GetAmpFloatFormat()); },
						false /* bDefaultHidden */,
						0.07f /* FillWidth */
					}
				}
			};
		};
		static const TMap<FName, FTraceTableDashboardViewFactory::FColumnData> ColumnData = CreateColumnData();
		return ColumnData;
	}

	void FControlBusDashboardViewFactory::SortTable()
	{
		using namespace UE::Audio::Insights;

		if (SortByColumn == "Name")
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FControlBusDashboardEntry& AData = ControlBusPrivate::CastEntry(*A.Get());
					const FControlBusDashboardEntry& BData = ControlBusPrivate::CastEntry(*B.Get());

					return AData.GetDisplayName().CompareToCaseIgnored(BData.GetDisplayName()) < 0;
				});
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FControlBusDashboardEntry& AData = ControlBusPrivate::CastEntry(*A.Get());
					const FControlBusDashboardEntry& BData = ControlBusPrivate::CastEntry(*B.Get());

					return BData.GetDisplayName().CompareToCaseIgnored(AData.GetDisplayName()) < 0;
				});
			}
		}
		else if (SortByColumn == "ParamName")
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FControlBusDashboardEntry& AData = ControlBusPrivate::CastEntry(*A.Get());
					const FControlBusDashboardEntry& BData = ControlBusPrivate::CastEntry(*B.Get());

					return AData.GetParamNameAsFText().CompareToCaseIgnored(BData.GetParamNameAsFText()) < 0;
				});
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				DataViewEntries.Sort([](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
				{
					const FControlBusDashboardEntry& AData = ControlBusPrivate::CastEntry(*A.Get());
					const FControlBusDashboardEntry& BData = ControlBusPrivate::CastEntry(*B.Get());

					return BData.GetParamNameAsFText().CompareToCaseIgnored(AData.GetParamNameAsFText()) < 0;
				});
			}
		}
	}

	TSharedRef<SWidget> FControlBusDashboardViewFactory::MakeControlBusListWidget()
	{
		return SNew(SBox);
	}

	TSharedRef<SWidget> FControlBusDashboardViewFactory::MakeControlBusWatchWidget()
	{
		return SNew(SBox);
	}

} // namespace AudioModulationEditor

#undef LOCTEXT_NAMESPACE
