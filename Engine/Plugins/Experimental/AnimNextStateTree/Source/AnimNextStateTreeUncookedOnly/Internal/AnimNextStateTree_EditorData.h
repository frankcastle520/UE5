// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Graph/AnimNextAnimationGraph_EditorData.h"

#include "AnimNextStateTree_EditorData.generated.h"

UCLASS()
class ANIMNEXTSTATETREEUNCOOKEDONLY_API UAnimNextStateTree_EditorData : public UAnimNextAnimationGraph_EditorData
{
	GENERATED_BODY()
	
protected:
	virtual TSubclassOf<UAssetUserData> GetAssetUserDataClass() const override;

	friend class UAnimNextStateTreeFactory;
	// IRigVMClientHost interface
	virtual void RecompileVM() override;
	
	// UAnimNextRigVMAssetEditorData interface
	virtual TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> GetEntryClasses() const override;

	// Allows this asset to generate graphs to be injected at compilation time
	virtual void GetProgrammaticGraphs(const FRigVMCompileSettings& InSettings, TArray<URigVMGraph*>& OutGraphs) override;
};