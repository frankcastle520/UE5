// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Templates/SharedPointer.h"

// TraceInsightsCore
#include "InsightsCore/Table/ViewModels/TableTreeNode.h"

// TraceInsights
#include "Insights/CookProfiler/ViewModels/PackageTable.h"
#include "Insights/CookProfiler/ViewModels/PackageEntry.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::Insights::CookProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FPackageNode;

/** Type definition for shared pointers to instances of FTaskNode. */
typedef TSharedPtr<class FPackageNode> FPackageNodePtr;

/** Type definition for shared references to instances of FTaskNode. */
typedef TSharedRef<class FPackageNode> FPackageNodeRef;

/** Type definition for shared references to const instances of FTaskNode. */
typedef TSharedRef<const class FPackageNode> FPackageNodeRefConst;

/** Type definition for weak references to instances of FTaskNode. */
typedef TWeakPtr<class FTaskNode> FPackageNodeWeak;

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Class used to store information about a package node (used in the SPackageTableTreeView).
 */
class FPackageNode : public FTableTreeNode
{
	INSIGHTS_DECLARE_RTTI(FPackageNode, FTableTreeNode)

public:
	/** Initialization constructor for the Task node. */
	explicit FPackageNode(const FName InName, TWeakPtr<FPackageTable> InParentTable, int32 InRowIndex)
		: FTableTreeNode(InName, InParentTable, InRowIndex)
	{
	}

	/** Initialization constructor for the group node. */
	explicit FPackageNode(const FName InGroupName, TWeakPtr<FPackageTable> InParentTable)
		: FTableTreeNode(InGroupName, InParentTable)
	{
	}

	FPackageTable& GetPackageTableChecked() const
	{
		const TSharedPtr<FTable>& TablePin = GetParentTable().Pin();
		check(TablePin.IsValid());
		return *StaticCastSharedPtr<FPackageTable>(TablePin);
	}

	bool IsValidPackage() const { return GetPackageTableChecked().IsValidRowIndex(GetRowIndex()); }
	const FPackageEntry* GetPackage() const { return GetPackageTableChecked().GetPackage(GetRowIndex()); }
	const FPackageEntry& GetPackageChecked() const { return GetPackageTableChecked().GetPackageChecked(GetRowIndex()); }
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::CookProfiler
