// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/AnimNextAnimationGraphFactory.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/AnimNextAnimationGraph_EditorData.h"
#include "UncookedOnlyUtils.h"
#include "Entries/AnimNextAnimationGraphEntry.h"

UAnimNextAnimationGraphFactory::UAnimNextAnimationGraphFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UAnimNextAnimationGraph::StaticClass();
}

bool UAnimNextAnimationGraphFactory::ConfigureProperties()
{
	return true;
}

UObject* UAnimNextAnimationGraphFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	EObjectFlags FlagsToUse = Flags | RF_Public | RF_Standalone | RF_Transactional | RF_LoadCompleted;
	if(InParent == GetTransientPackage())
	{
		FlagsToUse &= ~RF_Standalone;
	}

	UAnimNextAnimationGraph* NewAnimationGraph = NewObject<UAnimNextAnimationGraph>(InParent, Class, Name, FlagsToUse);

	// Create internal editor data
	UAnimNextAnimationGraph_EditorData* EditorData = NewObject<UAnimNextAnimationGraph_EditorData>(NewAnimationGraph, TEXT("EditorData"), RF_Transactional);
	NewAnimationGraph->EditorData = EditorData;
	EditorData->Initialize(/*bRecompileVM*/false);

	// Add a single internal graph entry
	UAnimNextAnimationGraphEntry* NewGraph = EditorData->AddAnimationGraph(FRigUnit_AnimNextGraphRoot::DefaultEntryPoint);

	// Hide in the outliner - the asset itself represents the graph to users
	NewGraph->SetHiddenInOutliner(true);

	// Compile the initial skeleton
	EditorData->RecompileVM();
	check(!EditorData->bErrorsDuringCompilation);

	return NewAnimationGraph;
}