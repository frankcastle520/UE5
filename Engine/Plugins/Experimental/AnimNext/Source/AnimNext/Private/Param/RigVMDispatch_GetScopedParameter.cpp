// Copyright Epic Games, Inc. All Rights Reserved.

#include "Param/RigVMDispatch_GetScopedParameter.h"
#include "RigVMCore/RigVMStruct.h"

const FName FRigVMDispatch_GetScopedParameter::ParameterName = TEXT("Parameter");
const FName FRigVMDispatch_GetScopedParameter::ValueName = TEXT("Value");
const FName FRigVMDispatch_GetScopedParameter::ParameterIdName = TEXT("ParameterId");
const FName FRigVMDispatch_GetScopedParameter::TypeHandleName = TEXT("Type");

FRigVMDispatch_GetScopedParameter::FRigVMDispatch_GetScopedParameter()
{
	FactoryScriptStruct = StaticStruct();
}

FName FRigVMDispatch_GetScopedParameter::GetArgumentNameForOperandIndex(int32 InOperandIndex, int32 InTotalOperands) const
{
	static const FName ArgumentNames[] =
	{
		ParameterName,
		ValueName,
		ParameterIdName,
		TypeHandleName,
	};
	check(InTotalOperands == UE_ARRAY_COUNT(ArgumentNames));
	return ArgumentNames[InOperandIndex];
}

#if WITH_EDITOR
FString FRigVMDispatch_GetScopedParameter::GetArgumentMetaData(const FName& InArgumentName, const FName& InMetaDataKey) const
{
	if ((InArgumentName == TypeHandleName || InArgumentName == ParameterIdName) &&
		InMetaDataKey == FRigVMStruct::SingletonMetaName)
	{
		return TEXT("True");
	}
	else if(InArgumentName == ParameterName && InMetaDataKey == FRigVMStruct::HideSubPinsMetaName)
	{
		return TEXT("True");
	}
	return Super::GetArgumentMetaData(InArgumentName, InMetaDataKey);
}
#endif

const TArray<FRigVMTemplateArgumentInfo>& FRigVMDispatch_GetScopedParameter::GetArgumentInfos() const
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
		Infos.Emplace(ParameterName, ERigVMPinDirection::Input, RigVMTypeUtils::TypeIndex::FString);
		Infos.Emplace(ValueName, ERigVMPinDirection::Output, ValueCategories);
		Infos.Emplace(ParameterIdName, ERigVMPinDirection::Hidden, RigVMTypeUtils::TypeIndex::UInt32);
		Infos.Emplace(TypeHandleName, ERigVMPinDirection::Hidden, RigVMTypeUtils::TypeIndex::UInt32);
	}

	return Infos;
}
 
FRigVMTemplateTypeMap FRigVMDispatch_GetScopedParameter::OnNewArgumentType(const FName& InArgumentName, TRigVMTypeIndex InTypeIndex) const
{
	FRigVMTemplateTypeMap Types;
	const FRigVMRegistry_NoLock& Registry = FRigVMRegistry_NoLock::GetForRead();
	Types.Add(ParameterName, RigVMTypeUtils::TypeIndex::FString);
	Types.Add(ValueName, InTypeIndex);
	Types.Add(ParameterIdName, RigVMTypeUtils::TypeIndex::UInt32);
	Types.Add(TypeHandleName, RigVMTypeUtils::TypeIndex::UInt32);
	return Types;
}

void FRigVMDispatch_GetScopedParameter::Execute(FRigVMExtendedExecuteContext& InContext, FRigVMMemoryHandleArray Handles, FRigVMPredicateBranchArray RigVMBranches)
{
	// Deprecated stub
}
