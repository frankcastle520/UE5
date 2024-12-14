// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"

class FName;
class SDMMaterialSlotEditor;
class SDMMaterialStage;
class SWidget;
class UToolMenu;

class FDMMaterialStageMenus final
{
public:
	static TSharedRef<SWidget> GenerateStageMenu(const TSharedPtr<SDMMaterialSlotEditor>& InSlotWidget, const TSharedPtr<SDMMaterialStage>& InStageWidget);

private:
	static void AddStageSettingsSection(UToolMenu* InMenu);

	static void AddStageSourceSection(UToolMenu* InMenu);
};
