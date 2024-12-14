// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FAdvancedRenamerLevelEditorIntegration
{
public:
	static void Initialize();
	static void Shutdown();

private:
	static void InitializeMenu();
	static void ShutdownMenu();
};
