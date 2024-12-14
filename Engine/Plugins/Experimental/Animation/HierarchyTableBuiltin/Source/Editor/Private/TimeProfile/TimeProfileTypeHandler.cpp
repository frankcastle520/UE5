// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeProfile/TimeProfileTypeHandler.h"
#include "TimeProfile/HierarchyTableTypeTime.h"
#include "TimeProfile/TimeProfileProxyColumn.h"

FInstancedStruct UHierarchyTableTypeHandler_Time::GetDefaultEntry() const
{
	FInstancedStruct NewEntry;
	NewEntry.InitializeAs(FHierarchyTableType_Time::StaticStruct());
	FHierarchyTableType_Time& NewEntryRef = NewEntry.GetMutable<FHierarchyTableType_Time>();
	NewEntryRef.StartTime = 0.0f;
	NewEntryRef.EndTime = 1.0f;
	NewEntryRef.TimeFactor = 1.0f;

	return NewEntry;
}

TArray<UScriptStruct*> UHierarchyTableTypeHandler_Time::GetColumns() const
{
	return { FHierarchyTableTimeColumn_StartTime::StaticStruct(), FHierarchyTableTimeColumn_EndTime::StaticStruct(), FHierarchyTableTimeColumn_TimeFactor::StaticStruct() };
}