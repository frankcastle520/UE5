// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintEditorLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditor.h"
#include "AnimGraphNode_Base.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintNodeBinder.h"
#include "BlueprintTypePromotion.h"
#include "Components/TimelineComponent.h"
#include "Containers/EnumAsByte.h"
#include "Containers/Map.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "HAL/PlatformCrt.h"
#include "HAL/PlatformMath.h"
#include "Internationalization/Text.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_PromotableOperator.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompilerModule.h"
#include "Logging/LogCategory.h"
#include "Logging/StructuredLog.h"
#include "Math/Vector2D.h"
#include "Misc/AssertionMacros.h"
#include "PackageTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Templates/Casts.h"
#include "Templates/SubclassOf.h"
#include "Trace/Detail/Channel.h"
#include "UObject/Class.h"
#include "UObject/Field.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"
#include "UObject/UnrealNames.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "BlueprintEditorLibrary"

DEFINE_LOG_CATEGORY(LogBlueprintEditorLib);

///////////////////////////////////////////////////////////
// InternalBlueprintEditorLibrary

namespace InternalBlueprintEditorLibrary
{
	/**
	* Replace the OldNode with the NewNode and reconnect it's pins. If the pins don't
	* exist on the NewNode, then orphan the connections.
	*
	* @param OldNode		The old node to replace
	* @param NewNode		The new node to put in the old node's place
	*/
	static bool ReplaceOldNodeWithNew(UEdGraphNode* OldNode, UEdGraphNode* NewNode)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		
		bool bSuccess = false;

		if (Schema && OldNode && NewNode)
		{
			TMap<FName, FName> OldToNewPinMap;
			for (UEdGraphPin* Pin : OldNode->Pins)
			{
				if (Pin->ParentPin != nullptr)
				{
					// ReplaceOldNodeWithNew() will take care of mapping split pins (as long as the parents are properly mapped)
					continue;
				}
				else if (Pin->PinName == UEdGraphSchema_K2::PN_Self)
				{
					// there's no analogous pin, signal that we're expecting this
					OldToNewPinMap.Add(Pin->PinName, NAME_None);
				}
				else
				{
					// The input pins follow the same naming scheme
					OldToNewPinMap.Add(Pin->PinName, Pin->PinName);
				}
			}
			
			bSuccess = Schema->ReplaceOldNodeWithNew(OldNode, NewNode, OldToNewPinMap);
			// reconstructing the node will clean up any
			// incorrect default values that may have been copied over
			NewNode->ReconstructNode();			
		}

		return bSuccess;
	}

	/**
	* Returns true if any of these nodes pins have any links. Does not check for 
	* a default value on pins
	*
	* @param Node		The node to check
	*
	* @return bool		True if the node has any links, false otherwise.
	*/
	static bool NodeHasAnyConnections(const UEdGraphNode* Node)
	{
		if (Node)
		{
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->LinkedTo.Num() > 0)
				{
					return true;
				}
			}
		}

		return false;
	}

	/**
	* Attempt to close any open editors that may be relevant to this blueprint. This will prevent any 
	* problems where the user could see a previously deleted node/graph.
	*
	* @param Blueprint		The blueprint that is being edited
	*/
	static void CloseOpenEditors(UBlueprint* Blueprint)
	{
		UAssetEditorSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (AssetSubsystem && Blueprint)
		{
			AssetSubsystem->CloseAllEditorsForAsset(Blueprint);
		}
	}
};

///////////////////////////////////////////////////////////
// UBlueprintEditorLibrary

UBlueprintEditorLibrary::UBlueprintEditorLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void UBlueprintEditorLibrary::ReplaceVariableReferences(UBlueprint* Blueprint, const FName OldVarName, const FName NewVarName)
{
	if (!Blueprint || OldVarName.IsNone() || NewVarName.IsNone())
	{
		return;
	}

	FBlueprintEditorUtils::RenameVariableReferences(Blueprint, Blueprint->GeneratedClass, OldVarName, NewVarName);
}

UEdGraph* UBlueprintEditorLibrary::FindEventGraph(UBlueprint* Blueprint)
{
	return Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
}

