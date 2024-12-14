// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Layout/Visibility.h"

#include "InsightsCore/Table/ViewModels/TableCellValue.h"

class IToolTip;

namespace UE::Insights
{

class FBaseTreeNode;
class FTableColumn;

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API ITableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const = 0;
	virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const = 0;

	virtual FText FormatValue(const FTableColumn& Column, const FBaseTreeNode& Node) const = 0;
	virtual FText FormatValueForTooltip(const FTableColumn& Column, const FBaseTreeNode& Node) const = 0;
	virtual FText FormatValueForGrouping(const FTableColumn& Column, const FBaseTreeNode& Node) const = 0;

	virtual FText CopyValue(const FTableColumn& Column, const FBaseTreeNode& Node) const = 0;
	virtual FText CopyTooltip(const FTableColumn& Column, const FBaseTreeNode& Node) const = 0;

	virtual TSharedPtr<IToolTip> GetCustomTooltip(const FTableColumn& Column, const FBaseTreeNode& Node) const = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FTableCellValueFormatter : public ITableCellValueFormatter
{
public:
	FTableCellValueFormatter() {}
	virtual ~FTableCellValueFormatter() {}

	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override { return FText::GetEmpty(); }
	virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const override { return FormatValue(InValue); }

	virtual FText FormatValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override; // { return FormatValue(Column.GetValue(Node)); }
	virtual FText FormatValueForTooltip(const FTableColumn& Column, const FBaseTreeNode& Node) const override; // { return FormatValueForTooltip(Column.GetValue(Node)); }
	virtual FText FormatValueForGrouping(const FTableColumn& Column, const FBaseTreeNode& Node) const override; // { return FormatValueForTooltip(Column.GetValue(Node)); }

	virtual FText CopyValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override; // { return FormatValue(Column, Node); }
	virtual FText CopyTooltip(const FTableColumn& Column, const FBaseTreeNode& Node) const override; // { return FormatValueForTooltip(Column, Node); }

	virtual TSharedPtr<IToolTip> GetCustomTooltip(const FTableColumn& Column, const FBaseTreeNode& Node) const override;

	static EVisibility GetTooltipVisibility();
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FTextValueFormatter : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override
	{
		if (InValue.IsSet())
		{
			return InValue.GetValue().GetText();
		}
		return FText::GetEmpty();
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FAsTextValueFormatter : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override
	{
		if (InValue.IsSet())
		{
			return InValue.GetValue().AsText();
		}
		return FText::GetEmpty();
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FBoolValueFormatterAsTrueFalse : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FBoolValueFormatterAsOnOff : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FInt64ValueFormatterAsNumber : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override
	{
		return InValue.IsSet() ? FText::AsNumber(InValue.GetValue().Int64) : FText::GetEmpty();
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FInt64ValueFormatterAsUInt32InfinteNumber : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override;
	virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FInt64ValueFormatterAsHex32 : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override
	{
		return InValue.IsSet() ? FText::FromString(FString::Printf(TEXT("0x%08X"), static_cast<uint32>(InValue.GetValue().Int64))) : FText::GetEmpty();
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FInt64ValueFormatterAsHex64 : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override
	{
		return InValue.IsSet() ? FText::FromString(FString::Printf(TEXT("0x%016llX"), static_cast<uint64>(InValue.GetValue().Int64))) : FText::GetEmpty();
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FInt64ValueFormatterAsMemory : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override;
	virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FFloatValueFormatterAsNumber : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FFloatValueFormatterAsTimeAuto : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override;
	virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FDoubleValueFormatterAsNumber : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FDoubleValueFormatterAsTimeAuto : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override;
	virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FDoubleValueFormatterAsTimeMs : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override;
	virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FCStringValueFormatterAsText : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights
