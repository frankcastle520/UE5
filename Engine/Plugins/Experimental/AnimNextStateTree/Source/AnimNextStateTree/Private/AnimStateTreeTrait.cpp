// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimStateTreeTrait.h"

#include "AnimNextStateTreeContext.h"
#include "AnimNextStateTreeSchema.h"
#include "StateTreeExecutionContext.h"
#include "Graph/AnimNextGraphInstance.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "TraitCore/NodeInstance.h"

namespace UE::AnimNext
{
	AUTO_REGISTER_ANIM_TRAIT(FStateTreeTrait)

	// Trait implementation boilerplate
	#define TRAIT_INTERFACE_ENUMERATOR(GeneratorMacro) \
	GeneratorMacro(IUpdate) \
	GeneratorMacro(IGarbageCollection) \

	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FStateTreeTrait, TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)
	#undef TRAIT_INTERFACE_ENUMERATOR


	void FStateTreeTrait::FInstanceData::Construct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Construct(Context, Binding);
		IGarbageCollection::RegisterWithGC(Context, Binding);
	}

	void FStateTreeTrait::FInstanceData::Destruct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Destruct(Context, Binding);
		IGarbageCollection::UnregisterWithGC(Context, Binding);
	}

	void FStateTreeTrait::OnBecomeRelevant(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		InstanceData->StateTree = SharedData->StateTreeReference.GetStateTree();

		if (InstanceData->StateTree)
		{
			UObject* Owner = GetTransientPackage();
			FStateTreeExecutionContext StateTreeExecutionContext(*Owner, *InstanceData->StateTree, InstanceData->InstanceData);

			FAnimNextStateTreeTraitContext TraitContext(Context, Binding.GetStack());
			StateTreeExecutionContext.SetContextDataByName(UStateTreeAnimNextSchema::AnimStateTreeExecutionContextName, FStateTreeDataView(FAnimNextStateTreeTraitContext::StaticStruct(), reinterpret_cast<uint8*>(&TraitContext)));

			FAnimNextGraphInstance* OwnerGraphInstance = &Binding.GetTraitPtr().GetNodeInstance()->GetOwner();
			if(OwnerGraphInstance && InstanceData->StateTree->GetDefaultParameters().Identical(&OwnerGraphInstance->GetVariables(), 0))
			{
				const FInstancedPropertyBag& StateTreeParameters = InstanceData->StateTree->GetDefaultParameters();
				FStructView MutableVariables = OwnerGraphInstance->GetVariables().GetMutableValue();
				
				const FRigVMExtendedExecuteContext& ExtendedExecuteContext = OwnerGraphInstance->GetExtendedExecuteContext();

				// This copy-behaviour is temporary until we find a better way to directly patch the StateTree property binding/copies
				const int32 NumVariables = StateTreeParameters.GetNumPropertiesInBag();
				for(int32 VariableIndex = 0; VariableIndex < NumVariables; ++VariableIndex)
				{
					if (ExtendedExecuteContext.ExternalVariableRuntimeData.IsValidIndex(VariableIndex))
					{
						const FPropertyBagPropertyDesc& Desc = StateTreeParameters.GetPropertyBagStruct()->GetPropertyDescs()[VariableIndex];
						void* TargetAddress = MutableVariables.GetMemory() + Desc.CachedProperty->GetOffset_ForInternal();
						void const* SourceAddress = ExtendedExecuteContext.ExternalVariableRuntimeData[VariableIndex].Memory;

						Desc.CachedProperty->CopyCompleteValue(TargetAddress, SourceAddress);
					}
				}
				
				StateTreeExecutionContext.SetCollectExternalDataCallback(FOnCollectStateTreeExternalData::CreateLambda([this, &TraitContext]( const FStateTreeExecutionContext& Context, const UStateTree* StateTree, TArrayView<const FStateTreeExternalDataDesc> ExternalDataDescs, TArrayView<FStateTreeDataView> OutDataViews) -> bool
				{
					for (int32 Index = 0; Index < ExternalDataDescs.Num(); Index++)
					{
						const FStateTreeExternalDataDesc& ItemDesc = ExternalDataDescs[Index];
						if (ItemDesc.Struct != nullptr)
						{
							if (ItemDesc.Struct->IsChildOf(FAnimNextStateTreeTraitContext::StaticStruct()))
							{
								OutDataViews[Index] = FStateTreeDataView(FAnimNextStateTreeTraitContext::StaticStruct(), reinterpret_cast<uint8*>(&TraitContext));
							}
						}
					}

					return true;
				}));
		
				if (StateTreeExecutionContext.IsValid())
				{
					StateTreeExecutionContext.Start(&OwnerGraphInstance->GetVariables());
				}
			}
			
		}	
	}

	void FStateTreeTrait::PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (InstanceData->StateTree)
		{
			UObject* Owner = GetTransientPackage();
			
			FStateTreeExecutionContext StateTreeExecutionContext(*Owner, *InstanceData->StateTree, InstanceData->InstanceData);
			FAnimNextGraphInstance* OwnerGraphInstance = &Binding.GetTraitPtr().GetNodeInstance()->GetOwner();
			if(OwnerGraphInstance && InstanceData->StateTree->GetDefaultParameters().Identical(&OwnerGraphInstance->GetVariables(), 0))
			{
				FAnimNextStateTreeTraitContext TraitContext(Context, Binding.GetStack());
				StateTreeExecutionContext.SetContextDataByName(UStateTreeAnimNextSchema::AnimStateTreeExecutionContextName, FStateTreeDataView(FAnimNextStateTreeTraitContext::StaticStruct(), reinterpret_cast<uint8*>(&TraitContext)));

				const FInstancedPropertyBag& StateTreeParameters = InstanceData->StateTree->GetDefaultParameters();
				FStructView MutableVariables = InstanceData->InstanceData.GetMutableStorage().GetMutableGlobalParameters();
				
				const FRigVMExtendedExecuteContext& ExtendedExecuteContext = OwnerGraphInstance->GetExtendedExecuteContext();

				// This copy-behaviour is temporary until we find a better way to directly patch the StateTree property binding/copies
				const int32 NumVariables = StateTreeParameters.GetNumPropertiesInBag();
				for(int32 VariableIndex = 0; VariableIndex < NumVariables; ++VariableIndex)
				{
					if (ExtendedExecuteContext.ExternalVariableRuntimeData.IsValidIndex(VariableIndex))
					{
						const FPropertyBagPropertyDesc& Desc = StateTreeParameters.GetPropertyBagStruct()->GetPropertyDescs()[VariableIndex];
						void* TargetAddress = MutableVariables.GetMemory() + Desc.CachedProperty->GetOffset_ForInternal();
						void const* SourceAddress = ExtendedExecuteContext.ExternalVariableRuntimeData[VariableIndex].Memory;

						Desc.CachedProperty->CopyCompleteValue(TargetAddress, SourceAddress);
					}
				}

				StateTreeExecutionContext.SetCollectExternalDataCallback(FOnCollectStateTreeExternalData::CreateLambda([this, &TraitContext]( const FStateTreeExecutionContext& Context, const UStateTree* StateTree, TArrayView<const FStateTreeExternalDataDesc> ExternalDataDescs, TArrayView<FStateTreeDataView> OutDataViews) -> bool
				{
					for (int32 Index = 0; Index < ExternalDataDescs.Num(); Index++)
					{
						const FStateTreeExternalDataDesc& ItemDesc = ExternalDataDescs[Index];
						if (ItemDesc.Struct != nullptr)
						{
							if (ItemDesc.Struct->IsChildOf(FAnimNextStateTreeTraitContext::StaticStruct()))
							{
								OutDataViews[Index] = FStateTreeDataView(FAnimNextStateTreeTraitContext::StaticStruct(), reinterpret_cast<uint8*>(&TraitContext));
							}
						}
					}

					return true;
				}));

				StateTreeExecutionContext.Tick(TraitState.GetDeltaTime());
			}
		}
	}

	void FStateTreeTrait::AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const
	{
		IGarbageCollection::AddReferencedObjects(Context, Binding, Collector);

		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		Collector.AddReferencedObject(InstanceData->StateTree);
	}
}
