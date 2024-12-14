// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextVariableEntryProxy.generated.h"

class UAnimNextVariableEntry;
class UAnimNextDataInterfaceEntry;

namespace UE::AnimNext::Editor
{
	class FVariablesOutlinerMode;
	class FVariableProxyCustomization;
}

// Editor-only proxy object used for editing variable entries in the context of per-interface overrides
UCLASS()
class UAnimNextVariableEntryProxy : public UObject
{
	GENERATED_BODY()

	friend class UE::AnimNext::Editor::FVariableProxyCustomization;
	friend class UE::AnimNext::Editor::FVariablesOutlinerMode;

	UPROPERTY()
	TObjectPtr<UAnimNextVariableEntry> VariableEntry;

	UPROPERTY()
	TObjectPtr<UAnimNextDataInterfaceEntry> DataInterfaceEntry;
};