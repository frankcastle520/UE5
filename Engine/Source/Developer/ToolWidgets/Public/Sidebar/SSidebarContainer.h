// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sidebar/SidebarState.h"
#include "Sidebar/SSidebar.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SSidebar;
class SSidebarDrawer;

/**
 * A container for a sidebar widget that manages the slider drawer overlay widgets
 * and a default docking location for all drawers.
 */
class TOOLWIDGETS_API SSidebarContainer : public SCompoundWidget
{
public:
	friend class SSidebar;

	SLATE_BEGIN_ARGS(SSidebarContainer)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void RebuildSidebar(const TSharedRef<SSidebar>& InSidebarWidget, const FSidebarState& InState);

	float GetContentSlotSize() const;
	float GetSidebarSlotSize() const;

	EOrientation GetSplitterOrientation() const;
	ESidebarTabLocation GetTabLocation() const;
	float GetCurrentDrawerSize() const;
	FVector2D GetOverlaySize() const;

	void CloseAllDrawerWidgets(const bool bInAnimate);

	/** Reopens the pinned tab only if there are no other open drawers. This should be used to bring pinned tabs back after other tabs lose focus/are closed. */
	void SummonPinnedTabIfNothingOpened();

	/** Updates the appearance of drawer tabs. */
	void UpdateDrawerTabAppearance();

	FName GetOpenedDrawerId() const;

protected:
	void Reconstruct(const FSidebarState& InState = FSidebarState());

	TSharedRef<SWidget> ConstructBoxPanel(const FSidebarState& InState);
	void ConstructSplitterPanel(const FSidebarState& InState);

	FMargin CalculateSlotMargin() const;

	void CreateDrawerWidget(const TSharedRef<FSidebarDrawer>& InDrawer);

	TSharedRef<SWidget> GetSidebarDrawerContent(const TSharedRef<FSidebarDrawer>& InDrawer) const;

	bool AddDrawerOverlaySlot(const TSharedRef<FSidebarDrawer>& InDrawer);
	bool RemoveDrawerOverlaySlot(const TSharedRef<FSidebarDrawer>& InDrawer, const bool bInAnimate);

	void AddContentDockSlot();
	void RemoveContentDockSlot();

	void AddSidebarDockSlot(const FName InDockDrawerId);
	void RemoveSidebarDockSlot();

	void OnTabDrawerFocusLost(const TSharedRef<SSidebarDrawer>& InDrawerWidget);
	void OnOpenAnimationFinish(const TSharedRef<SSidebarDrawer>& InDrawerWidget);
	void OnCloseAnimationFinish(const TSharedRef<SSidebarDrawer>& InDrawerWidget);
	void OnDrawerSizeChanged(const TSharedRef<SSidebarDrawer>& InDrawerWidget, const float InNewPixelSize);

	EActiveTimerReturnType OnOpenPendingDrawerTimer(const double InCurrentTime, const float InDeltaTime);

	void OpenDrawerNextFrame(const TSharedRef<FSidebarDrawer>& InDrawer, const bool bInAnimate = true);
	void OpenDrawer_Internal(const TSharedRef<FSidebarDrawer>& InDrawer, const bool bInAnimate = true);
	void CloseDrawer_Internal(const TSharedRef<FSidebarDrawer>& InDrawer, const bool bInAnimate = true);

	void DockDrawer_Internal(const TSharedRef<FSidebarDrawer>& InDrawer);
	void UndockDrawer_Internal(const TSharedRef<FSidebarDrawer>& InDrawer);

	TSharedPtr<FSidebarDrawer> FindDrawer(const FName InDrawerId) const;
	TSharedPtr<FSidebarDrawer> FindFirstPinnedTab() const;

	TSharedPtr<SSidebarDrawer> FindOpenDrawerWidget(const TSharedRef<FSidebarDrawer>& InDrawer) const;

	TSharedPtr<FSidebarDrawer> GetForegroundDrawer() const;

	/** Event that occurs when the docked main content slot is being resized by the user (while mouse down). */
	void OnContentSlotResizing(const float InFillPercent);
	/** Event that occurs when the docked sidebar slot is being resized by the user (while mouse down). */
	void OnSidebarSlotResizing(const float InFillPercent);

	/** Event that occurs when the main splitter widget has finished being resized by the user (mouse up). */
	void OnSplitterResized();

	int32 GetContentSlotIndex() const;
	int32 GetSidebarSlotIndex() const;

	/** The sidebar widget associated with this container. One sidebar widget per container. */
	TSharedPtr<SSidebar> SidebarWidget;

	/** The main splitter widget used when a drawer is docked. */
	TSharedPtr<SSplitter> MainSplitter;

	/** Overlay used to draw drawer widgets on top of the rest of the content. */
	TSharedPtr<SOverlay> DrawersOverlay;

	/** Generally speaking one drawer is only ever open at once but we animate any previous drawer
	 * closing so there could be more than one while an animation is playing. A docked drawer is
	 * also considered open, along with any user opened/pinned drawers. */
	TArray<TSharedRef<SSidebarDrawer>> OpenDrawerWidgets;

	TArray<TSharedRef<SSidebarDrawer>> ClosingDrawerWidgets;

	TWeakPtr<FSidebarDrawer> PendingTabToOpen;
	bool bAnimatePendingTabOpen = false;
	TSharedPtr<FActiveTimerHandle> OpenPendingDrawerTimerHandle;

	float ContentSizePercent = 0.8f;
	float SidebarSizePercent = 0.2f;

	TAttribute<float> ContentSlotSize;
	TAttribute<float> SidebarSlotSize;

	bool bWantsToAutoDock = false;
	float ContentSizeBeforeResize = 0.f;
	float SidebarSizeBeforeResize = 0.f;
};
