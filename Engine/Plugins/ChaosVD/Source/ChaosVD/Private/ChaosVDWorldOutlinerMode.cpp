// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDWorldOutlinerMode.h"

#include "ActorTreeItem.h"
#include "ChaosVDModule.h"
#include "ChaosVDParticleActor.h"
#include "ChaosVDPlaybackController.h"
#include "ChaosVDScene.h"
#include "Elements/Framework/TypedElementSelectionSet.h"

namespace Chaos::VisualDebugger::Cvars
{
static bool bQueueAndCombineSceneOutlinerEvent = true;
static FAutoConsoleVariableRef CVarChaosVDQueueAndCombineSceneOutlinerEvents(
	TEXT("p.Chaos.VD.Tool.QueueAndCombineSceneOutlinerEvents"),
	bQueueAndCombineSceneOutlinerEvent,
	TEXT("If set to true, scene outliner events will be queued and sent once per frame. If there was a unprocessed event for an item, the las queued event will replace it"));

static bool bPurgeInvalidOutlinerItemsBeforeBroadcast = true;
static FAutoConsoleVariableRef CVarChaosVDPurgeInvalidOutlinerItemsBeforeBroadcast(
	TEXT("p.Chaos.VD.Tool.PurgeInvalidOutlinerItemsBeforeBroadcast"),
	bPurgeInvalidOutlinerItemsBeforeBroadcast,
	TEXT("If set to true, scene outliner events will evaluated and any invalid outliner event in them will be removed before broadcasting the hierarchy change."));
}

const FSceneOutlinerTreeItemType FChaosVDActorTreeItem::Type(&FActorTreeItem::Type);

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

const TSharedRef<SWidget> FChaosVDSceneOutlinerGutter::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (TreeItem->ShouldShowVisibilityState())
	{
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SVisibilityWidget, SharedThis(this), WeakOutliner, TreeItem, &Row)
				.IsEnabled_Raw(this, &FChaosVDSceneOutlinerGutter::IsEnabled, TreeItem->AsWeak())
				.ToolTipText_Raw(this, &FChaosVDSceneOutlinerGutter::GetVisibilityTooltip, TreeItem->AsWeak())
			];
	}
	return SNullWidget::NullWidget;
}

FText FChaosVDSceneOutlinerGutter::GetVisibilityTooltip(TWeakPtr<ISceneOutlinerTreeItem> WeakTreeItem) const
{

	return IsEnabled(WeakTreeItem) ? LOCTEXT("SceneOutlinerVisibilityToggleTooltip", "Toggles the visibility of this object in the level editor.") :
										LOCTEXT("SceneOutlinerVisibilityToggleDisabkedTooltip", "Visibility of this object is being controlled by another visibility setting");
}

bool FChaosVDSceneOutlinerGutter::IsEnabled(TWeakPtr<ISceneOutlinerTreeItem> WeakTreeItem) const
{
	bool bIsEnabled = true;
	if (const TSharedPtr<ISceneOutlinerTreeItem> TreeItem = WeakTreeItem.Pin())
	{
		if (const FActorTreeItem* ActorItem = TreeItem->CastTo<FActorTreeItem>())
		{
			if (const AChaosVDParticleActor* CVDActor = Cast<AChaosVDParticleActor>(ActorItem->Actor.Get()))
			{
				bIsEnabled = CVDActor->GetHideFlags() == EChaosVDHideParticleFlags::HiddenBySceneOutliner || CVDActor->GetHideFlags() == EChaosVDHideParticleFlags::None;
			}
		}
	}

	return bIsEnabled;
}

bool FChaosVDActorTreeItem::GetVisibility() const
{
	if (const AChaosVDParticleActor* CVDActor = Cast<AChaosVDParticleActor>(Actor.Get()))
	{
		return CVDActor->IsVisible();
	}
	else
	{
		return FActorTreeItem::GetVisibility();
	}
}

