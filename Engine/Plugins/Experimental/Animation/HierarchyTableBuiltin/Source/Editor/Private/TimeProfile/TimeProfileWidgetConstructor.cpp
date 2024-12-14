// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeProfileWidgetConstructor.h"
#include "Widgets/Input/SSpinBox.h"
#include "TimeProfile/HierarchyTableTypeTime.h"
#include "HierarchyTable.h"

TSharedRef<SWidget> FHierarchyTableTimeWidgetConstructor_StartTime::CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex)
{
	return SNew(SSpinBox<float>)
		.IsEnabled_Lambda([HierarchyTable, EntryIndex]() { return HierarchyTable->TableData[EntryIndex].IsOverridden(); })
		.MinDesiredWidth(100.0f)
		.MinValue(0.0f)
		.MaxValue(1.0f)
		.Value_Lambda([HierarchyTable, EntryIndex]()
			{
				return HierarchyTable->TableData[EntryIndex].GetValue<FHierarchyTableType_Time>()->StartTime;
			})
		.OnValueChanged_Lambda([HierarchyTable, EntryIndex](float NewValue)
			{
				HierarchyTable->TableData[EntryIndex].GetMutableValue<FHierarchyTableType_Time>()->StartTime = NewValue;
			});
}

TSharedRef<SWidget> FHierarchyTableTimeWidgetConstructor_EndTime::CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex)
{
	return SNew(SSpinBox<float>)
		.IsEnabled_Lambda([HierarchyTable, EntryIndex]() { return HierarchyTable->TableData[EntryIndex].IsOverridden(); })
		.MinDesiredWidth(100.0f)
		.MinValue(0.0f)
		.MaxValue(1.0f)
		.Value_Lambda([HierarchyTable, EntryIndex]()
			{
				return HierarchyTable->TableData[EntryIndex].GetValue<FHierarchyTableType_Time>()->EndTime;
			})
		.OnValueChanged_Lambda([HierarchyTable, EntryIndex](float NewValue)
			{
				HierarchyTable->TableData[EntryIndex].GetMutableValue<FHierarchyTableType_Time>()->EndTime = NewValue;
			});
}

TSharedRef<SWidget> FHierarchyTableTimeWidgetConstructor_TimeFactor::CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex)
{
	return SNew(SSpinBox<float>)
		.IsEnabled_Lambda([HierarchyTable, EntryIndex]() { return HierarchyTable->TableData[EntryIndex].IsOverridden(); })
		.MinDesiredWidth(100.0f)
		.MinValue(0.0f)
		.MaxValue(1.0f)
		.Value_Lambda([HierarchyTable, EntryIndex]()
			{
				return HierarchyTable->TableData[EntryIndex].GetValue<FHierarchyTableType_Time>()->TimeFactor;
			})
		.OnValueChanged_Lambda([HierarchyTable, EntryIndex](float NewValue)
			{
				HierarchyTable->TableData[EntryIndex].GetMutableValue<FHierarchyTableType_Time>()->TimeFactor = NewValue;
			});
}

TSharedRef<SWidget> FHierarchyTableTimeWidgetConstructor_Preview::CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex)
{
	return SNew(STextBlock)
		.Text(INVTEXT("PREVIEW"));
}