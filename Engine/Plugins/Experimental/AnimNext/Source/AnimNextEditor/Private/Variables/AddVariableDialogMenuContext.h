// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SAddVariablesDialog.h"
#include "AddVariableDialogMenuContext.generated.h"

namespace UE::AnimNext::Editor
{
	class SAddVariablesDialog;
	struct FVariableToAdd;
}

UCLASS()
class UAddVariableDialogMenuContext : public UObject
{
	GENERATED_BODY()

	friend class UE::AnimNext::Editor::SAddVariablesDialog;

	// The add variable dialog that we are editing
	TWeakPtr<UE::AnimNext::Editor::SAddVariablesDialog> AddVariablesDialog;

	// The entry that we are editing
	TWeakPtr<UE::AnimNext::Editor::SAddVariablesDialog::FEntry> Entry;
};