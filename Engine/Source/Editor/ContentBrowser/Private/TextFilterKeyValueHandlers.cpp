// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextFilterKeyValueHandlers.h"

#include "TextFilterKeyValueHandler.h"
#include "UObject/Class.h"

class FTextFilterString;
struct FContentBrowserItem;

bool UTextFilterKeyValueHandlers::HandleTextFilterKeyValue(const FContentBrowserItem& InContentBrowserItem, const FName& InKey, const FTextFilterString& InValue, const ETextFilterComparisonOperation InComparisonOperation, const ETextFilterTextComparisonMode InTextComparisonMode, bool& bOutIsMatch)
{
	return false;
}