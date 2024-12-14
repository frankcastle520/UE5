// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorViewportSelectability.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "Math/Color.h"
#include "ScopedTransaction.h"
#include "Selection.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "UnrealWidget.h"
#include "UObject/WeakObjectPtr.h"

#define LOCTEXT_NAMESPACE "SequencerSelectabilityTool"

const FText FEditorViewportSelectability::DefaultLimitedSelectionText = LOCTEXT("DefaultSelectionLimitedHelp", "Viewport Selection Limited");

FEditorViewportSelectability::FEditorViewportSelectability(const FOnGetWorld& InOnGetWorld, const FOnIsObjectSelectableInViewport& InOnIsObjectSelectableInViewport)
	: OnGetWorld(InOnGetWorld)
	, OnIsObjectSelectableInViewportDelegate(InOnIsObjectSelectableInViewport)
{
}

void FEditorViewportSelectability::EnableLimitedSelection(const bool bInEnabled)
{
	bSelectionLimited = bInEnabled;

	if (bSelectionLimited)
	{
		DeselectNonSelectableActors();
	}

	UpdateSelectionLimitedVisuals(!bInEnabled);
}

bool FEditorViewportSelectability::IsObjectSelectableInViewport(UObject* const InObject) const
{
	if (OnIsObjectSelectableInViewportDelegate.IsBound())
	{
		return OnIsObjectSelectableInViewportDelegate.Execute(InObject);
	}
	return true;
}

void FEditorViewportSelectability::UpdatePrimitiveVisuals(const bool bInSelectedLimited, UPrimitiveComponent* const InPrimitive, const TOptional<FColor>& InColor)
{
	if (bInSelectedLimited && InColor.IsSet())
	{
		// @TODO: Need to resolve rendering issue before this can be used
		//InPrimitive->SetOverlayColor(InColor.GetValue());
		InPrimitive->PushHoveredToProxy(true);
	}
	else
	{
		// @TODO: Need to resolve rendering issue before this can be used
		//InPrimitive->RemoveOverlayColor();
		InPrimitive->PushHoveredToProxy(false);
	}
}

bool FEditorViewportSelectability::UpdateHoveredPrimitive(const bool bInSelectedLimited
	, UPrimitiveComponent* const InPrimitiveComponent
	, TMap<TWeakObjectPtr<UPrimitiveComponent>, TOptional<FColor>>& InOutHoveredPrimitiveComponents
	, const TFunctionRef<bool(UObject*)>& InSelectablePredicate)
{
	bool bValid = IsValid(InPrimitiveComponent);

	// Save the current overlay color to restore when unhovered
	TMap<UPrimitiveComponent*, TOptional<FColor>> PrimitiveComponentsToAdd;

	if (bValid && bInSelectedLimited && InSelectablePredicate(InPrimitiveComponent))
	{
		TOptional<FColor> UnhoveredColor;
		if (InPrimitiveComponent->bWantsEditorEffects)
		{
			UnhoveredColor = InPrimitiveComponent->OverlayColor;
		}
		PrimitiveComponentsToAdd.Add(const_cast<UPrimitiveComponent*>(InPrimitiveComponent), UnhoveredColor);

		bValid = true;
	}

	// Get the set of components to remove that aren't in the newly hovered set from the currently hovered compenents
	TMap<UPrimitiveComponent*, TOptional<FColor>> PrimitiveComponentsToRemove;
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, TOptional<FColor>>& HoveredPair : InOutHoveredPrimitiveComponents)
	{
		UPrimitiveComponent* const PrimitiveComponent = HoveredPair.Key.Get();
		if (IsValid(PrimitiveComponent) && !PrimitiveComponentsToAdd.Contains(PrimitiveComponent))
		{
			PrimitiveComponentsToRemove.Add(PrimitiveComponent, HoveredPair.Value);
		}
	}

	// Hover new primitives, unhover old primitives
	InOutHoveredPrimitiveComponents.Empty();

	for (const TPair<UPrimitiveComponent*, TOptional<FColor>>& AddPair : PrimitiveComponentsToAdd)
	{
		InOutHoveredPrimitiveComponents.Add(AddPair);

		// Using white becaue we've stripped out the visual element until the rendering issue can be resolved
		UpdatePrimitiveVisuals(bInSelectedLimited, AddPair.Key, FColor::White);
	}

	for (const TPair<UPrimitiveComponent*, TOptional<FColor>>& RemovePair : PrimitiveComponentsToRemove)
	{
		UpdatePrimitiveVisuals(bInSelectedLimited, RemovePair.Key);
	}

	return bValid;
}

