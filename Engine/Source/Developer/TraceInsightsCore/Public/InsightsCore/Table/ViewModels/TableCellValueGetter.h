// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "InsightsCore/Table/ViewModels/TableCellValue.h"

namespace UE::Insights
{

class FBaseTreeNode;
class FTableColumn;

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API ITableCellValueGetter
{
public:
	virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const = 0;
	virtual uint64 GetValueId(const FTableColumn& Column, const FBaseTreeNode& Node) const = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FTableCellValueGetter : public ITableCellValueGetter
{
public:
	FTableCellValueGetter() {}
	virtual ~FTableCellValueGetter() {}

	virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override { return TOptional<FTableCellValue>(); }
	virtual uint64 GetValueId(const FTableColumn& Column, const FBaseTreeNode& Node) const override { return 0; }
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FNameValueGetter : public FTableCellValueGetter
{
public:
	virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FDisplayNameValueGetter : public FTableCellValueGetter
{
public:
	virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights
