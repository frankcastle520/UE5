// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Graph/AnimNextGraphEntryPoint.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "RigVMCore/RigVM.h"
#include "TraitCore/TraitPtr.h"
#include "RigVMHost.h"

#include "AnimNextModule.generated.h"

class UEdGraph;
class UAnimNextModule;
class UAnimGraphNode_AnimNextGraph;
struct FAnimNode_AnimNextGraph;
struct FRigUnit_AnimNextGraphEvaluator;
struct FAnimNextGraphInstancePtr;
struct FAnimNextGraphInstance;
struct FAnimNextScheduleGraphTask;
struct FAnimNextEditorParam;
struct FAnimNextParam;

namespace UE::AnimNext
{
	struct FContext;
	struct FExecutionContext;
	class FAnimNextModuleImpl;
	struct FTestUtils;
	struct FParametersProxy;
}

namespace UE::AnimNext::UncookedOnly
{
	struct FUtils;
}

namespace UE::AnimNext::Editor
{
	class FModuleEditor;
	class FVariableCustomization;
}

UENUM()
enum class EAnimNextModuleInitMethod : uint8
{
	// Do not perform any initial update, set up data structures only
	None,

	// Set up data structures, perform an initial update and then pause
	InitializeAndPause,

	// Set up data structures, perform an initial update and then pause in editor only, otherwise act like InitializeAndRun
	InitializeAndPauseInEditor,

	// Set up data structures then continue updating
	InitializeAndRun
};

// Root asset represented by a component when instantiated
UCLASS(BlueprintType)
class ANIMNEXT_API UAnimNextModule : public UAnimNextDataInterface
{
	GENERATED_BODY()

public:
	UAnimNextModule(const FObjectInitializer& ObjectInitializer);

	// UObject interface
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;

#if WITH_EDITORONLY_DATA
	// Delegate called in editor when a module is compiled
	using FOnModuleCompiled = TTSMulticastDelegate<void(UAnimNextModule*)>;
	static FOnModuleCompiled& OnModuleCompiled();
#endif

protected:
	friend class UAnimNextModuleFactory;
	friend class UAnimNextModule_EditorData;
	friend struct UE::AnimNext::UncookedOnly::FUtils;
	friend class UE::AnimNext::Editor::FModuleEditor;
	friend struct UE::AnimNext::FTestUtils;
	friend FAnimNextGraphInstancePtr;
	friend FAnimNextGraphInstance;
	friend class UAnimGraphNode_AnimNextGraph;
	friend UE::AnimNext::FExecutionContext;
	friend struct FAnimNextScheduleGraphTask;
	friend UE::AnimNext::FAnimNextModuleImpl;
	friend class UE::AnimNext::Editor::FVariableCustomization;
	friend struct UE::AnimNext::FParametersProxy;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FAnimNextGraphState DefaultState_DEPRECATED;
	
	UPROPERTY()
	FInstancedPropertyBag PropertyBag_DEPRECATED;
#endif
};