void FChaosVDActorTreeItem::OnVisibilityChanged(const bool bNewVisibility)
{
	if (AChaosVDParticleActor* CVDActor = Cast<AChaosVDParticleActor>(Actor.Get()))
	{
		if (bNewVisibility)
		{
			CVDActor->RemoveHiddenFlag(EChaosVDHideParticleFlags::HiddenBySceneOutliner);
		}
		else
		{
			CVDActor->AddHiddenFlag(EChaosVDHideParticleFlags::HiddenBySceneOutliner);
		}
	}
	else
	{
		FActorTreeItem::OnVisibilityChanged(bNewVisibility);
	}
}

void FChaosVDActorTreeItem::UpdateDisplayString()
{
	if (AChaosVDParticleActor* CVDActor = Cast<AChaosVDParticleActor>(Actor.Get()))
	{
		if (TSharedPtr<const FChaosVDParticleDataWrapper> ParticleData = CVDActor->GetParticleData())
		{
			const bool bHasDebugName = !ParticleData->DebugName.IsEmpty();
			DisplayString = bHasDebugName ? ParticleData->DebugName : TEXT("Unnamed Particle - ID : ") + FString::FromInt(ParticleData->ParticleIndex);
		}
	}
	else
	{
		FActorTreeItem::UpdateDisplayString();
	}
}

TUniquePtr<FChaosVDOutlinerHierarchy> FChaosVDOutlinerHierarchy::Create(ISceneOutlinerMode* Mode, const TWeakObjectPtr<UWorld>& World)
{
	FChaosVDOutlinerHierarchy* Hierarchy = new FChaosVDOutlinerHierarchy(Mode, World);

	Create_Internal(Hierarchy, World);

	return TUniquePtr<FChaosVDOutlinerHierarchy>(Hierarchy);
}


FSceneOutlinerTreeItemPtr FChaosVDOutlinerHierarchy::CreateItemForActor(AActor* InActor, bool bForce) const
{
	return Mode->CreateItemFor<FChaosVDActorTreeItem>(InActor, bForce);
}

FChaosVDWorldOutlinerMode::FChaosVDWorldOutlinerMode(const FActorModeParams& InModeParams, TWeakPtr<FChaosVDScene> InScene, TWeakPtr<FChaosVDPlaybackController> InPlaybackController)
	: FActorMode(InModeParams),
	CVDScene(InScene),
	PlaybackController(InPlaybackController)
{
	TSharedPtr<FChaosVDScene> ScenePtr = CVDScene.Pin();
	if (!ensure(ScenePtr.IsValid()))
	{
		return;
	}

	ScenePtr->OnActorActiveStateChanged().AddRaw(this, &FChaosVDWorldOutlinerMode::HandleActorActiveStateChanged);
	ScenePtr->OnActorLabelChanged().AddRaw(this, &FChaosVDWorldOutlinerMode::HandleActorLabelChanged);

	RegisterSelectionSetObject(ScenePtr->GetElementSelectionSet());
}

FChaosVDWorldOutlinerMode::~FChaosVDWorldOutlinerMode()
{
	if (TSharedPtr<FChaosVDScene> ScenePtr = CVDScene.Pin())
	{
		ScenePtr->OnActorActiveStateChanged().RemoveAll(this);
		ScenePtr->OnActorLabelChanged().RemoveAll(this);
	}
}

void FChaosVDWorldOutlinerMode::OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}

	TSharedPtr<FChaosVDScene> ScenePtr = CVDScene.Pin();
	if (!ScenePtr.IsValid())
	{
		return;
	}

	TArray<AActor*> OutlinerSelectedActors = Selection.GetData<AActor*>(SceneOutliner::FActorSelector());

	if (const UTypedElementSelectionSet* SelectionSet = GetSelectionSetObject())
	{
		TArray<AActor*> SceneSelectedActors = SelectionSet->GetSelectedObjects<AActor>();
		ensureMsgf(SceneSelectedActors.Num() < 2, TEXT("Multi Selection is not supported, but [%d] Actors are selected... Choosing the first one"), SceneSelectedActors.Num());

		AActor* SelectedActor = OutlinerSelectedActors.IsEmpty() ? nullptr : OutlinerSelectedActors[0];

		// If the actor ptr is null, the selection will be cleared
		ScenePtr->SetSelectedObject(SelectedActor);
	}
}

