// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/AnimNextGraphInstancePtr.h"

#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/AnimNextGraphInstance.h"

FAnimNextGraphInstancePtr::FAnimNextGraphInstancePtr() = default;

FAnimNextGraphInstancePtr::~FAnimNextGraphInstancePtr()
{
	Release();
}

void FAnimNextGraphInstancePtr::Release()
{
	Impl.Reset();
}

bool FAnimNextGraphInstancePtr::IsValid() const
{
	return Impl && Impl->IsValid();
}

const UAnimNextAnimationGraph* FAnimNextGraphInstancePtr::GetAnimationGraph() const
{
	return Impl->GetAnimationGraph();
}

UE::AnimNext::FWeakTraitPtr FAnimNextGraphInstancePtr::GetGraphRootPtr() const
{
	return Impl ? Impl->GetGraphRootPtr() : UE::AnimNext::FWeakTraitPtr();
}

FAnimNextGraphInstance* FAnimNextGraphInstancePtr::GetImpl() const
{
	return Impl.Get();
}

bool FAnimNextGraphInstancePtr::UsesAnimationGraph(const UAnimNextAnimationGraph* InAnimationGraph) const
{
	return Impl ? Impl->UsesAnimationGraph(InAnimationGraph) : false;
}

bool FAnimNextGraphInstancePtr::IsRoot() const
{
	return Impl ? Impl->IsRoot() : true;
}

bool FAnimNextGraphInstancePtr::HasUpdated() const
{
	return Impl ? Impl->HasUpdated() : false;
}

void FAnimNextGraphInstancePtr::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	if (Impl)
	{
		FAnimNextGraphInstance* ImplPtr = Impl.Get();
		Collector.AddPropertyReferences(FAnimNextGraphInstance::StaticStruct(), ImplPtr);
		Impl->AddStructReferencedObjects(Collector);
	}
}

UE::AnimNext::FGraphInstanceComponent* FAnimNextGraphInstancePtr::TryGetComponent(int32 ComponentNameHash, FName ComponentName) const
{
	return Impl ? Impl->TryGetComponent(ComponentNameHash, ComponentName) : nullptr;
}

UE::AnimNext::FGraphInstanceComponent& FAnimNextGraphInstancePtr::AddComponent(int32 ComponentNameHash, FName ComponentName, TSharedPtr<UE::AnimNext::FGraphInstanceComponent>&& Component)
{
	check(Impl);
	return Impl->AddComponent(ComponentNameHash, ComponentName, MoveTemp(Component));
}

GraphInstanceComponentMapType::TConstIterator FAnimNextGraphInstancePtr::GetComponentIterator() const
{
	check(Impl);
	return Impl->GetComponentIterator();
}

void FAnimNextGraphInstancePtr::Update() const
{
	check(Impl);
	Impl->Update();
}

FRigVMExtendedExecuteContext& FAnimNextGraphInstancePtr::GetExtendedExecuteContext() const
{
	check(Impl);
	return Impl->GetExtendedExecuteContext();
}

bool FAnimNextGraphInstancePtr::RequiresPublicVariableBinding() const
{
	check(Impl);
	return Impl->PublicVariablesState == FAnimNextGraphInstance::EPublicVariablesState::Unbound;
}

void FAnimNextGraphInstancePtr::BindPublicVariables(TConstArrayView<UE::AnimNext::IDataInterfaceHost*> InHosts) const
{
	check(Impl);
	return Impl->BindPublicVariables(InHosts);
}
