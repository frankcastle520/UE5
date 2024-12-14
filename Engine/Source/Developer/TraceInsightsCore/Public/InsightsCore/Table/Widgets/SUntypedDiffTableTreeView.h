// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Input/Reply.h"
#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"

// TraceInsights
#include "InsightsCore/Table/ViewModels/UntypedTable.h"
#include "InsightsCore/Table/Widgets/SUntypedTableTreeView.h"

namespace UE::Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API SUntypedDiffTableTreeView : public SUntypedTableTreeView
{
public:
	void UpdateSourceTableA(const FString& Name, TSharedPtr<TraceServices::IUntypedTable> SourceTable);
	void UpdateSourceTableB(const FString& Name, TSharedPtr<TraceServices::IUntypedTable> SourceTable);

protected:
	FReply SwapTables_OnClicked();
	FText GetSwapButtonText() const;

	virtual TSharedPtr<SWidget> ConstructToolbar() override;

	void RequestMergeTables();

private:
	TSharedPtr<TraceServices::IUntypedTable> SourceTableA;
	TSharedPtr<TraceServices::IUntypedTable> SourceTableB;
	FString TableNameA;
	FString TableNameB;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights
