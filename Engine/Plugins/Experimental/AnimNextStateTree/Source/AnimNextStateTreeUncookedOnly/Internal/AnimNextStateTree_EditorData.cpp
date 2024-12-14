// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextStateTree_EditorData.h"

#include "AnimNextStateTree.h"
#include "AnimNextStateTreeWorkspaceAssetUserData.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Entries/AnimNextAnimationGraphEntry.h"
#include "Graph/AnimNextAnimationGraphSchema.h"
#include "Graph/RigUnit_AnimNextGraphRoot.h"
#include "Graph/RigDecorator_AnimNextCppTrait.h"
#include "Graph/RigUnit_AnimNextTraitStack.h"
#include "TraitCore/TraitRegistry.h"
#include "TraitCore/Trait.h"
#include "AnimStateTreeTrait.h"
#include "Traits/BlendStackTrait.h"

TSubclassOf<UAssetUserData> UAnimNextStateTree_EditorData::GetAssetUserDataClass() const
{
	return UAnimNextStateTreeWorkspaceAssetUserData::StaticClass();
}

void UAnimNextStateTree_EditorData::RecompileVM()
{
	UAnimNextAnimationGraph_EditorData::RecompileVM();

	UAnimNextStateTree* AnimationStateTree = UE::AnimNext::UncookedOnly::FUtils::GetAsset<UAnimNextStateTree>(this);

	UStateTree* InnerStateTree = AnimationStateTree->StateTree;

	UStateTreeEditorData* InnerEditorData = Cast<UStateTreeEditorData>(InnerStateTree->EditorData);
	InnerEditorData->RootParameters.ResetParametersAndOverrides();
	InnerEditorData->RootParameters.Parameters = AnimationStateTree->VariableDefaults;
}

TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> UAnimNextStateTree_EditorData::GetEntryClasses() const
{
	static const TSubclassOf<UAnimNextRigVMAssetEntry> Classes[] =
{
		UAnimNextVariableEntry::StaticClass(),
		UAnimNextAnimationGraphEntry::StaticClass(),
	};

	return Classes;
}

void UAnimNextStateTree_EditorData::GetProgrammaticGraphs(const FRigVMCompileSettings& InSettings, TArray<URigVMGraph*>& OutGraphs)
{
	if (UAnimNextStateTree* AnimStateTree = UE::AnimNext::UncookedOnly::FUtils::GetAsset<UAnimNextStateTree>(this))
	{
		URigVMGraph* Graph = NewObject<URigVMGraph>(this, NAME_None, RF_Transient);
		Graph->SetSchemaClass(UAnimNextAnimationGraphSchema::StaticClass());

		UAnimNextController* Controller = CastChecked<UAnimNextController>(RigVMClient.GetOrCreateController(Graph));
		UE::AnimNext::UncookedOnly::FUtils::SetupAnimGraph(FRigUnit_AnimNextGraphRoot::DefaultEntryPoint, Controller);

		if (Controller->GetGraph()->GetNodes().Num() != 1)
		{
			InSettings.ReportError(TEXT("Expected singular FRigUnit_AnimNextGraphRoot node"));
			return;
		}
		
		URigVMNode* EntryNode = Controller->GetGraph()->GetNodes()[0];			
		URigVMPin* BeginExecutePin = EntryNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_AnimNextGraphRoot, Result));

		if (BeginExecutePin == nullptr)
		{
			InSettings.ReportError(TEXT("Failed to retrieve Result pin from FRigUnit_AnimNextGraphRoot node"));
			return;
		}

		URigVMUnitNode* TraitStackNode = Controller->AddUnitNode(FRigUnit_AnimNextTraitStack::StaticStruct(), FRigVMStruct::ExecuteName, FVector2D(-800.0f, 0.0f), FString(), false);

		if (TraitStackNode == nullptr)
		{
			InSettings.ReportError(TEXT("Failed to spawn FRigUnit_AnimNextTraitStack node"));
			return;
		}

		const FName BlendStackTraitName = Controller->AddTraitByName(TraitStackNode->GetFName(), *UE::AnimNext::FTraitRegistry::Get().Find(UE::AnimNext::FBlendStackCoreTrait::TraitUID)->GetTraitName(), INDEX_NONE);
		
		if (BlendStackTraitName == NAME_None)
		{
			InSettings.ReportError(TEXT("Failed to add BlendStack trait to node"));
			return;
		}
		
		const FName StateTreeTraitName = Controller->AddTraitByName(TraitStackNode->GetFName(), *UE::AnimNext::FTraitRegistry::Get().Find(UE::AnimNext::FStateTreeTrait::TraitUID)->GetTraitName(), INDEX_NONE);

		if (StateTreeTraitName == NAME_None)
		{
			InSettings.ReportError(TEXT("Failed to add StateTree trait to node"));
			return;
		}

		URigVMPin* StateTreeReferencePin = TraitStackNode->FindTrait(StateTreeTraitName, GET_MEMBER_NAME_STRING_CHECKED(FAnimNextStateTreeTraitSharedData, StateTreeReference));
		if (StateTreeReferencePin == nullptr)
		{
			InSettings.ReportError(TEXT("Failed to retrieve StateTreeReference pin"));
			return;
		}
		
		FStateTreeReference Ref;
		Ref.SetStateTree(AnimStateTree->StateTree);

		FString PinValue;
		FStateTreeReference::StaticStruct()->ExportText(PinValue, &Ref, nullptr, nullptr, 0, nullptr);
		Controller->SetPinDefaultValue(StateTreeReferencePin->GetPinPath(), PinValue);
		
		URigVMPin* TraitResult = TraitStackNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_AnimNextTraitStack, Result));
		if (TraitResult == nullptr)
		{
			InSettings.ReportError(TEXT("Failed to retrieve Result pin"));
			return;
		}

		if (!Controller->AddLink(TraitResult, BeginExecutePin, false))
		{
			InSettings.ReportError(TEXT("Failed to link TraitStack and Graph Output pins"));
			return;
		}
		
		OutGraphs.Add(Graph);	
	}
}