bool FEditorViewportSelectability::UpdateHoveredActorPrimitives(const bool bInSelectedLimited
	, AActor* const InActor
	, TMap<TWeakObjectPtr<UPrimitiveComponent>, TOptional<FColor>>& InOutHoveredPrimitiveComponents
	, const TFunctionRef<bool(UObject*)>& InSelectablePredicate)
{
	bool bValid = false;

	// Save the current overlay color to restore when unhovered
	TMap<UPrimitiveComponent*, TOptional<FColor>> PrimitiveComponentsToAdd;

	if (IsValid(InActor) && bInSelectedLimited)
	{
		if (InSelectablePredicate(InActor))
		{
			bValid = true;
		}
		InActor->ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors=*/true,
			[&InSelectablePredicate, &bValid, &PrimitiveComponentsToAdd](UPrimitiveComponent* const InPrimitiveComponent)
			{
				if (bValid || InSelectablePredicate(InPrimitiveComponent))
				{
					TOptional<FColor> UnhoveredColor;
					if (InPrimitiveComponent->bWantsEditorEffects)
					{
						UnhoveredColor = InPrimitiveComponent->OverlayColor;
					}
					PrimitiveComponentsToAdd.Add(InPrimitiveComponent, UnhoveredColor);

					bValid = true;
				}
			});
	}

	// Get the set of components to remove that aren't in the newly hovered set from the currently hovered compenents
	TMap<UPrimitiveComponent*, TOptional<FColor>> PrimitiveComponentsToRemove;
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, TOptional<FColor>>& HoveredPair : InOutHoveredPrimitiveComponents)
	{
		UPrimitiveComponent* const PrimitiveComponent = HoveredPair.Key.Get();
		if (IsValid(PrimitiveComponent) && !PrimitiveComponentsToAdd.Contains(PrimitiveComponent))
		{
			PrimitiveComponentsToRemove.Add(PrimitiveComponent, HoveredPair.Value);
		}
	}

	// Hover new primitives, unhover old primitives
	InOutHoveredPrimitiveComponents.Empty();

	for (const TPair<UPrimitiveComponent*, TOptional<FColor>>& AddPair : PrimitiveComponentsToAdd)
	{
		InOutHoveredPrimitiveComponents.Add(AddPair);

		// Using white becaue we've stripped out the visual element until the rendering issue can be resolved
		UpdatePrimitiveVisuals(bInSelectedLimited, AddPair.Key, FColor::White);
	}

	for (const TPair<UPrimitiveComponent*, TOptional<FColor>>& RemovePair : PrimitiveComponentsToRemove)
	{
		UpdatePrimitiveVisuals(bInSelectedLimited, RemovePair.Key);
	}

	return bValid;
}

void FEditorViewportSelectability::UpdateHoveredActorPrimitives(AActor* const InActor)
{
	UpdateHoveredActorPrimitives(bSelectionLimited, InActor, HoveredPrimitiveComponents,
		[this](UObject* const InObject) -> bool
		{
			return IsObjectSelectableInViewport(InObject);
		});
}

void FEditorViewportSelectability::UpdateSelectionLimitedVisuals(const bool bInClearHovered)
{
	if (bInClearHovered)
	{
		UpdateHoveredActorPrimitives(nullptr);
	}

	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, TOptional<FColor>>& HoveredPair : HoveredPrimitiveComponents)
	{
		UPrimitiveComponent* const PrimitiveComponent = HoveredPair.Key.Get();
		if (IsValid(PrimitiveComponent))
		{
			if (bSelectionLimited
				&& (IsObjectSelectableInViewport(PrimitiveComponent) || IsObjectSelectableInViewport(PrimitiveComponent->GetOwner())))
			{
				UpdatePrimitiveVisuals(bSelectionLimited, PrimitiveComponent, HoveredPair.Value);
			}
			else
			{
				UpdatePrimitiveVisuals(bSelectionLimited, PrimitiveComponent);
			}
		}
	}
}

