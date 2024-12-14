// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Param/ParamType.h"
#include "EdGraphSchema_K2.h"
#include "StructView.h"

struct FAnimNextVariableBindingData;
class UAnimNextModule;
class UAnimNextRigVMAssetEditorData;
struct FAnimNextParamType;
class URigVMController;
struct FAnimNextWorkspaceAssetRegistryExports;
class SWidget;

struct FAnimNextAssetRegistryExports;

namespace UE::AnimNext::Editor
{

struct FUtils
{
	static FName ValidateName(const UObject* InObject, const FString& InName);

	static void GetAllEntryNames(const UAnimNextRigVMAssetEditorData* InEditorData, TSet<FName>& OutNames);

	static FAnimNextParamType GetParameterTypeFromMetaData(const FStringView& InStringView);

	static void GetFilteredVariableTypeTree(TArray<TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>>& TypeTree, ETypeTreeFilter TypeTreeFilter);

	static bool IsValidParameterNameString(FStringView InStringView, FText& OutErrorText);

	static bool IsValidParameterName(const FName InName, FText& OutErrorText);
};

}