UEdGraph* UBlueprintEditorLibrary::FindGraph(UBlueprint* Blueprint, FName GraphName)
{
	if (Blueprint && !GraphName.IsNone())
	{
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);

		for (UEdGraph* CurrentGraph : AllGraphs)
		{
			if (CurrentGraph->GetFName() == GraphName)
			{
				return CurrentGraph;
			}
		}
	}

	return nullptr;
}

UK2Node_PromotableOperator* CreateOpNode(const FName OpName, UEdGraph* Graph, const int32 AdditionalPins)
{
	if (!Graph)
	{
		return nullptr;
	}

	// The spawner will be null if type promo isn't enabled
	if (UBlueprintFunctionNodeSpawner* Spawner = FTypePromotion::GetOperatorSpawner(OpName))
	{
		// Spawn a new node!
		IBlueprintNodeBinder::FBindingSet Bindings;
		FVector2D SpawnLoc{};
		UK2Node_PromotableOperator* NewOpNode = Cast<UK2Node_PromotableOperator>(Spawner->Invoke(Graph, Bindings, SpawnLoc));
		check(NewOpNode);

		// Add the necessary number of additional pins
		for (int32 i = 0; i < AdditionalPins; ++i)
		{
			NewOpNode->AddInputPin();
		}

		return NewOpNode;
	}

	return nullptr;
}

void UBlueprintEditorLibrary::UpgradeOperatorNodes(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return;
	}

	if (!TypePromoDebug::IsTypePromoEnabled())
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Type Promotion is not enabled! Cannot upgrade operator nodes. Set 'BP.TypePromo.IsEnabled' to true and try again."));
		return;
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	Blueprint->Modify();

	/**
	* Used to help us restore the default values of any pins that may have changed their types
	* during replacement. 
	*/
	struct FRestoreDefaultsHelper
	{
		FEdGraphPinType PinType {};

		FString DefaultValue = TEXT("");

		TObjectPtr<UObject> DefaultObject = nullptr;

		FText DefaultTextValue = FText::GetEmpty();
	};

	TMap<FName, FRestoreDefaultsHelper> PinTypeMap;

	for (UEdGraph* Graph : AllGraphs)
	{
		check(Graph);

		Graph->Modify();

		for (int32 i = Graph->Nodes.Num() - 1; i >= 0; --i)
		{
			PinTypeMap.Reset();
			
			// Not every function that we want to upgrade is a CommunicativeBinaryOpNode
			// Some are just regular CallFunction nodes; Vector + Float is an example of this
			if (UK2Node_CallFunction* OldOpNode = Cast<UK2Node_CallFunction>(Graph->Nodes[i]))
			{
				UFunction* Func = OldOpNode->GetTargetFunction();
				UEdGraph* OwningGraph = OldOpNode->GetGraph();
				const bool bHadAnyConnections = InternalBlueprintEditorLibrary::NodeHasAnyConnections(OldOpNode);

				// We should only be modifying nodes within the graph that we want
				ensure(OwningGraph == Graph);
				
				// Don't bother with non-promotable functions or things that are already promotable operators
				if (!FTypePromotion::IsFunctionPromotionReady(Func) || OldOpNode->IsA<UK2Node_PromotableOperator>())
				{
					continue;
				}

				// Keep track of the types of anything with a default value so they can be restored
				for (UEdGraphPin* Pin : OldOpNode->Pins)
				{
					if (Pin->Direction == EGPD_Input && Pin->LinkedTo.IsEmpty())
					{
						FRestoreDefaultsHelper RestoreData;
						RestoreData.PinType = Pin->PinType;
						RestoreData.DefaultValue = Pin->DefaultValue;
						RestoreData.DefaultObject = Pin->DefaultObject;
						RestoreData.DefaultTextValue = Pin->DefaultTextValue;

						PinTypeMap.Add(Pin->GetFName(), RestoreData);
					}
				}

				FName OpName = FTypePromotion::GetOpNameFromFunction(Func);

				UK2Node_CommutativeAssociativeBinaryOperator* BinaryOpNode = Cast<UK2Node_CommutativeAssociativeBinaryOperator>(OldOpNode);

				// Spawn a new node!
				UK2Node_PromotableOperator* NewOpNode = CreateOpNode(
					OpName,
					OwningGraph,
					BinaryOpNode ? BinaryOpNode->GetNumberOfAdditionalInputs() : 0
				);

				// If there is a node that is a communicative op node but is not promotable
				// then the node will be null
				if (!NewOpNode)
				{
					UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Failed to spawn new operator node!"));
					continue;
				}

				NewOpNode->NodePosX = OldOpNode->NodePosX;
				NewOpNode->NodePosY = OldOpNode->NodePosY;

				InternalBlueprintEditorLibrary::ReplaceOldNodeWithNew(OldOpNode, NewOpNode);

				for (const TPair<FName, FRestoreDefaultsHelper>& Pair : PinTypeMap)
				{
					const FRestoreDefaultsHelper& OldPinData = Pair.Value;

					if (UEdGraphPin* Pin = NewOpNode->FindPin(Pair.Key))
					{
						if (NewOpNode->CanConvertPinType(Pin))
						{
							NewOpNode->ConvertPinType(Pin, OldPinData.PinType);
							Pin->DefaultValue = OldPinData.DefaultValue;
							Pin->DefaultObject = OldPinData.DefaultObject;
							Pin->DefaultTextValue = OldPinData.DefaultTextValue;
						}
					}
				}

				// Reset the new node to be wild card if there were no connections to the original node.
				// This is necessary because replacing the old node will attempt to reconcile any 
				// default values on the node, which can result in incorrect pin types and a default
				// value that doesn't match. 
				if(!bHadAnyConnections)
				{
					NewOpNode->ResetNodeToWildcard();
				}
			}
		}
	}
}

