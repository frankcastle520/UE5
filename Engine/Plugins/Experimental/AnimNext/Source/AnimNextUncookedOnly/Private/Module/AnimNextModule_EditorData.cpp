// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/AnimNextModule_EditorData.h"

#include "ExternalPackageHelper.h"
#include "UncookedOnlyUtils.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Module/AnimNextModule.h"
#include "Entries/AnimNextAnimationGraphEntry.h"
#include "Entries/AnimNextEventGraphEntry.h"
#include "AnimNextEventGraphSchema.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Graph/AnimNextAnimationGraphSchema.h"
#include "RigVMModel/Nodes/RigVMAggregateNode.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/LinkerLoad.h"
#include "Variables/AnimNextUniversalObjectLocatorBindingData.h"

void UAnimNextModule_EditorData::RecompileVM()
{
	Super::RecompileVM();

	if (bIsCompiling)
	{
		return;
	}

#if WITH_EDITOR
	UAnimNextModule::OnModuleCompiled().Broadcast(UE::AnimNext::UncookedOnly::FUtils::GetAsset<UAnimNextModule>(this));
#endif
}

void UAnimNextModule_EditorData::PostLoad()
{
	Super::PostLoad();

	auto FindEntryForRigVMGraph = [this](URigVMGraph* InRigVMGraph)
	{
		UAnimNextRigVMAssetEntry* FoundEntry = nullptr;
		for(UAnimNextRigVMAssetEntry* Entry : Entries)
		{
			if(IAnimNextRigVMGraphInterface* GraphEntry = Cast<IAnimNextRigVMGraphInterface>(Entry))
			{
				if(InRigVMGraph == GraphEntry->GetRigVMGraph())
				{
					FoundEntry = Entry;
					break;
				}
			}
		}
		return FoundEntry;
	};

	if(GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextCombineGraphContexts)
	{
		// Must preload entries so their data is populated or we cannot find the appropriate entries for graphs
		for(UAnimNextRigVMAssetEntry* Entry : Entries) 
		{
			Entry->GetLinker()->Preload(Entry);
		}

		TArray<URigVMGraph*> AllModels = RigVMClient.GetAllModels(false, true);
		for(URigVMGraph* Graph : AllModels)
		{
			Graph->SetExecuteContextStruct(FAnimNextExecuteContext::StaticStruct());
			if(UAnimNextRigVMAssetEntry* FoundEntry = FindEntryForRigVMGraph(Graph))
			{
				if(FoundEntry->IsA(UAnimNextAnimationGraphEntry::StaticClass()))
				{
					Graph->SetSchemaClass(UAnimNextAnimationGraphSchema::StaticClass());
				}
				else
				{
					Graph->SetSchemaClass(UAnimNextEventGraphSchema::StaticClass());
				}
			}
			else
			{
				Graph->SetSchemaClass(UAnimNextAnimationGraphSchema::StaticClass());
			}
		}
	}

	if(GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextMoveGraphsToEntries)
	{
		// Must preload entries so their data is populated or we cannot find the appropriate entries for graphs
		for(UAnimNextRigVMAssetEntry* Entry : Entries) 
		{
			Entry->GetLinker()->Preload(Entry);
		}
		
		for(TObjectPtr<UAnimNextEdGraph> Graph : Graphs_DEPRECATED)
		{
			URigVMGraph* FoundRigVMGraph = GetRigVMGraphForEditorObject(Graph);
			if(FoundRigVMGraph)
			{
				if(UAnimNextRigVMAssetEntry* FoundEntry = FindEntryForRigVMGraph(FoundRigVMGraph))
				{
					if(UAnimNextAnimationGraphEntry* AnimationGraphEntry = Cast<UAnimNextAnimationGraphEntry>(FoundEntry))
					{
						AnimationGraphEntry->EdGraph = Graph;
					}
					else if(UAnimNextEventGraphEntry* EventGraphEntry = Cast<UAnimNextEventGraphEntry>(FoundEntry))
					{
						EventGraphEntry->EdGraph = Graph;
					}

					Graph->Rename(nullptr, FoundEntry, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
					Graph->Initialize(this);
				}
			}
		}

		// We used to add a default model that is no longer needed
		URigVMGraph* DefaultModel = RigVMClient.GetDefaultModel();
		if(DefaultModel && DefaultModel->GetName() == TEXT("RigVMGraph"))
		{
			bool bFound = false;
			for(UAnimNextRigVMAssetEntry* Entry : Entries)
			{
				if(UAnimNextEventGraphEntry* EventGraphEntry = Cast<UAnimNextEventGraphEntry>(Entry))
				{
					if(DefaultModel == static_cast<IAnimNextRigVMGraphInterface*>(EventGraphEntry)->GetRigVMGraph())
					{
						bFound = true;
						break;
					}
				}
			}

			if(!bFound)
			{
				TGuardValue<bool> DisablePythonPrint(bSuspendPythonMessagesForRigVMClient, false);
				TGuardValue<bool> DisableAutoCompile(bAutoRecompileVM, false);
				RigVMClient.RemoveModel(DefaultModel->GetNodePath(), false);
			}
		}

		RecompileVM();
	}

	if(GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextGraphAccessSpecifiers)
	{
		// Must preload entries so their data is populated as we will be modifying them
		for(UAnimNextRigVMAssetEntry* Entry : Entries) 
		{
			Entry->GetLinker()->Preload(Entry);
		}

		// Force older assets to all have public symbols so they work as-is. Newer assets need user intervention as entries default to private
		for(UAnimNextRigVMAssetEntry* Entry : Entries)
		{
			if(UAnimNextAnimationGraphEntry* AnimationGraphEntry = Cast<UAnimNextAnimationGraphEntry>(Entry))
			{
				AnimationGraphEntry->Access = EAnimNextExportAccessSpecifier::Public;
			}
			else if(UAnimNextVariableEntry* ParameterEntry = Cast<UAnimNextVariableEntry>(Entry))
			{
				ParameterEntry->Access = EAnimNextExportAccessSpecifier::Public;
			}
		}
	}
}

TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> UAnimNextModule_EditorData::GetEntryClasses() const
{
	static const TSubclassOf<UAnimNextRigVMAssetEntry> Classes[] =
	{
		UAnimNextAnimationGraphEntry::StaticClass(),	// TEMP: Remove this once we have ported all old data
		UAnimNextEventGraphEntry::StaticClass(),
		UAnimNextVariableEntry::StaticClass(),
		UAnimNextDataInterfaceEntry::StaticClass(),
	};
	
	return Classes;
}

void UAnimNextModule_EditorData::GetProgrammaticGraphs(const FRigVMCompileSettings& InSettings, TArray<URigVMGraph*>& OutGraphs)
{
	using namespace UE::AnimNext::UncookedOnly;

	FUtils::CompileVariableBindings(InSettings, FUtils::GetAsset<UAnimNextModule>(this), OutGraphs);
}

void UAnimNextModule_EditorData::CustomizeNewAssetEntry(UAnimNextRigVMAssetEntry* InNewEntry) const
{
	Super::CustomizeNewAssetEntry(InNewEntry);
	
	UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(InNewEntry);
	if(VariableEntry == nullptr)
	{
		return;
	}

	VariableEntry->SetBindingType(FAnimNextUniversalObjectLocatorBindingData::StaticStruct(), false);
}