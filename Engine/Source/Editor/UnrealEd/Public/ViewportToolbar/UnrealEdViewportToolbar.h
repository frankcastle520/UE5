// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "Engine/EngineBaseTypes.h"
#include "Templates/SharedPointerFwd.h"
#include "ToolMenuDelegates.h"
#include "UnrealEdViewportToolbarContext.h"
#include "UnrealWidgetFwd.h"

class FEditorViewportClient;
class FText;
class IPreviewProfileController;
class IPreviewProfileController;
class SEditorViewport;
class UToolMenu;
enum ELevelViewportType : int;
enum ERotationGridMode : int;
struct FNewToolMenuChoice;
struct FToolMenuEntry;

namespace UE::UnrealEd
{

/** Lists View Mode Menu Sections which can be shown/hidden based on specific menu requirements */
enum EHidableViewModeMenuSections : uint8
{
	Exposure = 0,
	GPUSkinCache = 1,
	RayTracingDebug = 2
};

/** The value of this function is controlled by the CVAR "ToolMenusViewportToolbars". */
UNREALED_API bool ShowOldViewportToolbars();

/** The value of this function is controlled by the CVAR "ToolMenusViewportToolbars". */
UNREALED_API bool ShowNewViewportToolbars();

UNREALED_API FSlateIcon GetIconFromCoordSystem(ECoordSystem InCoordSystem);
UNREALED_API FToolMenuEntry CreateViewportToolbarTransformsSection();

UNREALED_API FToolMenuEntry CreateViewportToolbarSelectSection();

UNREALED_API FToolMenuEntry CreateViewportToolbarSnappingSubmenu();

UNREALED_API FText GetViewModesSubmenuLabel(TWeakPtr<SEditorViewport> InViewport);

/**
 * Populate a given UToolMenu with entries for a View Modes viewport toolbar submenu.
 *
 * @param InMenu The menu to populate with entries.
 */
UNREALED_API void PopulateViewModesMenu(UToolMenu* InMenu);

/** Create a Viewport Toolbar Context with common values (many Asset Editors have the same settings) */
UNREALED_API UUnrealEdViewportToolbarContext* CreateViewportToolbarDefaultContext(const TWeakPtr<SEditorViewport>& InViewport
);

UNREALED_API FToolMenuEntry CreateViewportToolbarViewModesSubmenu();

DECLARE_DELEGATE_TwoParams(FRotationGridCheckboxListExecuteActionDelegate, int, ERotationGridMode);
DECLARE_DELEGATE_RetVal_TwoParams(bool, FRotationGridCheckboxListIsCheckedDelegate, int, ERotationGridMode);

DECLARE_DELEGATE_OneParam(FLocationGridCheckboxListExecuteActionDelegate, int);
DECLARE_DELEGATE_RetVal_OneParam(bool, FLocationGridCheckboxListIsCheckedDelegate, int);

DECLARE_DELEGATE_OneParam(FScaleGridCheckboxListExecuteActionDelegate, int);
DECLARE_DELEGATE_RetVal_OneParam(bool, FScaleGridCheckboxListIsCheckedDelegate, int);

DECLARE_DELEGATE_OneParam(FNumericEntryExecuteActionDelegate, float);

UNREALED_API TSharedRef<SWidget> BuildRotationGridCheckBoxList(
	FName InExtentionHook,
	const FText& InHeading,
	const TArray<float>& InGridSizes,
	ERotationGridMode InGridMode,
	const FRotationGridCheckboxListExecuteActionDelegate& InExecuteAction,
	const FRotationGridCheckboxListIsCheckedDelegate& InIsActionChecked,
	const TSharedPtr<FUICommandList>& InCommandList = {}
);

UNREALED_API FText GetRotationGridLabel();
UNREALED_API TSharedRef<SWidget> CreateRotationGridSnapMenu(
	const FRotationGridCheckboxListExecuteActionDelegate& InExecuteDelegate,
	const FRotationGridCheckboxListIsCheckedDelegate& InIsCheckedDelegate,
	const TAttribute<bool>& InIsEnabledDelegate = TAttribute<bool>(true),
	const TSharedPtr<FUICommandList>& InCommandList = {}
);

UNREALED_API FText GetLocationGridLabel();
UNREALED_API TSharedRef<SWidget> CreateLocationGridSnapMenu(
	const FLocationGridCheckboxListExecuteActionDelegate& InExecuteDelegate,
	const FLocationGridCheckboxListIsCheckedDelegate& InIsCheckedDelegate,
	const TArray<float>& InGridSizes,
	const TAttribute<bool>& InIsEnabledDelegate = TAttribute<bool>(true),
	const TSharedPtr<FUICommandList>& InCommandList = {}
);

UNREALED_API FText GetScaleGridLabel();
UNREALED_API TSharedRef<SWidget> CreateScaleGridSnapMenu(
	const FScaleGridCheckboxListExecuteActionDelegate& InExecuteDelegate,
	const FScaleGridCheckboxListIsCheckedDelegate& InIsCheckedDelegate,
	const TArray<float>& InGridSizes,
	const TAttribute<bool>& InIsEnabledDelegate = TAttribute<bool>(true),
	const TSharedPtr<FUICommandList>& InCommandList = {},
	const TAttribute<bool>& ShowPreserveNonUniformScaleOption = TAttribute<bool>(false),
	const FUIAction& PreserveNonUniformScaleUIAction = FUIAction()
);

UNREALED_API FToolMenuEntry CreateCheckboxSubmenu(
	const FName InName,
	const TAttribute<FText>& InLabel,
	const TAttribute<FText>& InToolTip,
	const FToolMenuExecuteAction& InCheckboxExecuteAction,
	const FToolMenuCanExecuteAction& InCheckboxCanExecuteAction,
	const FToolMenuGetActionCheckState& InCheckboxActionCheckState,
	const FNewToolMenuChoice& InMakeMenu
);

UNREALED_API FToolMenuEntry CreateNumericEntry(
	const FName InName,
	const FText& InLabel,
	const FText& InTooltip,
	const FCanExecuteAction& InCanExecuteAction,
	const FNumericEntryExecuteActionDelegate& InOnValueChanged,
	const TAttribute<float>& InGetValue,
	float InMinValue = 0.0f,
	float InMaxValue = 1.0f,
	int32 InMaxFractionalDigits = 2
);

UNREALED_API FText GetCameraSpeedLabel(const TWeakPtr<SEditorViewport>& WeakViewport);
UNREALED_API FText GetCameraSubmenuLabelFromViewportType(const ELevelViewportType ViewportType);
UNREALED_API FName GetCameraSubmenuIconFNameFromViewportType(const ELevelViewportType ViewportType);
UNREALED_API FToolMenuEntry CreateViewportToolbarCameraSubmenu();

UNREALED_API FToolMenuEntry CreateViewportToolbarAssetViewerProfileSubmenu(const TSharedPtr<IPreviewProfileController>& InPreviewProfileController);

UNREALED_API void PopulateCameraMenu(UToolMenu* InMenu);

/**
 * Adds Field of View and Far View Plane entries to the specified Camera Submenu
 */
UNREALED_API void ExtendCameraSubmenu(FName InCameraOptionsSubmenuName);

UNREALED_API bool ShouldShowViewportRealtimeWarning(const FEditorViewportClient& ViewportClient);

UNREALED_API FToolMenuEntry CreatePerformanceAndScalabilitySubmenu();

/**
 * Creates a Show submenu with commonly used show flags
 */
UNREALED_API FToolMenuEntry CreateDefaultShowSubmenu();

/**
 * Adds common flags sections to the specified menu
 */
UNREALED_API void AddDefaultShowFlags(UToolMenu* InMenu);

UNREALED_API FToolMenuEntry CreateToggleRealtimeEntry();

// Camera Menu Widgets
TSharedRef<SWidget> CreateCameraMenuWidget(const TSharedRef<SEditorViewport>& InViewport);
TSharedRef<SWidget> CreateFOVMenuWidget(const TSharedRef<SEditorViewport>& InViewport);
TSharedRef<SWidget> CreateFarViewPlaneMenuWidget(const TSharedRef<SEditorViewport>& InViewport);

// Screen Percentage Submenu Widgets
TSharedRef<SWidget> CreateCurrentPercentageWidget(FEditorViewportClient& InViewportClient);
TSharedRef<SWidget> CreateResolutionsWidget(FEditorViewportClient& InViewportClient);
TSharedRef<SWidget> CreateActiveViewportWidget(FEditorViewportClient& InViewportClient);
TSharedRef<SWidget> CreateSetFromWidget(FEditorViewportClient& InViewportClient);
TSharedRef<SWidget> CreateCurrentScreenPercentageSettingWidget(FEditorViewportClient& InViewportClient);
TSharedRef<SWidget> CreateCurrentScreenPercentageWidget(FEditorViewportClient& InViewportClient);

} // namespace UE::UnrealEd