void UBlueprintEditorLibrary::CompileBlueprint(UBlueprint* Blueprint)
{
	if (Blueprint)
	{
		// Skip saving this to avoid possible tautologies when saving and allow the user to manually save
		EBlueprintCompileOptions Flags = EBlueprintCompileOptions::SkipSave;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, Flags);
	}
}

UEdGraph* UBlueprintEditorLibrary::AddFunctionGraph(UBlueprint* Blueprint, const FString& FuncName)
{
	if (!Blueprint)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Failed to add function graph, ensure that blueprint is not null!"));
		return nullptr;
	}

	// Validate that the given name is appropriate for a new function graph
	FName GraphName;

	if (FKismetNameValidator(Blueprint).IsValid(FuncName) == EValidatorResult::Ok)
	{
		GraphName = FName(*FuncName);
	}
	else
	{
		static const FString NewFunctionString = TEXT("NewFunction");
		GraphName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, !FuncName.IsEmpty() ? FuncName : NewFunctionString);
	}

	Blueprint->Modify();
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, 
		GraphName,
		UEdGraph::StaticClass(), 
		UEdGraphSchema_K2::StaticClass()
	);

	FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, NewGraph, /* bIsUserCreated = */ true, /* SignatureFromObject = */ nullptr);

	return NewGraph;
}

void UBlueprintEditorLibrary::RemoveFunctionGraph(UBlueprint* Blueprint, FName FuncName)
{
	if (!Blueprint)
	{
		return;
	}

	// Find the function graph of this name
	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == FuncName)
		{
			FunctionGraph = Graph;
			break;
		}
	}

	// Remove the function graph if we can
	if (FunctionGraph && FunctionGraph->bAllowDeletion)
	{
		Blueprint->Modify();
		InternalBlueprintEditorLibrary::CloseOpenEditors(Blueprint);
		FBlueprintEditorUtils::RemoveGraph(Blueprint, FunctionGraph, EGraphRemoveFlags::MarkTransient);
	}
	else
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Failed to remove function '%s' on blueprint '%s'!"), *FuncName.ToString(), *Blueprint->GetFriendlyName());
	}
}

