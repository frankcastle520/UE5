// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

namespace UE::Insights { class FTableColumn; }

namespace UE::Insights::MemoryProfiler
{

// Column identifiers
struct FMemTagTreeViewColumns
{
	static const FName NameColumnID;
	static const FName TypeColumnID;
	static const FName TrackerColumnID;
	static const FName InstanceCountColumnID;
	static const FName MinValueColumnID;
	static const FName MaxValueColumnID;
	static const FName AverageValueColumnID;
};

struct FMemTagTreeViewColumnFactory
{
public:
	static void CreateMemTagTreeViewColumns(TArray<TSharedRef<FTableColumn>>& Columns);

	static TSharedRef<FTableColumn> CreateNameColumn();
	static TSharedRef<FTableColumn> CreateTypeColumn();
	static TSharedRef<FTableColumn> CreateTrackerColumn();
	static TSharedRef<FTableColumn> CreateInstanceCountColumn();
	static TSharedRef<FTableColumn> CreateMinValueColumn();
	static TSharedRef<FTableColumn> CreateMaxValueColumn();
	static TSharedRef<FTableColumn> CreateAverageValueColumn();

private:
	static constexpr float ValueColumnInitialWidth = 50.0f;
};

} // namespace UE::Insights::MemoryProfiler