void FEditorViewportSelectability::DeselectNonSelectableActors()
{
	if (!bSelectionLimited)
	{
		return;
	}

	USelection* const ActorSelection = GEditor ? GEditor->GetSelectedActors() : nullptr;
	if (!ActorSelection || ActorSelection->Num() == 0)
	{
		return;
	}

	TArray<AActor*> SelectedActors;
	ActorSelection->GetSelectedObjects<AActor>(SelectedActors);

	UWorld* const World = OnGetWorld.IsBound() ? OnGetWorld.Execute() : nullptr;

	SelectActorsByPredicate(World, false, false
		, [this](AActor* const InActor) -> bool
		{
			return IsObjectSelectableInViewport(InActor);
		}
		, SelectedActors);
}

bool FEditorViewportSelectability::SelectActorsByPredicate(UWorld* const InWorld
	, const bool bInSelect
	, const bool bInClearSelection
	, const TFunctionRef<bool(AActor*)> InPredicate
	, const TArray<AActor*>& InActors)
{
	if (!GEditor || !IsValid(InWorld))
	{
		return false;
	}

	USelection* const ActorSelection = GEditor->GetSelectedActors();
	if (!ActorSelection)
	{
		return false;
	}

	const FText TransactionText = bInSelect ? LOCTEXT("SelectActors_Internal", "Select Actor(s)") : LOCTEXT("DeselectActors_Internal", "Deselect Actor(s)");
	FScopedTransaction ScopedTransaction(TransactionText, !GIsTransacting);

	bool bSomethingSelected = false;

	ActorSelection->BeginBatchSelectOperation();
	ActorSelection->Modify();

	if (bInClearSelection)
	{
		ActorSelection->DeselectAll();
	}

	// Early out for specific deselect case
	if (!bInSelect && bInClearSelection)
	{
		ActorSelection->EndBatchSelectOperation();
		GEditor->NoteSelectionChange();

		return true;
	}

	auto SelectIfPossible = [bInSelect, ActorSelection, &InPredicate, &bSomethingSelected](AActor* const InActor)
	{
		if (IsValid(InActor)
			&& ActorSelection->IsSelected(InActor) != bInSelect
			&& InPredicate(InActor))
		{
			bSomethingSelected = true;
			GEditor->SelectActor(InActor, bInSelect, true);
		}
	};

	if (InActors.IsEmpty())
	{
		for (FActorIterator Iter(InWorld); Iter; ++Iter)
		{
			AActor* const Actor = *Iter;
			SelectIfPossible(Actor);
		}
	}
	else
	{
		for (AActor* const Actor : InActors)
		{
			SelectIfPossible(Actor);
		}
	}

	ActorSelection->EndBatchSelectOperation();
	GEditor->NoteSelectionChange();

	if (!bSomethingSelected)
	{
		ScopedTransaction.Cancel();
	}

	return bSomethingSelected;
}

bool FEditorViewportSelectability::IsActorSelectableClass(const AActor& InActor)
{
	const bool bInvalidClass = InActor.IsA<AWorldSettings>() || InActor.IsA<ABrush>();
	return !bInvalidClass;
}

bool FEditorViewportSelectability::IsActorInLevelHiddenLayer(const AActor& InActor, FLevelEditorViewportClient* const InLevelEditorViewportClient)
{
	if (!InLevelEditorViewportClient)
	{
		return false;
	}

	for (const FName Layer : InActor.Layers)
	{
		if (InLevelEditorViewportClient->ViewHiddenLayers.Contains(Layer))
		{
			return true;
		}
	}

	return false;
}

bool FEditorViewportSelectability::DoesActorIntersectBox(const AActor& InActor, const FBox& InBox, FEditorViewportClient* const InEditorViewportClient, const bool bInUseStrictSelection)
{
	if (InActor.IsHiddenEd() || !IsActorSelectableClass(InActor))
	{
		return false;
	}

	if (GCurrentLevelEditingViewportClient
		&& InEditorViewportClient == GCurrentLevelEditingViewportClient
		&& IsActorInLevelHiddenLayer(InActor, GCurrentLevelEditingViewportClient))
	{
		return false;
	}

	// Iterate over all actor components, selecting our primitive components
	for (const UActorComponent* const Component : InActor.GetComponents())
	{
		const UPrimitiveComponent* const PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
		if (PrimitiveComponent
			&& PrimitiveComponent->IsRegistered()
			&& PrimitiveComponent->IsVisibleInEditor()
			&& PrimitiveComponent->IsShown(InEditorViewportClient->EngineShowFlags)
			&& PrimitiveComponent->ComponentIsTouchingSelectionBox(InBox, false, bInUseStrictSelection))
		{
			return true;
		}
	}

	return false;
}