void UBlueprintEditorLibrary::RemoveUnusedNodes(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return;
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	Blueprint->Modify();

	for (UEdGraph* Graph : AllGraphs)
	{
		// Skip non-editable graphs
		if (!Graph || FBlueprintEditorUtils::IsGraphReadOnly(Graph))
		{
			continue;
		}

		Graph->Modify();
		int32 NumNodesRemoved = 0;

		for (int32 i = Graph->Nodes.Num() - 1; i >= 0; --i)
		{
			UEdGraphNode* Node = Graph->Nodes[i];

			// We only want to delete user facing nodes because this is meant 
			// to be a BP refactoring/cleanup tool. Anim graph nodes can still 
			// be valid with no pin connections made to them
			if (Node->CanUserDeleteNode() && 
				!Node->IsA<UAnimGraphNode_Base>() && 
				!Node->IsA<UEdGraphNode_Comment>() &&
				!InternalBlueprintEditorLibrary::NodeHasAnyConnections(Node))
			{
				Node->BreakAllNodeLinks();
				Graph->RemoveNode(Node);
				++NumNodesRemoved;
			}
		}

		// Notify a change to the graph if nodes have been removed
		if (NumNodesRemoved > 0)
		{
			Graph->NotifyGraphChanged();
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
}

void UBlueprintEditorLibrary::RemoveGraph(UBlueprint* Blueprint, UEdGraph* Graph)
{
	if (!Blueprint || !Graph)
	{
		return;
	}

	InternalBlueprintEditorLibrary::CloseOpenEditors(Blueprint);
	FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::MarkTransient);
}

void UBlueprintEditorLibrary::RenameGraph(UEdGraph* Graph, const FString& NewNameStr)
{
	if (!Graph)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Invalid graph given, failed to rename!"));
		return;
	}
	
	// Validate that the given name is appropriate for a new function graph
	UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!BP)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Failed to find blueprint for graph!"));
		return;
	}

	FString ValidatedNewName;

	if (FKismetNameValidator(BP).IsValid(NewNameStr) == EValidatorResult::Ok)
	{
		ValidatedNewName = NewNameStr;
	}
	else
	{
		static const FString RenamedGraphString = TEXT("NewGraph");
		ValidatedNewName = FBlueprintEditorUtils::FindUniqueKismetName(BP, !NewNameStr.IsEmpty() ? NewNameStr : RenamedGraphString).ToString();
	}

	FBlueprintEditorUtils::RenameGraph(Graph, ValidatedNewName);
}

UBlueprint* UBlueprintEditorLibrary::GetBlueprintAsset(UObject* Object)
{
	return Cast<UBlueprint>(Object);
}

void UBlueprintEditorLibrary::RefreshOpenEditorsForBlueprint(const UBlueprint* BP)
{
	// Get any open blueprint editors for this asset and refresh them if they match the given blueprint
	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
	for (TSharedRef<IBlueprintEditor>& Editor : BlueprintEditorModule.GetBlueprintEditors())
	{
		if (TSharedPtr<FBlueprintEditor> BPEditor = StaticCastSharedPtr<FBlueprintEditor>(Editor.ToSharedPtr()))
		{
			if (BPEditor->GetBlueprintObj() == BP)
			{
				BPEditor->RefreshEditors();
			}
		}
	}
}

void UBlueprintEditorLibrary::RefreshAllOpenBlueprintEditors()
{
	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
	for (TSharedRef<IBlueprintEditor>& Editor : BlueprintEditorModule.GetBlueprintEditors())
	{
		Editor->RefreshEditors();
	}
}

