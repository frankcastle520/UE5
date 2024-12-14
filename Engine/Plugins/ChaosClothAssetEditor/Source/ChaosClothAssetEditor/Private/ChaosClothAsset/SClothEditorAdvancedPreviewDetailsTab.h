// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SAdvancedPreviewDetailsTab.h"

// Custom subclass of SAdvancedPreviewDetailsTab that allows invalidating the cached state of the SettingsView when the preview scene changes
class CHAOSCLOTHASSETEDITOR_API SChaosClothEditorAdvancedPreviewDetailsTab : public SAdvancedPreviewDetailsTab
{
public:

	SChaosClothEditorAdvancedPreviewDetailsTab();
	virtual ~SChaosClothEditorAdvancedPreviewDetailsTab() override;

private:

	FDelegateHandle PropertyChangedDelegateHandle;
};
