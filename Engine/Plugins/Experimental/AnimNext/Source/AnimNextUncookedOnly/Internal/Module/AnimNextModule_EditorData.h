// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextController.h"
#include "RigVMModel/RigVMGraph.h"
#include "AnimNextEdGraph.h"
#include "DataInterface/AnimNextDataInterface_EditorData.h"
#include "AnimNextExecuteContext.h"
#include "AnimNextModule_EditorData.generated.h"

class UAnimNextModule;
class UAnimNextModule_FunctionGraph;
enum class ERigVMGraphNotifType : uint8;
class FAnimationAnimNextRuntimeTest_GraphAddTrait;
class FAnimationAnimNextRuntimeTest_GraphExecute;
class FAnimationAnimNextRuntimeTest_GraphExecuteLatent;

namespace UE::AnimNext::UncookedOnly
{
	struct FUtils;
}

namespace UE::AnimNext::Editor
{
	class FModuleEditor;
	class SAnimNextGraphView;
	struct FUtils;
}

/** Editor data for AnimNext modules */
UCLASS(MinimalAPI)
class UAnimNextModule_EditorData : public UAnimNextDataInterface_EditorData
{
	GENERATED_BODY()

	friend class UAnimNextModuleFactory;
	friend class UAnimNextEdGraph;
	friend struct UE::AnimNext::UncookedOnly::FUtils;
	friend struct UE::AnimNext::Editor::FUtils;
	friend class UE::AnimNext::Editor::FModuleEditor;
	friend class UE::AnimNext::Editor::SAnimNextGraphView;
	friend struct FAnimNextGraphSchemaAction_RigUnit;
	friend struct FAnimNextGraphSchemaAction_DispatchFactory;
	friend class FAnimationAnimNextEditorTest_GraphAddTrait;
	friend class FAnimationAnimNextEditorTest_GraphTraitOperations;
	friend class FAnimationAnimNextRuntimeTest_GraphExecute;
	friend class FAnimationAnimNextRuntimeTest_GraphExecuteLatent;

private:
	// UObject interface
	virtual void PostLoad() override;

	// UAnimNextRigVMAssetEditorData interface
	virtual TSubclassOf<URigVMController> GetControllerClass() const override { return UAnimNextController::StaticClass(); }
	virtual UScriptStruct* GetExecuteContextStruct() const override { return FAnimNextExecuteContext::StaticStruct(); }
	virtual TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> GetEntryClasses() const override;
	virtual void RecompileVM() override;
	virtual void GetProgrammaticGraphs(const FRigVMCompileSettings& InSettings, TArray<URigVMGraph*>& OutGraphs) override;
	virtual void CustomizeNewAssetEntry(UAnimNextRigVMAssetEntry* InNewEntry) const override;

private:
	UPROPERTY()
	TArray<TObjectPtr<UAnimNextEdGraph>> Graphs_DEPRECATED;
};