void FChaosVDWorldOutlinerMode::OnItemDoubleClick(FSceneOutlinerTreeItemPtr Item)
{
	TSharedPtr<FChaosVDScene> ScenePtr = CVDScene.Pin();
	if (!ScenePtr.IsValid())
	{
		return;
	}

	if (const FActorTreeItem* ActorItem = Item->CastTo<FActorTreeItem>())
	{
		if (AActor* Actor = ActorItem->Actor.Get())
		{
			ScenePtr->OnFocusRequest().Broadcast(Actor->GetComponentsBoundingBox(false));
		}
	}
}

void FChaosVDWorldOutlinerMode::ProcessPendingHierarchyEvents()
{
	const double StartTimeSeconds = FPlatformTime::Seconds();
	double CurrentTimeSpentSeconds = 0.0;
	int32 CurrentEventProcessedNum = 0;

	constexpr double MaxUpdateBudgetSeconds = 0.002;

	for (TMap<FSceneOutlinerTreeItemID, FSceneOutlinerHierarchyChangedData>::TIterator RemoveIterator = PendingOutlinerEventsMap.CreateIterator(); RemoveIterator; ++RemoveIterator)
	{
		if (CurrentTimeSpentSeconds > MaxUpdateBudgetSeconds)
		{
			return;
		}

		// Only check the budget every 5 tasks as Getting the current time is a syscall and it is not free
		if (CurrentEventProcessedNum % 5 == 0)
		{
			CurrentTimeSpentSeconds += FPlatformTime::Seconds() - StartTimeSeconds;
		}

		FSceneOutlinerHierarchyChangedData& HierarchyChangedData = RemoveIterator->Value;

		if (Chaos::VisualDebugger::Cvars::bPurgeInvalidOutlinerItemsBeforeBroadcast && HierarchyChangedData.Type == FSceneOutlinerHierarchyChangedData::Added)
		{ 
			HierarchyChangedData.Items.RemoveAllSwap([](const FSceneOutlinerTreeItemPtr& SceneOutlinerItemPtr)
			{
				return !SceneOutlinerItemPtr || !SceneOutlinerItemPtr->IsValid();
			});
		}

		Hierarchy->OnHierarchyChanged().Broadcast(HierarchyChangedData);
		RemoveIterator.RemoveCurrent();
		CurrentEventProcessedNum++;
	}
}

bool FChaosVDWorldOutlinerMode::Tick(float DeltaTime)
{
	ProcessPendingHierarchyEvents();
	return true;
}

TUniquePtr<ISceneOutlinerHierarchy> FChaosVDWorldOutlinerMode::CreateHierarchy()
{
	TUniquePtr<FChaosVDOutlinerHierarchy> ActorHierarchy = FChaosVDOutlinerHierarchy::Create(this, RepresentingWorld);

	ActorHierarchy->SetShowingComponents(!bHideComponents);
	ActorHierarchy->SetShowingOnlyActorWithValidComponents(!bHideComponents && bHideActorWithNoComponent);
	ActorHierarchy->SetShowingLevelInstances(!bHideLevelInstanceHierarchy);
	ActorHierarchy->SetShowingUnloadedActors(!bHideUnloadedActors);
	ActorHierarchy->SetShowingEmptyFolders(!bHideEmptyFolders);
	
	return ActorHierarchy;
}

bool FChaosVDWorldOutlinerMode::CanInteract(const ISceneOutlinerTreeItem& Item) const
{
	// This option is not supported in CVD yet
	ensure(!bCanInteractWithSelectableActorsOnly);

	return true;
}

