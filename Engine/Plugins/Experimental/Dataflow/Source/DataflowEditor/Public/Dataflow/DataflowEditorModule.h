// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacterFXEditorModule.h"


class FDataflowSNodeFactory;

/**
 * The public interface to this module
 */
class DATAFLOWEDITOR_API FDataflowEditorModule : public FBaseCharacterFXEditorModule
{
public:

	static const FColor SurfaceColor;

	/** IModuleInterface implementation */
	virtual void StartupModule();
	virtual void ShutdownModule();

private:

	TSharedPtr<FDataflowSNodeFactory> DataflowSNodeFactory;

};

