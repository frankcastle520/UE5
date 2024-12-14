// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaNameAttribute.h"

#define LOCTEXT_NAMESPACE "AvaNameAttribute"

FText UAvaNameAttribute::GetDisplayName() const
{
	return FText::Format(LOCTEXT("DisplayName", "Name: {0}"), FText::FromName(Name));
}

void UAvaNameAttribute::SetName(FName InName)
{
	Name = InName;
}

#undef LOCTEXT_NAMESPACE
