// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/CounterWidget.h"

#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Columns/TypedElementValueCacheColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Interfaces/IMainFrameModule.h"
#include "Layout/Margin.h"
#include "Misc/CoreDelegates.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TedsUI_CounterWidget"


FAutoConsoleCommand EnableCounterWidgetsConsoleCommand(
	TEXT("TEDS.UI.EnableCounterWidgets"),
	TEXT("Adds registered counter widgets to the bottom right status bar of the main editor window."),
	FConsoleCommandDelegate::CreateLambda([]()
		{
			UCounterWidgetFactory::EnableCounterWidgets();
		}));

//
// UCounterWidgetFactory
//

FName UCounterWidgetFactory::WigetPurpose(TEXT("LevelEditor.StatusBar.ToolBar"));
bool UCounterWidgetFactory::bAreCounterWidgetsEnabled{ false };
bool UCounterWidgetFactory::bHasBeenSetup{ false };

UCounterWidgetFactory::UCounterWidgetFactory()
{
	if (bAreCounterWidgetsEnabled)
	{
		IMainFrameModule::Get().OnMainFrameCreationFinished().AddStatic(&UCounterWidgetFactory::SetupMainWindowIntegrations);
	}
}

void UCounterWidgetFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	DataStorage.RegisterQuery(Select(TEXT("Sync counter widgets"), 
		FProcessor(EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncWidgets))
			.SetExecutionMode(EExecutionMode::GameThread),
		[](
			IQueryContext& Context,
			FTypedElementSlateWidgetReferenceColumn& Widget,
			FTypedElementU32IntValueCacheColumn& Comparison, 
			const FCounterWidgetColumn& Counter
		)
		{
			FQueryResult Result = Context.RunQuery(Counter.Query);
			if (Result.Completed == FQueryResult::ECompletion::Fully && Result.Count != Comparison.Value)
			{
				TSharedPtr<SWidget> WidgetPointer = Widget.Widget.Pin();
				checkf(WidgetPointer, TEXT("Referenced widget is not valid. A constructed widget may not have been cleaned up. This can "
					"also happen if this processor is running in the same phase as the processors responsible for cleaning up old "
					"references."));
				checkf(WidgetPointer->GetType() == STextBlock::StaticWidgetClass().GetWidgetType(),
					TEXT("Stored widget with FTypedElementCounterWidgetFragment doesn't match type %s, but was a %s."),
					*(STextBlock::StaticWidgetClass().GetWidgetType().ToString()),
					*(WidgetPointer->GetTypeAsString()));

				STextBlock* WidgetInstance = static_cast<STextBlock*>(WidgetPointer.Get());
				WidgetInstance->SetText(FText::Format(Counter.LabelTextFormatter, Result.Count));
				Comparison.Value = Result.Count;
			}
		})
	.Compile());
}

void UCounterWidgetFactory::RegisterWidgetPurposes(IEditorDataStorageUiProvider& DataStorageUi) const
{
	DataStorageUi.RegisterWidgetPurpose(WigetPurpose, IEditorDataStorageUiProvider::EPurposeType::Generic,
		LOCTEXT("ToolBarPurposeDescription", "Widgets added to the status bar at the bottom editor of the main editor window."));
}

void UCounterWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
	IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;

	TUniquePtr<FCounterWidgetConstructor> WidgetCounter = MakeUnique<FCounterWidgetConstructor>();
	WidgetCounter->LabelText = LOCTEXT("WidgetCounterStatusBarLabel", "{0} {0}|plural(one=Widget, other=Widgets)");
	WidgetCounter->ToolTipText = LOCTEXT(
		"WidgetCounterStatusBarToolTip",
		"The total number of widgets in the editor hosted through the Typed Element's Data Storage.");
	WidgetCounter->Query = DataStorage.RegisterQuery(
		Count().
		Where().
			All<FTypedElementSlateWidgetReferenceColumn>().
		Compile());
	DataStorageUi.RegisterWidgetFactory(WigetPurpose, MoveTemp(WidgetCounter));
}