bool FChaosVDWorldOutlinerMode::CanPopulate() const
{
	if (TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		// Updating the scene outliner during playback it is very expensive and can tank framerate,
		// as it need to re-build the hierarchy when things are added in ad removed. So if we are playing we want to pause any updates to the outliner
		return !PlaybackControllerPtr->IsPlaying();
	}
	return true;
}

void FChaosVDWorldOutlinerMode::EnqueueAndCombineHierarchyEvent(const FSceneOutlinerTreeItemID& ItemID, const FSceneOutlinerHierarchyChangedData& EnventToProcess)
{
	if (FSceneOutlinerHierarchyChangedData* EventData = PendingOutlinerEventsMap.Find(ItemID))
	{
		*EventData = EnventToProcess;
	}
	else
	{
		PendingOutlinerEventsMap.Add(ItemID, EnventToProcess);
	}	
}
void FChaosVDWorldOutlinerMode::HandleActorLabelChanged(AChaosVDParticleActor* ChangedActor)
{
	if (!ensure(ChangedActor))
	{
		return;
	}

	if (IsActorDisplayable(ChangedActor) && RepresentingWorld.Get() == ChangedActor->GetWorld())
	{
		// Force create the item otherwise the outliner may not be notified of a change to the item if it is filtered out
		if (FSceneOutlinerTreeItemPtr Item = CreateItemFor<FChaosVDActorTreeItem>(ChangedActor, true))
		{
			SceneOutliner->OnItemLabelChanged(Item);
		}
	}
}

void FChaosVDWorldOutlinerMode::HandleActorActiveStateChanged(AChaosVDParticleActor* ChangedActor)
{
	if (!ChangedActor)
	{
		return;
	}

	if (Hierarchy.IsValid())
	{
		FSceneOutlinerHierarchyChangedData EventData;

		if (ChangedActor->IsActive())
		{
			EventData.Type = FSceneOutlinerHierarchyChangedData::Added;
			EventData.Items.Emplace(CreateItemFor<FChaosVDActorTreeItem>(ChangedActor));
		}
		else
		{
			EventData.ItemIDs.Emplace(ChangedActor);
			EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;
		}

		// There is currently a bug in the Scene Outliner where if opposite events happen multiple times within the same tick, the last ones get dropped
		// (UE-193877). As our current use case is fairly simple, as a workaround we can just queue the events and process them once per frame
		// Only taking into account only the last requested event for each item.
		// Keeping this behind a cvar enabled by default so when the Scene Outliner bug is fixed, we can test it easily.
		if (Chaos::VisualDebugger::Cvars::bQueueAndCombineSceneOutlinerEvent)
		{
			EnqueueAndCombineHierarchyEvent(ChangedActor, EventData);
		}
		else
		{
			Hierarchy->OnHierarchyChanged().Broadcast(EventData);
		}
	}
}

void FChaosVDWorldOutlinerMode::HandlePostSelectionChange(const UTypedElementSelectionSet* ChangesSelectionSet)
{
	TArray<AActor*> SelectedActors = ChangesSelectionSet->GetSelectedObjects<AActor>();

	if (SelectedActors.Num() > 0)
	{
		// We don't support multi selection yet
		ensure(SelectedActors.Num() == 1);
		AActor* SelectedActor = SelectedActors[0];
		
		if (FSceneOutlinerTreeItemPtr TreeItem = SceneOutliner->GetTreeItem(SelectedActor, false))
		{
			SceneOutliner->ScrollItemIntoView(TreeItem);
			SceneOutliner->SetItemSelection(TreeItem, true, ESelectInfo::Direct);
		}
		else
		{
			UE_LOG(LogChaosVDEditor, Verbose, TEXT("Selected actor is not in the outliner. It might be filtered out"))	
		}
	}
}

#undef LOCTEXT_NAMESPACE