void UBlueprintEditorLibrary::ReparentBlueprint(UBlueprint* Blueprint, UClass* NewParentClass)
{
	if (!Blueprint || !NewParentClass)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Failed to reparent blueprint!"));
		return;
	}

	if (NewParentClass == Blueprint->ParentClass)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("'%s' is already parented to class '%s'!"), *Blueprint->GetFriendlyName(), *NewParentClass->GetName());
		return;
	}

	// There could be possible data loss if reparenting outside the current class hierarchy
	if (!Blueprint->ParentClass || !NewParentClass->GetDefaultObject()->IsA(Blueprint->ParentClass))
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("'%s' class hierarchy is changing, there could be possible data loss!"), *Blueprint->GetFriendlyName());
	}

	Blueprint->ParentClass = NewParentClass;

	if (Blueprint->SimpleConstructionScript != nullptr)
	{
		Blueprint->SimpleConstructionScript->ValidateSceneRootNodes();
	}

	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	EBlueprintCompileOptions CompileOptions
	{
			EBlueprintCompileOptions::SkipSave
		|	EBlueprintCompileOptions::UseDeltaSerializationDuringReinstancing
		|	EBlueprintCompileOptions::SkipNewVariableDefaultsDetection
	};

	// If compilation is enabled during PIE/simulation, references to the CDO might be held by a script variable.
	// Thus, we set the flag to direct the compiler to allow those references to be replaced during reinstancing.
	if (GEditor && GEditor->PlayWorld != nullptr)
	{
		CompileOptions |= EBlueprintCompileOptions::IncludeCDOInReferenceReplacement;
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileOptions);
}

bool UBlueprintEditorLibrary::GatherUnusedVariables(const UBlueprint* Blueprint, TArray<FProperty*>& OutProperties)
{
	if (!Blueprint)
	{
		return false;
	}

	bool bHasAtLeastOneVariableToCheck = false;

	for (TFieldIterator<FProperty> PropertyIt(Blueprint->SkeletonGeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		// Don't show delegate properties, there is special handling for these
		const bool bDelegateProp = Property->IsA(FDelegateProperty::StaticClass()) || Property->IsA(FMulticastDelegateProperty::StaticClass());
		const bool bShouldShowProp = (!Property->HasAnyPropertyFlags(CPF_Parm) && Property->HasAllPropertyFlags(CPF_BlueprintVisible) && !bDelegateProp);

		if (bShouldShowProp)
		{
			bHasAtLeastOneVariableToCheck = true;
			FName VarName = Property->GetFName();

			const int32 VarInfoIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarName);
			const bool bHasVarInfo = (VarInfoIndex != INDEX_NONE);

			const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property);
			bool bIsTimeline = ObjectProperty &&
				ObjectProperty->PropertyClass &&
				ObjectProperty->PropertyClass->IsChildOf(UTimelineComponent::StaticClass());
			if (!bIsTimeline && bHasVarInfo && !FBlueprintEditorUtils::IsVariableUsed(Blueprint, VarName))
			{
				OutProperties.Add(Property);
			}
		}
	}

	return bHasAtLeastOneVariableToCheck;
}

int32 UBlueprintEditorLibrary::RemoveUnusedVariables(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return 0;
	}

	// Gather FProperties from this BP and see if we can remove any
	TArray<FProperty*> VariableProperties;
	UBlueprintEditorLibrary::GatherUnusedVariables(Blueprint, VariableProperties);
	
	// No variables can be removed from this blueprint
	if (VariableProperties.Num() == 0)
	{
		return 0;
	}

	// Get the variables by name so that we can bulk remove them and print them out to the log
	TArray<FName> VariableNames;
	FString PropertyList;
	VariableNames.Reserve(VariableProperties.Num());
	for (int32 Index = 0; Index < VariableProperties.Num(); ++Index)
	{
		VariableNames.Add(VariableProperties[Index]->GetFName());
		if (PropertyList.IsEmpty())
		{
			PropertyList = UEditorEngine::GetFriendlyName(VariableProperties[Index]);
		}
		else
		{
			PropertyList += FString::Printf(TEXT(", %s"), *UEditorEngine::GetFriendlyName(VariableProperties[Index]));
		}
	}

	const int32 NumRemovedVars = VariableNames.Num();
	// Remove the variables by name
	FBlueprintEditorUtils::BulkRemoveMemberVariables(Blueprint, VariableNames);

	UE_LOG(LogBlueprintEditorLib, Log, TEXT("The following variable(s) were deleted successfully: %s."), *PropertyList);
	return NumRemovedVars;
}

