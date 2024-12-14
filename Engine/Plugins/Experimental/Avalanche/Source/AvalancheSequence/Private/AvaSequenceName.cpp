// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaSequenceName.h"

bool FAvaSequenceName::SerializeFromMismatchedTag(const FPropertyTag& InPropertyTag, FStructuredArchive::FSlot InSlot)
{
	if (InPropertyTag.GetType().GetName() == NAME_NameProperty)
	{
		InSlot << Name;
		return true;
	}
	return false;
}
