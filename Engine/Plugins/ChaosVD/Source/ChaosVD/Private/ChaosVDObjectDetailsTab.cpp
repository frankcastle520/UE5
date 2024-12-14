// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDObjectDetailsTab.h"

#include "ChaosVDCollisionDataDetailsTab.h"
#include "ChaosVDParticleActor.h"
#include "ChaosVDScene.h"
#include "ChaosVDStyle.h"
#include "ChaosVDTabsIDs.h"
#include "Editor.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SChaosVDCollisionDataInspector.h"
#include "Widgets/SChaosVDDetailsView.h"
#include "Widgets/SChaosVDMainTab.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

void FChaosVDObjectDetailsTab::AddUnsupportedStruct(const UStruct* Struct)
{
	UnsupportedStructs.Add(Struct);
}

TSharedRef<SDockTab> FChaosVDObjectDetailsTab::HandleTabSpawnRequest(const FSpawnTabArgs& Args)
{

	TSharedRef<SDockTab> DetailsPanelTab =
	SNew(SDockTab)
	.TabRole(ETabRole::MajorTab)
	.Label(LOCTEXT("DetailsPanel", "Details"))
	.ToolTipText(LOCTEXT("DetailsPanelToolTip", "See the details of the selected object"));

	// The following types have their own data inspectors, we should not open them in the details pannel
	AddUnsupportedStruct(FChaosVDConstraintDataWrapperBase::StaticStruct());
	AddUnsupportedStruct(FChaosVDQueryDataWrapper::StaticStruct());
	AddUnsupportedStruct(FChaosVDParticlePairMidPhase::StaticStruct());

	if (const TSharedPtr<FChaosVDScene> ScenePtr = GetChaosVDScene().Pin())
	{
		RegisterSelectionSetObject(ScenePtr->GetElementSelectionSet());

		if (TSharedPtr<FChaosVDSolverDataSelection> SolverDataSelectionObject = ScenePtr->GetSolverDataSelectionObject().Pin())
		{
			SolverDataSelectionObject->GetDataSelectionChangedDelegate().AddSP(this, &FChaosVDObjectDetailsTab::HandleSolverDataSelectionChange);
		}

		if (const TSharedPtr<SChaosVDMainTab> MainTabPtr = OwningTabWidget.Pin())
		{
			DetailsPanelTab->SetContent
			(
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				[
					SAssignNew(DetailsPanelView, SChaosVDDetailsView, MainTabPtr.ToSharedRef())
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					GenerateShowCollisionDataButton().ToSharedRef()
				]
			);

			// If we closed the tab and opened it again with an object already selected, try to restore the selected object view
			if (DetailsPanelView.IsValid() && CurrentSelectedObject.IsValid())
			{
				DetailsPanelView->SetSelectedObject(CurrentSelectedObject.Get());
			}
		}
		else
		{
			DetailsPanelTab->SetContent(GenerateErrorWidget());
		}

	}
	else
	{
		DetailsPanelTab->SetContent(GenerateErrorWidget());
	}

	DetailsPanelTab->SetTabIcon(FChaosVDStyle::Get().GetBrush("TabIconDetailsPanel"));
	
	HandleTabSpawned(DetailsPanelTab);
	
	return DetailsPanelTab;
}

void FChaosVDObjectDetailsTab::HandleTabClosed(TSharedRef<SDockTab> InTabClosed)
{
	FChaosVDTabSpawnerBase::HandleTabClosed(InTabClosed);

	if (const TSharedPtr<FChaosVDScene> ScenePtr = GetChaosVDScene().Pin())
	{
		if (TSharedPtr<FChaosVDSolverDataSelection> SolverDataSelectionObject = ScenePtr->GetSolverDataSelectionObject().Pin())
		{
			SolverDataSelectionObject->GetDataSelectionChangedDelegate().RemoveAll(this);
		}
	}
	
	DetailsPanelView.Reset();
}

void FChaosVDObjectDetailsTab::HandleActorsSelection(TArrayView<AActor*> SelectedActors)
{
	if (SelectedActors.Num() > 0)
	{
		// We don't support multi selection yet
		ensure(SelectedActors.Num() == 1);

		CurrentSelectedObject = SelectedActors[0];

		if (DetailsPanelView)
		{
			DetailsPanelView->SetSelectedObject(CurrentSelectedObject.Get());
		}
	}
	else
	{
		CurrentSelectedObject = nullptr;
	}
}

