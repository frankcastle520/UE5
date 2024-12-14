// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PrimitiveComponent.h"
#include "GenericPlatform/ICursor.h"
#include "Internationalization/Text.h"
#include "Math/Color.h"
#include "Misc/Optional.h"

class AActor;
class FCanvas;
class FEditorViewportClient;
class FLevelEditorViewportClient;
class FUICommandInfo;
class HHitProxy;
struct FConvexVolume;
struct FViewportClick;

DECLARE_DELEGATE_RetVal(UWorld*, FOnGetWorld);
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnIsObjectSelectableInViewport, UObject* /*InObject*/);

/**
 * Manages level actor viewport selectability and hovered visual states.
 * Contains static methods to enable outside modules to implement their own management.
 */
class UNREALED_API FEditorViewportSelectability
{
public:
	/** Default text to display in the viewport when selection is limited as a helpful reminder to the user. */
	static const FText DefaultLimitedSelectionText;

	/** Updates a single primitive component's hovered state and visuals. */
	static void UpdatePrimitiveVisuals(const bool bInSelectedLimited, UPrimitiveComponent* const InPrimitive, const TOptional<FColor>& InColor = TOptional<FColor>());

	/** Updates a list of hovered primitive component's hovered state and visuals */
	static bool UpdateHoveredPrimitive(const bool bInSelectedLimited
		, UPrimitiveComponent* const InPrimitiveComponent
		, TMap<TWeakObjectPtr<UPrimitiveComponent>, TOptional<FColor>>& InOutHoveredPrimitiveComponents
		, const TFunctionRef<bool(UObject*)>& InSelectablePredicate);

	/** Updates an actors hovered state and visuals. */
	static bool UpdateHoveredActorPrimitives(const bool bInSelectedLimited
		, AActor* const InActor
		, TMap<TWeakObjectPtr<UPrimitiveComponent>, TOptional<FColor>>& InOutHoveredPrimitiveComponents
		, const TFunctionRef<bool(UObject*)>& InSelectablePredicate);

	static FText GetLimitedSelectionText(const TSharedPtr<FUICommandInfo>& InToggleAction, const FText& InDefaultText = DefaultLimitedSelectionText);

	static void DrawEnabledTextNotice(FCanvas* const InCanvas, const FText& InText);

	FEditorViewportSelectability() = delete;
	FEditorViewportSelectability(const FOnGetWorld& InOnGetWorld, const FOnIsObjectSelectableInViewport& InOnIsObjectSelectableInViewport);

	/** Enables or disables the selectability tool */
	void EnableLimitedSelection(const bool bInEnabled);

	bool IsSelectionLimited() const
    {
    	return bSelectionLimited;
    }

	/** @return True if the specified object is selectable in the viewport and not made unselectable by the Sequencer selection limiting. */
	bool IsObjectSelectableInViewport(UObject* const InObject) const;

	/** Updates hover visual states based on current selection limiting settings */
	void UpdateSelectionLimitedVisuals(const bool bInClearHovered);

	void DeselectNonSelectableActors();

	bool GetCursorForHovered(EMouseCursor::Type& OutCursor) const;

	void UpdateHoverFromHitProxy(HHitProxy* const InHitProxy);

	bool HandleClick(FEditorViewportClient* const InViewportClient, HHitProxy* const InHitProxy, const FViewportClick& InClick);

	/** Selects or deselects all actors in a level world that are inside a defined box. */
	bool BoxSelectWorldActors(FBox& InBox, FEditorViewportClient* const InEditorViewportClient, const bool bInSelect);
	/** Selects or deselects all actors in a level world that are inside a defined convex volume. */
	bool FrustumSelectWorldActors(const FConvexVolume& InFrustum, FEditorViewportClient* const InEditorViewportClient, const bool bInSelect);

protected:
	static bool IsActorSelectableClass(const AActor& InActor);

	static bool IsActorInLevelHiddenLayer(const AActor& InActor, FLevelEditorViewportClient* const InLevelEditorViewportClient);

    static bool DoesActorIntersectBox(const AActor& InActor, const FBox& InBox, FEditorViewportClient* const InEditorViewportClient, const bool bInUseStrictSelection);
    static bool DoesActorIntersectFrustum(const AActor& InActor, const FConvexVolume& InFrustum, FEditorViewportClient* const InEditorViewportClient, const bool bInUseStrictSelection);

	/**
	 * Selects or deselects actors in a world. If no actors are specified, uses all the actors in the level.
	 * 
	 * @param InPredicate Function to use to check if the actor should be selected/deselected
	 * @param InActors Optional list of actors to selected/deselected
	 * @param bInSelect If true, selects the actors. If false, deselects the actors
	 * @param bInClearSelection If true, clears the current selection before selecting the new actors
	 * @return True if atleast one new actor was selected/deselected
	 */
	static bool SelectActorsByPredicate(UWorld* const InWorld
		, const bool bInSelect
		, const bool bInClearSelection
		, const TFunctionRef<bool(AActor*)> InPredicate
		, const TArray<AActor*>& InActors = {});

	/** Updates an actors hovered state and visuals. */
	void UpdateHoveredActorPrimitives(AActor* const InActor);

	bool bSelectionLimited = false;

	/** Hovered primitives and their last overlay color before we apply the hover overlay */
	TMap<TWeakObjectPtr<UPrimitiveComponent>, TOptional<FColor>> HoveredPrimitiveComponents;

	/** Mouse cursor to display for the viewport when selection is limited. */
	TOptional<EMouseCursor::Type> MouseCursor;

	FOnGetWorld OnGetWorld;

	/** Delegate used to check if an object is selectable in the viewport */
	FOnIsObjectSelectableInViewport OnIsObjectSelectableInViewportDelegate;
};