UClass* UBlueprintEditorLibrary::GeneratedClass(UBlueprint* BlueprintObj)
{
	if (BlueprintObj)
	{
		if(BlueprintObj->GeneratedClass == nullptr)
		{
			UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Blueprint %s does not have a generated class - consider compiling it"), *BlueprintObj->GetPathName());
			return nullptr;
		}
		return BlueprintObj->GeneratedClass->GetAuthoritativeClass();
	}
	return nullptr;
}

void UBlueprintEditorLibrary::SetBlueprintVariableExposeOnSpawn(UBlueprint* Blueprint, const FName& VariableName, bool bExposeOnSpawn)
{
	if (!Blueprint)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Invalid Blueprint!"));
		return;
	}
	
	if (VariableName == NAME_None)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Invalid variable name!"));
		return;
	}
	
	if(bExposeOnSpawn)
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VariableName, NULL, FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
	}
	else
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, VariableName, NULL, FBlueprintMetadata::MD_ExposeOnSpawn);
	} 
}

UBlueprint* UBlueprintEditorLibrary::CreateBlueprintAssetWithParent(const FString& AssetPath, UClass* ParentClass)
{
	if(!ParentClass)
	{
		UE_LOGFMT(LogBlueprintEditorLib, Warning, "Cannot create a blueprint asset with null parent class");
		return nullptr;
	}
	
	// do not allow inheritance of function library blueprints or native function libraries that already have functions
	// bIsValidFunctionLibrary provides a carve out for UEditorFunctionLibrary and similar sentinel types:
	const bool bIsFunctionLibrary = ParentClass->IsChildOf(UBlueprintFunctionLibrary::StaticClass());
	const bool bIsValidFunctionLibrary = bIsFunctionLibrary && (ParentClass->Children == nullptr && ParentClass->HasAnyClassFlags(CLASS_Native));
	if(bIsFunctionLibrary && !bIsValidFunctionLibrary)
	{
		UE_LOGFMT(LogBlueprintEditorLib, Warning, "Cannot create a blueprint asset from a function library: {ClassPath}", ParentClass->GetPathName());
		return nullptr;
	}

	// Validate base blueprint logic - this enforces 'blueprintable/notblueprintable'
	if(!bIsValidFunctionLibrary && !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		UE_LOGFMT(LogBlueprintEditorLib, Warning, "Not allowed to create blueprint for class: {ClassPath} - is it Blueprintable or IsBlueprintBase?", ParentClass->GetPathName());
		return nullptr;
	}

	// interface classes require special handling - reject them:
	if(ParentClass->HasAnyClassFlags(CLASS_Interface))
	{
		UE_LOGFMT(LogBlueprintEditorLib, Warning, "Cannot create a blueprint asset from an interface: {ClassPath}", ParentClass->GetPathName());
		return nullptr;
	}

	const FString PackageName = UPackageTools::SanitizePackageName(AssetPath);
	UPackage* Existing = FindObject<UPackage>(nullptr, *PackageName);
	if(Existing)
	{
		UE_LOGFMT(LogBlueprintEditorLib, Warning, "Cannot create a blueprint asset because an asset with this name already exists: {PackageName}", PackageName);
		return nullptr;
	}

	UPackage* Pkg = CreatePackage(*PackageName);
	if(!Pkg)
	{	
		UE_LOGFMT(LogBlueprintEditorLib, Warning, "Create Package Failed: {PackageName}", PackageName);
		return nullptr;
	}

	FName BPName = FPackageName::GetShortFName(PackageName);
	
	UClass* BlueprintClass = nullptr;
	UClass* BlueprintGeneratedClass = nullptr;
	IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler");
	KismetCompilerModule.GetBlueprintTypesForClass(ParentClass, BlueprintClass, BlueprintGeneratedClass);
	
	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass, 
		Pkg, 
		BPName, 
		bIsFunctionLibrary ? BPTYPE_FunctionLibrary : BPTYPE_Normal, 
		BlueprintClass, 
		BlueprintGeneratedClass);
	ensure(BP); // FKismetEditorUtilities::CreateBlueprint does not return null, if it does we should clean up the UPackage - somehow

	Pkg->SetIsExternallyReferenceable(true);

	// Notify the asset registry
	FAssetRegistryModule::AssetCreated(BP);

	// Mark the package dirty...
	Pkg->MarkPackageDirty();
	return BP;
}