void FChaosVDObjectDetailsTab::HandlePostSelectionChange(const UTypedElementSelectionSet* ChangedSelectionSet)
{
	TArray<AActor*> SelectedActors = ChangedSelectionSet->GetSelectedObjects<AActor>();

	HandleActorsSelection(SelectedActors);
}

void FChaosVDObjectDetailsTab::HandleSolverDataSelectionChange(const TSharedPtr<FChaosVDSolverDataSelectionHandle>& SelectionHandle)
{
	TSharedPtr<FStructOnScope> StructOnScope = SelectionHandle ? SelectionHandle->GetDataAsStructScope() : nullptr;
	if (!StructOnScope || !IsSupportedStruct(StructOnScope->GetStructPtr()))
	{
		DetailsPanelView->SetSelectedStruct(nullptr);
		return;
	}

	HandleActorsSelection(TArrayView<AActor*>());

	DetailsPanelView->SetSelectedStruct(SelectionHandle->GetCustomDataReadOnlyStructViewForDetails());
}

bool FChaosVDObjectDetailsTab::IsSupportedStruct(const TWeakObjectPtr<const UStruct>& InWeakStructPtr)
{
	return !UnsupportedStructs.Contains(InWeakStructPtr);
}

EVisibility FChaosVDObjectDetailsTab::GetCollisionDataButtonVisibility() const
{
	EVisibility DesiredVisibility = EVisibility::Collapsed;
	if (CurrentSelectedObject.IsValid())
	{
		if (Cast<IChaosVDCollisionDataProviderInterface>(CurrentSelectedObject))
		{
			DesiredVisibility = EVisibility::Visible;
		}
	}
	return DesiredVisibility;
}

bool FChaosVDObjectDetailsTab::GetCollisionDataButtonEnabled() const
{
	if (IChaosVDCollisionDataProviderInterface* CollisionDataProvider = Cast<IChaosVDCollisionDataProviderInterface>(CurrentSelectedObject.Get()))
	{
		return CollisionDataProvider->HasCollisionData();
	}

	return false;
}

TSharedPtr<SWidget> FChaosVDObjectDetailsTab::GenerateShowCollisionDataButton()
{
	TSharedPtr<SWidget> ShowCollisionButton = SNew(SHorizontalBox)
	.Visibility_Raw(this, &FChaosVDObjectDetailsTab::GetCollisionDataButtonVisibility)
	+SHorizontalBox::Slot()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Center)
	.Padding(12.0f, 7.0f, 12.0f, 7.0f)
	.FillWidth(1.0f)
	[
		SNew(SButton)
		.ToolTip(SNew(SToolTip).Text(LOCTEXT("OpenCollisionDataDesc", "Click here to open the collision data for this particle on the collision data inspector.")))
		.IsEnabled_Raw(this, &FChaosVDObjectDetailsTab::GetCollisionDataButtonEnabled)
		.ContentPadding(FMargin(0, 5.f, 0, 4.f))
		.OnClicked_Raw(this, &FChaosVDObjectDetailsTab::ShowCollisionDataForSelectedObject)
		.Content()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(FMargin(3, 0, 0, 0))
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.TextStyle(FAppStyle::Get(), "SmallButtonText")
				.Text(LOCTEXT("ShowCollisionDataOnInspector","Show Collison Data in Inspector"))
			]
		]
	];

	return ShowCollisionButton;
}

FReply FChaosVDObjectDetailsTab::ShowCollisionDataForSelectedObject()
{
	IChaosVDCollisionDataProviderInterface* CollisionDataProvider = Cast<IChaosVDCollisionDataProviderInterface>(CurrentSelectedObject.Get());
	if (!CollisionDataProvider)
	{
		return FReply::Handled();
	}
	
	TSharedPtr<SChaosVDMainTab> OwningTabPtr = OwningTabWidget.Pin();
	if (!OwningTabPtr.IsValid())
	{
		return FReply::Handled();
	}

	if (const TSharedPtr<FChaosVDCollisionDataDetailsTab> CollisionDataTab = OwningTabPtr->GetTabSpawnerInstance<FChaosVDCollisionDataDetailsTab>(FChaosVDTabID::CollisionDataDetails).Pin())
	{
		if (const TSharedPtr<FTabManager> TabManager = OwningTabPtr->GetTabManager())
		{
			TabManager->TryInvokeTab(FChaosVDTabID::CollisionDataDetails);

			if (const TSharedPtr<SChaosVDCollisionDataInspector> CollisionInspector = CollisionDataTab->GetCollisionInspectorInstance().Pin())
			{
				CollisionInspector->SetCollisionDataProviderObjectToInspect(CollisionDataProvider);
			}
		}
	}
	
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
