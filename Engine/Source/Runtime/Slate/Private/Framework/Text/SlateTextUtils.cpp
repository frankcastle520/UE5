// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Text/SlateTextUtils.h"
#include "Styling/SlateTypes.h"

bool SlateTextUtils::IsEllipsisPolicy(const ETextOverflowPolicy& InOverflowPolicy)
{
	return	InOverflowPolicy == ETextOverflowPolicy::Ellipsis ||
			InOverflowPolicy == ETextOverflowPolicy::MultilineEllipsis ||
			InOverflowPolicy == ETextOverflowPolicy::MiddleEllipsis;
}
