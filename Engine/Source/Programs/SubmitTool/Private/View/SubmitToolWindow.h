// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"
#include "Widgets/Docking/SDockTab.h"

class FModelInterface;

class SubmitToolWindow
{
public:
	SubmitToolWindow(FModelInterface* modelInterface);

	TSharedRef<SDockTab> BuildMainTab(TSharedPtr<SWindow> InParentWindow);

private:
	TSharedPtr<SDockTab> MainTab;
	
	bool OnCanCloseTab();
	
	void CreateAutoUpdateSubmitToolContent(TSharedPtr<SWindow> InParentWindow);
	void CreateMainSubmitToolContent(TSharedPtr<SWindow> InParentWindow);

	FModelInterface* ModelInterface;
};