bool FEditorViewportSelectability::DoesActorIntersectFrustum(const AActor& InActor, const FConvexVolume& InFrustum, FEditorViewportClient* const InEditorViewportClient, const bool bInUseStrictSelection)
{
	if (InActor.IsHiddenEd() || !IsActorSelectableClass(InActor))
	{
		return false;
	}

	if (GCurrentLevelEditingViewportClient
		&& InEditorViewportClient == GCurrentLevelEditingViewportClient
		&& IsActorInLevelHiddenLayer(InActor, GCurrentLevelEditingViewportClient))
	{
		return false;
	}

	for (const UActorComponent* const Component : InActor.GetComponents())
	{
		const UPrimitiveComponent* const PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
		if (IsValid(PrimitiveComponent)
			&& PrimitiveComponent->IsRegistered()
			&& PrimitiveComponent->IsVisibleInEditor()
			&& PrimitiveComponent->IsShown(InEditorViewportClient->EngineShowFlags)
			&& PrimitiveComponent->ComponentIsTouchingSelectionFrustum(InFrustum, false, bInUseStrictSelection))
		{
			return true;
		}
	}

	return false;
}

bool FEditorViewportSelectability::GetCursorForHovered(EMouseCursor::Type& OutCursor) const
{
	if (bSelectionLimited && MouseCursor.IsSet())
	{
		OutCursor = MouseCursor.GetValue();
		return true;
	}

	return false;
}

void FEditorViewportSelectability::UpdateHoverFromHitProxy(HHitProxy* const InHitProxy)
{
	AActor* Actor = nullptr;
	bool bIsGizmoHit = false;
	bool bIsActorHit = false;

	if (InHitProxy)
	{
		if (InHitProxy->IsA(HWidgetAxis::StaticGetType()))
		{
			if (bSelectionLimited)
			{
				bIsGizmoHit = true;
			}
		}
		else if (InHitProxy->IsA(HActor::StaticGetType()))
		{
			const HActor* const ActorHitProxy = static_cast<HActor*>(InHitProxy);
			if (ActorHitProxy && IsValid(ActorHitProxy->Actor))
			{
				if (bSelectionLimited)
				{
					bIsActorHit = true;
				}
				Actor = ActorHitProxy->Actor;
			}
		}
	}

	UpdateHoveredActorPrimitives(Actor);

	// Set mouse cursor after hovered primitive component list has been updated
	if (bIsGizmoHit)
	{
		MouseCursor = EMouseCursor::CardinalCross;
	}
	else if (bIsActorHit)
	{
		MouseCursor = HoveredPrimitiveComponents.IsEmpty() ? EMouseCursor::SlashedCircle : EMouseCursor::Crosshairs;
	}
	else if (bSelectionLimited)
	{
		MouseCursor = EMouseCursor::SlashedCircle;
	}
	else
	{
		MouseCursor.Reset();
	}
}

bool FEditorViewportSelectability::HandleClick(FEditorViewportClient* const InViewportClient, HHitProxy* const InHitProxy, const FViewportClick& InClick)
{
	if (!InViewportClient)
	{
		return false;
	}

	UWorld* const World = InViewportClient->GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	// Disable actor selection when sequencer is limiting selection
	const int32 HitX = InViewportClient->Viewport->GetMouseX();
	const int32 HitY = InViewportClient->Viewport->GetMouseY();
	const HHitProxy* const HitResult = InViewportClient->Viewport->GetHitProxy(HitX, HitY);
	if (!HitResult)
	{
		return false;
	}

	if (HitResult->IsA(HWidgetAxis::StaticGetType()) || !HitResult->IsA(HActor::StaticGetType()))
	{
		return false;
	}

	const HActor* const ActorHitProxy = static_cast<const HActor*>(HitResult);
	if (!ActorHitProxy || !IsValid(ActorHitProxy->Actor))
	{
		return false;
	}

	const bool bNotSelectable = !IsObjectSelectableInViewport(ActorHitProxy->Actor);

	if (bNotSelectable)
	{
		SelectActorsByPredicate(World, false, true
			, [this](AActor* const InActor) -> bool
			{
				return false;
			});
	}

	return bNotSelectable;
}