void UBlueprintEditorLibrary::SetBlueprintVariableExposeToCinematics(UBlueprint* Blueprint, const FName& VariableName, bool bExposeToCinematics)
{
	if (!Blueprint)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Invalid Blueprint!"));
		return;
	}
	
	if (VariableName == NAME_None)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Invalid variable name!"));
		return;
	}

	FBlueprintEditorUtils::SetInterpFlag(Blueprint, VariableName, bExposeToCinematics);
}

void UBlueprintEditorLibrary::SetBlueprintVariableInstanceEditable(UBlueprint* Blueprint, const FName& VariableName, bool bInstanceEditable)
{
	if (!Blueprint)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Invalid Blueprint!"));
		return;
	}
	
	if (VariableName == NAME_None)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Invalid variable name!"));
		return;
	}
	
	FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VariableName, !bInstanceEditable);
}

bool UBlueprintEditorLibrary::Generic_AddMemberVariableWithValue(UBlueprint* Blueprint, FName MemberName, const uint8* DefaultValuePtr, const FProperty* DefaultValueProp)
{
	if(!Blueprint)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Attempted to add member variable to null blueprint"));
		return false;	
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	FEdGraphPinType PinType;
	if( !Schema->ConvertPropertyToPinType(DefaultValueProp, PinType) )
	{
		return false;
	}

	FName VarName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, MemberName.ToString(), Blueprint->SkeletonGeneratedClass);
	return FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, PinType);
}

DEFINE_FUNCTION(UBlueprintEditorLibrary::execAddMemberVariableWithValue)
{
	P_GET_OBJECT(UBlueprint, Blueprint);
	P_GET_PROPERTY(FNameProperty, MemberName);
	
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* DefaultValueProp = Stack.MostRecentProperty;
	const uint8* DefaultValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	if (!DefaultValueProp || !DefaultValuePtr)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AccessViolation,
			LOCTEXT("AddMemberVariable_MissingValue", "Failed to resolve default value and property type from AddMemberVariable.")
		);
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}

	*(bool*)RESULT_PARAM = P_THIS->Generic_AddMemberVariableWithValue(Blueprint, MemberName, DefaultValuePtr, DefaultValueProp);
}

bool UBlueprintEditorLibrary::AddMemberVariable(UBlueprint* Blueprint, FName MemberName, const FEdGraphPinType& VariableType)
{
	if(!Blueprint)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Attempted to add member variable to null blueprint"));
		return false;	
	}

	return FBlueprintEditorUtils::AddMemberVariable(
		Blueprint, 
		FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, MemberName.ToString(), Blueprint->SkeletonGeneratedClass),
		VariableType);
}

FEdGraphPinType UBlueprintEditorLibrary::GetBasicTypeByName(FName TypeName)
{
	FEdGraphPinType Result;
	const TSet<FName> PrimitiveTypes = {
		UEdGraphSchema_K2::PC_Boolean, 
		UEdGraphSchema_K2::PC_Byte,
		UEdGraphSchema_K2::PC_Int,
		UEdGraphSchema_K2::PC_Int64,
		UEdGraphSchema_K2::PC_Real,
		UEdGraphSchema_K2::PC_Name,
		UEdGraphSchema_K2::PC_String,
		UEdGraphSchema_K2::PC_Text,
	};
	if(!PrimitiveTypes.Contains(TypeName))
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Primitive type: %s not recognized, defaulting to int"), *TypeName.ToString());
		TypeName = UEdGraphSchema_K2::PC_Int;
	}
	Result.PinCategory = TypeName;
	if(TypeName == UEdGraphSchema_K2::PC_Real)
	{
		Result.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	return Result;
}
	
