// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

class FArchive;
class FText;
class UAvaRundown;

namespace UE::AvaMedia::RundownSerializationUtils
{
	AVALANCHEMEDIA_API bool SaveRundownToJson(const UAvaRundown* InRundown, FArchive& InArchive, FText& OutErrorMessage);
	AVALANCHEMEDIA_API bool SaveRundownToJson(const UAvaRundown* InRundown, const TCHAR* InFilepath, FText& OutErrorMessage);

	AVALANCHEMEDIA_API bool LoadRundownFromJson(UAvaRundown* InRundown, FArchive& InArchive, FText& OutErrorMessage);
	AVALANCHEMEDIA_API bool LoadRundownFromJson(UAvaRundown* InRundown, const TCHAR* InFilepath, FText& OutErrorMessage);
}