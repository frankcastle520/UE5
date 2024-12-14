// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ObjectChooserWidgetFactories.h"
#include "Widgets/SWidget.h"

class UChooserTable;
struct FChooserColumnBase;


namespace UE::ChooserEditor
{
	TSharedRef<SWidget> MakeColumnHeaderWidget(UChooserTable* Chooser, FChooserColumnBase* Column, const FText& ColumnName,const FText& ColumnTooltip, const FSlateBrush* ColumnIcon, TSharedPtr<SWidget> DebugWidget, FChooserWidgetValueChanged ValueChanged = FChooserWidgetValueChanged());
}