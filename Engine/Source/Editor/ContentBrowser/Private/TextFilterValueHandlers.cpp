// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextFilterValueHandlers.h"

#include "TextFilterValueHandler.h"
#include "UObject/Class.h"

class FTextFilterString;
struct FContentBrowserItem;

bool UTextFilterValueHandlers::HandleTextFilterValue(const FContentBrowserItem& InContentBrowserItem, const FTextFilterString& InValue, const ETextFilterTextComparisonMode InTextComparisonMode, bool& bOutIsMatch)
{

	return false;
}