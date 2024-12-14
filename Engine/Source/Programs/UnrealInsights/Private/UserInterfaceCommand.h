// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FUserInterfaceCommand
{
public:
	/** Executes the command. */
	static void Run(bool bFrontendMode, const FString& TraceFileToOpen);

protected:
	/**
	 * Initializes the Slate application.
	 */
	static void InitializeSlateApplication(bool bFrontendMode, const FString& TraceFileToOpen);

	/**
	 * Shuts down the Slate application.
	 */
	static void ShutdownSlateApplication();
};
