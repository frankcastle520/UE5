// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextController.h"
#include "DataInterface/AnimNextDataInterface_EditorData.h"
#include "AnimNextExecuteContext.h"

#include "AnimNextAnimationGraph_EditorData.generated.h"

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

/** Editor data for AnimNext animation graphs */
UCLASS()
class ANIMNEXTUNCOOKEDONLY_API UAnimNextAnimationGraph_EditorData : public UAnimNextDataInterface_EditorData
{
	GENERATED_BODY()

	friend class UAnimNextAnimationGraphFactory;
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

protected:
	// IRigVMClientHost interface
	virtual void RecompileVM() override;

	// UAnimNextRigVMAssetEditorData interface
	virtual TSubclassOf<URigVMController> GetControllerClass() const override { return UAnimNextController::StaticClass(); }
	virtual UScriptStruct* GetExecuteContextStruct() const override { return FAnimNextExecuteContext::StaticStruct(); }
	virtual TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> GetEntryClasses() const override;
	virtual bool CanAddNewEntry(TSubclassOf<UAnimNextRigVMAssetEntry> InClass) const override;
};
