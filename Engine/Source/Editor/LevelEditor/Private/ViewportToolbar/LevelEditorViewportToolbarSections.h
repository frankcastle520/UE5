// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"
#include "ToolMenuEntry.h"

class FExtender;
class FLevelEditorViewportClient;
class SLevelViewport;
class SWidget;
class UToolMenu;
struct FToolMenuSection;

namespace UE::LevelEditor
{
void CreateCameraSpawnMenu(UToolMenu* InMenu);
void CreateBookmarksMenu(UToolMenu* InMenu);
void CreateCameraSpeedMenu(UToolMenu* InMenu);
void AddCameraActorSelectSection(UToolMenu* InMenu);

FToolMenuEntry CreateFOVMenu(TWeakPtr<SLevelViewport> InLevelViewportWeak);
FToolMenuEntry CreateFarViewPlaneMenu(TWeakPtr<SLevelViewport> InLevelViewportWeak);
FToolMenuEntry CreateCameraSpeedSlider(TWeakPtr<SLevelViewport> InLevelViewportWeak);
FToolMenuEntry CreateCameraSpeedScalarSlider(TWeakPtr<SLevelViewport> InLevelViewportWeak);
FToolMenuEntry CreateLevelViewportToolbarCameraSubmenu();

TSharedPtr<FExtender> GetViewModesLegacyExtenders();
void PopulateViewModesMenu(UToolMenu* InMenu);
void ExtendViewModesSubmenu(FName InViewModesSubmenuName);

FToolMenuEntry CreateShowFoliageSubmenu();
FToolMenuEntry CreateShowHLODsSubmenu();
FToolMenuEntry CreateShowLayersSubmenu();
FToolMenuEntry CreateShowSpritesSubmenu();
#if STATS
FToolMenuEntry CreateShowStatsSubmenu(
	bool bInAddToggleStatsCheckbox = false, TAttribute<FText> InLabelOverride = TAttribute<FText>()
);
#endif
FToolMenuEntry CreateShowVolumesSubmenu();
FToolMenuEntry CreateViewportToolbarShowSubmenu();

FToolMenuEntry CreateFeatureLevelPreviewSubmenu();
FToolMenuEntry CreateMaterialQualityLevelSubmenu();
FToolMenuEntry CreateViewportToolbarPerformanceAndScalabilitySubmenu();

void GenerateViewportLayoutsMenu(UToolMenu* InMenu);
TSharedRef<SWidget> BuildVolumeControlCustomWidget();
FToolMenuEntry CreateLevelEditorViewportToolbarSettingsSubmenu();

void ExtendCameraSubmenu(FName InCameraOptionsSubmenuName);
} // namespace UE::LevelEditor
