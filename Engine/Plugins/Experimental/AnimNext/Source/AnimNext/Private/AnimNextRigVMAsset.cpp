// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextRigVMAsset.h"
#include "RigVMRuntimeDataRegistry.h"
#include "Graph/RigUnit_AnimNextBeginExecution.h"
#include "UObject/AssetRegistryTagsContext.h"

UAnimNextRigVMAsset::UAnimNextRigVMAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetRigVMExtendedExecuteContext(&ExtendedExecuteContext);
}

void UAnimNextRigVMAsset::BeginDestroy()
{
	Super::BeginDestroy();

	if (VM)
	{
		UE::AnimNext::FRigVMRuntimeDataRegistry::ReleaseAllVMRuntimeData(VM);
	}
}

void UAnimNextRigVMAsset::PostLoad()
{
	Super::PostLoad();

	ExtendedExecuteContext.InvalidateCachedMemory();

	VM = RigVM;

	// In packaged builds, initialize the VM
	// In editor, the VM will be recompiled and initialized at UAnimNextRigVMAssetEditorData::HandlePackageDone::RecompileVM
#if !WITH_EDITOR
	if(VM != nullptr)
	{
		VM->ClearExternalVariables(ExtendedExecuteContext);
		VM->SetExternalVariableDefs(GetExternalVariablesImpl(false));
		VM->Initialize(ExtendedExecuteContext);
		InitializeVM(FRigUnit_AnimNextBeginExecution::EventName);
	}
#endif
}

void UAnimNextRigVMAsset::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

#if WITH_EDITORONLY_DATA
	if(EditorData)
	{
		EditorData->GetAssetRegistryTags(Context);
	}
#endif

#if WITH_EDITOR
	// Allow asset user data to output tags
	for(const UAssetUserData* AssetUserDataItem : *GetAssetUserDataArray())
	{
		if (AssetUserDataItem)
		{
			AssetUserDataItem->GetAssetRegistryTags(Context);
		}
	}
#endif // WITH_EDITOR
}

TArray<FRigVMExternalVariable> UAnimNextRigVMAsset::GetExternalVariablesImpl(bool bFallbackToBlueprint) const
{
	TArray<FRigVMExternalVariable> ExternalVariables;

	if(const UPropertyBag* PropertyBag = VariableDefaults.GetPropertyBagStruct())
	{
		TConstArrayView<FPropertyBagPropertyDesc> VariableDescs = PropertyBag->GetPropertyDescs();
		if(VariableDescs.Num() > 0)
		{
			ExternalVariables.Reserve(VariableDescs.Num());
			for(const FPropertyBagPropertyDesc& Desc : VariableDescs)
			{
				const FProperty* Property = Desc.CachedProperty;
				FRigVMExternalVariable ExternalVariable = FRigVMExternalVariable::Make(Property, const_cast<uint8*>(VariableDefaults.GetValue().GetMemory()));
				if(!ExternalVariable.IsValid())
				{
					UE_LOG(LogRigVM, Warning, TEXT("%s: Property '%s' of type '%s' is not supported."), *GetClass()->GetName(), *Property->GetName(), *Property->GetCPPType());
					continue;
				}

				ExternalVariables.Add(ExternalVariable);
			}
		}
	}

	return ExternalVariables;
}
