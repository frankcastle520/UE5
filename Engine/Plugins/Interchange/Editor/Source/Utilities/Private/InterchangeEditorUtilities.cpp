// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeEditorUtilities.h"

#include "FileHelpers.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "Editor/EditorEngine.h"

extern UNREALED_API UEditorEngine* GEditor;
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeEditorUtilities)

bool UInterchangeEditorUtilities::SaveAsset(UObject* Asset)
{
	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(Asset->GetPackage());
	FEditorFileUtils::EPromptReturnCode ReturnCode = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false /*bCheckDirty*/, false /*bPromptToSave*/);
	return (ReturnCode == FEditorFileUtils::PR_Success);
}

bool UInterchangeEditorUtilities::IsRuntimeOrPIE()
{
#if WITH_EDITOR
	return (GEditor && GEditor->PlayWorld) || GIsPlayInEditorWorld || IsRunningGame();
#else
	return true;
#endif // WITH_EDITOR
}