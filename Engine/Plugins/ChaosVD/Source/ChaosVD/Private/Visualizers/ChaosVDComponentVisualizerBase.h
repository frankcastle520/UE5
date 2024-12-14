// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComponentVisualizer.h"
#include "HAL/Platform.h"
#include "Templates/SharedPointer.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "Utils/ChaosVDUserInterfaceUtils.h"
#include "Widgets/SChaosVDEnumFlagsMenu.h"
#include "Widgets/SWidget.h"

class SChaosVDMainTab;
class FChaosVDSolverDataSelection;
struct FChaosVDSolverDataSelectionHandle;
class UChaosVDSettingsObjectBase;
class FChaosVDScene;

/** Context needed to be able to visualize data in the viewport */
struct FChaosVDVisualizationContext
{
	FTransform SpaceTransform;
	TWeakPtr<FChaosVDScene> CVDScene;
	int32 SolverID = INDEX_NONE;
	uint32 VisualizationFlags = 0;
	const UChaosVDSettingsObjectBase* DebugDrawSettings = nullptr;
	TSharedPtr<FChaosVDSolverDataSelection> SolverDataSelectionObject = nullptr;
};

/** Custom Hit Proxy for debug drawn particle data */
struct HChaosVDComponentVisProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY()

	HChaosVDComponentVisProxy(const UActorComponent* InComponent, const TSharedPtr<FChaosVDSolverDataSelectionHandle>& InDataSelectionHandle)
		: HComponentVisProxy(InComponent), DataSelectionHandle(InDataSelectionHandle)
	{
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}

	TSharedPtr<FChaosVDSolverDataSelectionHandle> DataSelectionHandle;
};

/** Base class used for all component visualizers in CVD - It provides a common code to handle selection and clicks*/
class FChaosVDComponentVisualizerBase : public FComponentVisualizer
{
public:

	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;

protected:
	
	virtual void RegisterVisualizerMenus() = 0;
	
	virtual bool CanHandleClick(const HChaosVDComponentVisProxy& VisProxy);

	virtual bool SelectVisualizedData(const HChaosVDComponentVisProxy& VisProxy, const TSharedRef<FChaosVDScene>& InCVDScene,const TSharedRef<SChaosVDMainTab>& InMainTabToolkitHost);

	template<typename ObjectSettingsType, typename VisualizationFlagsType>
	void CreateGenericVisualizerMenu(FName MenuToExtend, FName SectionName, const FText& InSectionLabel, const FText& InFlagsMenuLabel,  const FText& InFlagsMenuTooltip, FSlateIcon FlagsMenuIcon,  const FText& InSettingsMenuLabel, const FText& InSettingsMenuTooltip);

	FName InspectorTabID = NAME_None;
};

template <typename ObjectSettingsType, typename VisualizationFlagsType>
void FChaosVDComponentVisualizerBase::CreateGenericVisualizerMenu(FName MenuToExtend, FName SectionName, const FText& InSectionLabel, const FText& InFlagsMenuLabel,  const FText& InFlagsMenuTooltip, FSlateIcon FlagsMenuIcon,  const FText& InSettingsMenuLabel, const FText& InSettingsMenuTooltip)
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	
	if (!ensure(ToolMenus))
	{
		return;
	}

	if (UToolMenu* Menu = ToolMenus->ExtendMenu(MenuToExtend))
	{
		using namespace Chaos::VisualDebugger::Utils;
		CreateVisualizationOptionsMenuSections<ObjectSettingsType, VisualizationFlagsType>(Menu, SectionName, InSectionLabel, InFlagsMenuLabel, InFlagsMenuTooltip, FlagsMenuIcon, InSettingsMenuLabel, InSettingsMenuTooltip);
	}
}
