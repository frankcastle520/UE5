// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/RigVMDispatch_GetObjectProperty.h"
#include "RigVMCore/RigVMStruct.h"
#include "Variables/AnimNextFieldPath.h"

FRigVMDispatch_GetObjectProperty::FRigVMDispatch_GetObjectProperty()
{
	FactoryScriptStruct = StaticStruct();
}

FName FRigVMDispatch_GetObjectProperty::GetArgumentNameForOperandIndex(int32 InOperandIndex, int32 InTotalOperands) const
{
	static const FName ArgumentNames[] =
	{
		ObjectName,
		PropertyName,
		ValueName,
	};
	check(InTotalOperands == UE_ARRAY_COUNT(ArgumentNames));
	return ArgumentNames[InOperandIndex];
}

const TArray<FRigVMTemplateArgumentInfo>& FRigVMDispatch_GetObjectProperty::GetArgumentInfos() const
{
	static TArray<FRigVMTemplateArgumentInfo> Infos;
	if(Infos.IsEmpty())
	{
		static const TArray<FRigVMTemplateArgument::ETypeCategory> ValueCategories =
		{
			FRigVMTemplateArgument::ETypeCategory_SingleAnyValue,
			FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue
		};

		const FRigVMRegistry_NoLock& Registry = FRigVMRegistry_NoLock::GetForRead();
		Infos.Emplace(ObjectName, ERigVMPinDirection::Input, Registry.GetTypeIndex_NoLock<UObject>());
		Infos.Emplace(PropertyName, ERigVMPinDirection::Input, Registry.GetTypeIndex_NoLock<FAnimNextFieldPath>());
		Infos.Emplace(ValueName, ERigVMPinDirection::Output, ValueCategories);
	}

	return Infos;
}

FRigVMTemplateTypeMap FRigVMDispatch_GetObjectProperty::OnNewArgumentType(const FName& InArgumentName, TRigVMTypeIndex InTypeIndex) const
{
	const FRigVMRegistry_NoLock& Registry = FRigVMRegistry_NoLock::GetForRead();
	
	FRigVMTemplateTypeMap Types;
	Types.Add(ObjectName, Registry.GetTypeIndex_NoLock<UObject>());
	Types.Add(PropertyName, Registry.GetTypeIndex_NoLock<FAnimNextFieldPath>());
	Types.Add(ValueName, InTypeIndex);
	return Types;
}

void FRigVMDispatch_GetObjectProperty::Execute(FRigVMExtendedExecuteContext& InContext, FRigVMMemoryHandleArray Handles, FRigVMPredicateBranchArray RigVMBranches)
{
	const UObject* ObjectPtr = *reinterpret_cast<UObject**>(Handles[0].GetData());
	if(ObjectPtr == nullptr)
	{
		// Something failed to resolve upstream, OK to just skip this work
		return;
	}

	FAnimNextFieldPath* FieldPathPtr = reinterpret_cast<FAnimNextFieldPath*>(Handles[1].GetData());
	const FProperty* SourceProperty = FieldPathPtr->FieldPath.Get();
	checkSlow(SourceProperty);
	const uint8* SourceAddress = SourceProperty->ContainerPtrToValuePtr<uint8>(ObjectPtr);
	checkSlow(SourceAddress);

	const FProperty* TargetProperty = Handles[2].GetResolvedProperty();
	checkSlow(TargetProperty);
	checkSlow(TargetProperty->GetClass() == SourceProperty->GetClass());
	uint8* TargetAddress = Handles[2].GetData();
	checkSlow(TargetAddress);

	// TODO: add a specialization for bool to GetDispatchFunctionImpl rather than branching here
	const FBoolProperty* SourceBoolProperty = CastField<FBoolProperty>(SourceProperty);
	if(SourceBoolProperty)
	{
		static_cast<const FBoolProperty*>(TargetProperty)->SetPropertyValue(TargetAddress, SourceBoolProperty->GetPropertyValue(SourceAddress));
	}
	else
	{
		SourceProperty->CopyCompleteValue(TargetAddress, SourceAddress);
	}
}

