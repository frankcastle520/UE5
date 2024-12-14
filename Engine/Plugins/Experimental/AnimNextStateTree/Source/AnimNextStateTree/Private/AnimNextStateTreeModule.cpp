// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextStateTreeModule.h"

#include "StateTree.h"
#include "StateTreeReference.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "RigVMCore/RigVMRegistry.h"

namespace UE::AnimNext::StateTree
{

void FAnimNextStateTreeModule::ShutdownModule()
{
}

void FAnimNextStateTreeModule::StartupModule()
{
	static TPair<UClass*, FRigVMRegistry::ERegisterObjectOperation> const AllowedObjectTypes[] =
	{
		{ UStateTree::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
		{ UAnimNextAnimationGraph::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class }
	};

	FRigVMRegistry::Get().RegisterObjectTypes(AllowedObjectTypes);

	static UScriptStruct* const AllowedStructTypes[] =
	{
		FStateTreeReference::StaticStruct(),
	};

	FRigVMRegistry::Get().RegisterStructTypes(AllowedStructTypes);
}

IMPLEMENT_MODULE(FAnimNextStateTreeModule, AnimNextStateTree)

}