FEdGraphPinType UBlueprintEditorLibrary::GetStructType(const UScriptStruct* StructType)
{
	if(	StructType == nullptr || 
		!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(StructType))
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Struct type: %s not allowed, defaulting to int"), StructType ? *StructType->GetPathName() : TEXT("null"));
		return GetBasicTypeByName(UEdGraphSchema_K2::PC_Int);
	}
	
	FEdGraphPinType Result;
	Result.PinCategory = UEdGraphSchema_K2::PC_Struct;
	// the struct here is notionally const, and via PinSubCategoryObject should be extremely rare if they exist:
	Result.PinSubCategoryObject = const_cast<UScriptStruct*>(StructType);
	return Result;
}
	
FEdGraphPinType UBlueprintEditorLibrary::GetClassReferenceType(const UClass* ClassType)
{
	if(	ClassType == nullptr || 
		!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(ClassType))
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Class type: %s not allowed, defaulting to int"), ClassType ? *ClassType->GetPathName() : TEXT("null"));
		return GetBasicTypeByName(UEdGraphSchema_K2::PC_Int);
	}
	
	FEdGraphPinType Result;
	Result.PinCategory = UEdGraphSchema_K2::PC_Class;
	// the class here is notionally const, and via PinSubCategoryObject should be extremely rare if they exist:
	Result.PinSubCategoryObject = const_cast<UClass*>(ClassType);
	return Result;
}
	
FEdGraphPinType UBlueprintEditorLibrary::GetObjectReferenceType(const UClass* ObjectType)
{
	if(	ObjectType == nullptr || 
		!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(ObjectType))
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Object reference type: %s not allowed, defaulting to int"), ObjectType ? *ObjectType->GetPathName() : TEXT("null"));
		return GetBasicTypeByName(UEdGraphSchema_K2::PC_Int);
	}
	
	FEdGraphPinType Result;
	Result.PinCategory = UEdGraphSchema_K2::PC_Object;
	// the struct here is notionally const, and via PinSubCategoryObject should be extremely rare if they exist:
	Result.PinSubCategoryObject = const_cast<UClass*>(ObjectType);
	return Result;
}
	
FEdGraphPinType UBlueprintEditorLibrary::GetArrayType(const FEdGraphPinType& ContainedType)
{
	if(ContainedType.IsContainer())
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Containers cannot be nested directly, an intermediate struct type must be created. Defaulting to int"));
		return GetBasicTypeByName(UEdGraphSchema_K2::PC_Int);
	}

	FEdGraphPinType Result = ContainedType;
	Result.ContainerType = EPinContainerType::Array;
	return Result;
}
	
FEdGraphPinType UBlueprintEditorLibrary::GetSetType(const FEdGraphPinType& ContainedType)
{
	if(ContainedType.IsContainer())
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Containers cannot be nested directly, an intermediate struct type must be created. Defaulting to int"));
		return GetBasicTypeByName(UEdGraphSchema_K2::PC_Int);
	}

	if(!FBlueprintEditorUtils::HasGetTypeHash(ContainedType))
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Key type must be hashable. Defaulting to int"));
		return GetBasicTypeByName(UEdGraphSchema_K2::PC_Int);
	}

	FEdGraphPinType Result= ContainedType;
	Result.ContainerType = EPinContainerType::Set;
	return Result;
}
	
FEdGraphPinType UBlueprintEditorLibrary::GetMapType(const FEdGraphPinType& KeyType,const FEdGraphPinType& ValueType)
{
	if(KeyType.IsContainer())
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Containers cannot be used as a key type, an intermediate struct type must be created. Defaulting to int"));
		return GetBasicTypeByName(UEdGraphSchema_K2::PC_Int);
	}

	if(ValueType.IsContainer())
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Containers cannot be as a value type, an intermediate struct type must be created. Defaulting to int"));
		return GetBasicTypeByName(UEdGraphSchema_K2::PC_Int);
	}

	if(!FBlueprintEditorUtils::HasGetTypeHash(KeyType))
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Key type must be hashable. Defaulting to int"));
		return GetBasicTypeByName(UEdGraphSchema_K2::PC_Int);
	}

	FEdGraphPinType Result = KeyType;
	Result.ContainerType = EPinContainerType::Map;
	Result.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
	return Result;
}

#undef LOCTEXT_NAMESPACE	// "BlueprintEditorLibrary"