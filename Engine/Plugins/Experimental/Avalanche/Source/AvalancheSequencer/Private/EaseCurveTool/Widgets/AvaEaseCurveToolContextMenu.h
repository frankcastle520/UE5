// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Templates/SharedPointer.h"

class FUICommandList;
class SWidget;
class UToolMenu;

DECLARE_DELEGATE_OneParam(FAvaEaseCurveToolOnGraphSizeChanged, const int32 /*InNewSize*/)

class FAvaEaseCurveToolContextMenu : public TSharedFromThis<FAvaEaseCurveToolContextMenu>
{
public:
	FAvaEaseCurveToolContextMenu(const TWeakPtr<FUICommandList>& InCommandListWeak, const FAvaEaseCurveToolOnGraphSizeChanged& InOnGraphSizeChanged);

	TSharedRef<SWidget> GenerateWidget();

protected:
	void PopulateContextMenuSettings(UToolMenu* const InToolMenu);
	
	TWeakPtr<FUICommandList> CommandListWeak;

	FAvaEaseCurveToolOnGraphSizeChanged OnGraphSizeChanged;
	
	int32 GraphSize;
};
