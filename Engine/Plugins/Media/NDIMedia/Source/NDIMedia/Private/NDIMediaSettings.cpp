// Copyright Epic Games, Inc. All Rights Reserved.

#include "NDIMediaSettings.h"

#include "NDIMediaAPI.h"

UNDIMediaSettings::UNDIMediaSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName  = TEXT("NDI Media");
}

const TCHAR* UNDIMediaSettings::GetNDILibRedistUrl() const
{
	return TEXT(NDILIB_REDIST_URL);
}