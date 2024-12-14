// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceViewModel.h"

#include "Containers/UnrealString.h"

namespace UE::Insights
{

FText FTraceViewModel::AnsiStringViewToText(const FAnsiStringView& AnsiStringView)
{
	FString FatString = FString::ConstructFromPtrSize(AnsiStringView.GetData(), AnsiStringView.Len());
	return FText::FromString(FatString);
}

} // namespace UE::Insights
