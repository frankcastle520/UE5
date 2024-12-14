// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IEditorTraceUtilitiesModule.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTraceUtilities, Log, All)

namespace UE::EditorTraceUtilities
{
class FEditorTraceUtilitiesModule : public IEditorTraceUtilitiesModule
{
public:

	//~ Begin IEditorTraceUtilitiesModule Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual const FStatusBarTraceSettings& GetTraceSettings() const override;
	//~ End IEditorTraceUtilitiesModule Interface

	static const FString& GetTraceUtilitiesIni() { return EditorTraceUtilitiesIni; }

private:
	FDelegateHandle RegisterStartupCallbackHandle;
	static FString EditorTraceUtilitiesIni;
};
} // namespace UE::EditorTraceUtilities