bool FEditorViewportSelectability::BoxSelectWorldActors(FBox& InBox, FEditorViewportClient* const InEditorViewportClient, const bool bInSelect)
{
	if (!InEditorViewportClient || InEditorViewportClient->IsInGameView())
	{
		return false;
	}

	UWorld* const World = InEditorViewportClient->GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	const ULevelEditorViewportSettings* const LevelEditorViewportSettings = GetDefault<ULevelEditorViewportSettings>();
	const bool bUseStrictSelection = IsValid(LevelEditorViewportSettings) ? LevelEditorViewportSettings->bStrictBoxSelection : false;

	const auto Predicate = [this, &InBox, InEditorViewportClient, bUseStrictSelection](AActor* const InActor) -> bool
	{
		const bool bSelectable = IsObjectSelectableInViewport(InActor);
		const bool bIntersects = DoesActorIntersectBox(*InActor, InBox, InEditorViewportClient, bUseStrictSelection);
		return bSelectable && bIntersects;
	};

	const bool bShiftDown = InEditorViewportClient->Viewport->KeyState(EKeys::LeftShift)
		|| InEditorViewportClient->Viewport->KeyState(EKeys::RightShift);

	SelectActorsByPredicate(World, bInSelect, !bShiftDown, Predicate);

	return true;
}

bool FEditorViewportSelectability::FrustumSelectWorldActors(const FConvexVolume& InFrustum, FEditorViewportClient* const InEditorViewportClient, const bool bInSelect)
{
	if (!InEditorViewportClient || InEditorViewportClient->IsInGameView())
	{
		return false;
	}

	UWorld* const World = InEditorViewportClient->GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	const ULevelEditorViewportSettings* const LevelEditorViewportSettings = GetDefault<ULevelEditorViewportSettings>();
	const bool bUseStrictSelection = IsValid(LevelEditorViewportSettings) ? LevelEditorViewportSettings->bStrictBoxSelection : false;

	const auto Predicate = [this, &InFrustum, InEditorViewportClient, bUseStrictSelection](AActor* const InActor) -> bool
	{
		const bool bSelectable = IsObjectSelectableInViewport(InActor);
		const bool bIntersects = DoesActorIntersectFrustum(*InActor, InFrustum, InEditorViewportClient, bUseStrictSelection);
		return bSelectable && bIntersects;
	};

	const bool bShiftDown = InEditorViewportClient->Viewport->KeyState(EKeys::LeftShift)
		|| InEditorViewportClient->Viewport->KeyState(EKeys::RightShift);

	SelectActorsByPredicate(World, bInSelect, !bShiftDown, Predicate);

	return true;
}

void FEditorViewportSelectability::DrawEnabledTextNotice(FCanvas* const InCanvas, const FText& InText)
{
	const FStringView HelpString = *InText.ToString();

	FTextSizingParameters SizingParameters(GEngine->GetLargeFont(), 1.f, 1.f);
	UCanvas::CanvasStringSize(SizingParameters, HelpString);

	const float ViewWidth = InCanvas->GetViewRect().Width() / InCanvas->GetDPIScale();
	const float DrawX = FMath::FloorToFloat((ViewWidth - SizingParameters.DrawXL) * 0.5f);
	InCanvas->DrawShadowedString(DrawX, 34.f, HelpString, GEngine->GetLargeFont(), FLinearColor::White);
}

FText FEditorViewportSelectability::GetLimitedSelectionText(const TSharedPtr<FUICommandInfo>& InToggleAction, const FText& InDefaultText)
{
	if (!InToggleAction.IsValid())
	{
		return FText();
	}

	FText HelpText = InDefaultText.IsEmpty() ? DefaultLimitedSelectionText : InDefaultText;

	if (InToggleAction.IsValid())
	{
		const TSharedRef<const FInputChord> ActiveChord = InToggleAction->GetFirstValidChord();
		if (ActiveChord->IsValidChord())
		{
			HelpText = FText::Format(LOCTEXT("LimitedSelectionActionKeyHelp", "{0}  ({1} to toggle)"), HelpText, ActiveChord->GetInputText(true));
		}
	}

	return HelpText;
}

#undef LOCTEXT_NAMESPACE