void UCounterWidgetFactory::EnableCounterWidgets()
{
	bAreCounterWidgetsEnabled = true;
	UCounterWidgetFactory::SetupMainWindowIntegrations(nullptr, false);
}

void UCounterWidgetFactory::SetupMainWindowIntegrations(TSharedPtr<SWindow> ParentWindow, bool bIsRunningStartupDialog)
{
	if (!bHasBeenSetup)
	{
		using namespace UE::Editor::DataStorage;

		IEditorDataStorageUiProvider* UiInterface = GetMutableDataStorageFeature<IEditorDataStorageUiProvider>(UiFeatureName);
		checkf(UiInterface, TEXT(
			"FEditorDataStorageUiModule tried to integrate with the main window before the "
			"TEDS UI interface is available."));

		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(WigetPurpose);

		TArray<TSharedRef<SWidget>> Widgets;
		UiInterface->ConstructWidgets(WigetPurpose, {},
			[&Widgets](const TSharedRef<SWidget>& NewWidget, UE::Editor::DataStorage::RowHandle Row)
			{
				Widgets.Add(NewWidget);
			});

		if (!Widgets.IsEmpty())
		{
			FToolMenuSection& Section = Menu->AddSection("DataStorageSection");
			int32 WidgetCount = Widgets.Num();

			Section.AddEntry(FToolMenuEntry::InitWidget("DataStorageStatusBarWidget_0", MoveTemp(Widgets[0]), FText::GetEmpty()));
			for (int32 I = 1; I < WidgetCount; ++I)
			{
				Section.AddSeparator(FName(*FString::Format(TEXT("DataStorageStatusBarWidgetDivider_{0}"), { FString::FromInt(I) })));
				Section.AddEntry(FToolMenuEntry::InitWidget(
					FName(*FString::Format(TEXT("DataStorageStatusBarWidget_{0}"), { FString::FromInt(I) })),
					MoveTemp(Widgets[I]), FText::GetEmpty()));
			}
		}
		bHasBeenSetup = true;
	}
}



//
// FCounterWidgetConstructor
//

FCounterWidgetConstructor::FCounterWidgetConstructor()
	: Super(FCounterWidgetConstructor::StaticStruct())
{
}

TConstArrayView<const UScriptStruct*> FCounterWidgetConstructor::GetAdditionalColumnsList() const
{
	static TTypedElementColumnTypeList<FCounterWidgetColumn, FTypedElementU32IntValueCacheColumn> Columns;
	return Columns;
}

TSharedPtr<SWidget> FCounterWidgetConstructor::CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return SNew(STextBlock)
		.Text(FText::Format(LabelText, 0))
		.Margin(FMargin(4.0f, 0.0f))
		.ToolTipText(ToolTipText)
		.Justification(ETextJustify::Center);
}

bool FCounterWidgetConstructor::SetColumns(IEditorDataStorageProvider* DataStorage, UE::Editor::DataStorage::RowHandle Row)
{
	FCounterWidgetColumn* CounterColumn = DataStorage->GetColumn<FCounterWidgetColumn>(Row);
	checkf(CounterColumn, TEXT("Added a new FCounterWidgetColumn to the Typed Elements Data Storage, but didn't get a valid pointer back."));
	CounterColumn->LabelTextFormatter = LabelText;
	CounterColumn->Query = Query;

	FTypedElementU32IntValueCacheColumn* CacheColumn = DataStorage->GetColumn<FTypedElementU32IntValueCacheColumn>(Row);
	checkf(CacheColumn, TEXT("Added a new FTypedElementUnsigned32BitIntValueCache to the Typed Elements Data Storage, but didn't get a valid pointer back."));
	CacheColumn->Value = 0;

	return true;
}

#undef LOCTEXT_NAMESPACE