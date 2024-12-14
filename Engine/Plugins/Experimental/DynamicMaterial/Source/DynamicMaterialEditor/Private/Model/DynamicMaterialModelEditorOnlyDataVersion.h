// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Misc/Guid.h"

struct FDynamicMaterialModelEditorOnlyDataVersion
{
private:
	FDynamicMaterialModelEditorOnlyDataVersion() = delete;

public:
	enum Type : uint8
	{
		PreVersioning = 0,

		/** When the global variables were added and a couple were renamed. */
		GlobalValueRename,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static const FGuid GUID;
};
