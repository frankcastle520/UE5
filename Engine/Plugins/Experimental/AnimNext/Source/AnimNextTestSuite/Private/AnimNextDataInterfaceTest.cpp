// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "AnimNextTest.h"
#include "UncookedOnlyUtils.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "Misc/AutomationTest.h"
#include "DataInterface/AnimNextDataInterfaceFactory.h"
#include "DataInterface/AnimNextDataInterface_EditorData.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/AnimNextAnimationGraphFactory.h"

// AnimNext Data Interface Tests

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

namespace UE::AnimNext::Tests
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDataInterfaceCompile, "Animation.AnimNext.DataInterface.Compile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataInterfaceCompile::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), UAnimNextDataInterfaceFactory::StaticClass());
	UAnimNextDataInterface* A = CastChecked<UAnimNextDataInterface>(Factory->FactoryCreateNew(UAnimNextDataInterface::StaticClass(), GetTransientPackage(), TEXT("TestDataInterfaceA"), RF_Transient, nullptr, nullptr, NAME_None));
	UE_RETURN_ON_ERROR(A != nullptr, "FDataInterfaceCompile -> Failed to create asset");
	UAnimNextDataInterface* B = CastChecked<UAnimNextDataInterface>(Factory->FactoryCreateNew(UAnimNextDataInterface::StaticClass(), GetTransientPackage(), TEXT("TestDataInterfaceB"), RF_Transient, nullptr, nullptr, NAME_None));
	UE_RETURN_ON_ERROR(B != nullptr, "FDataInterfaceCompile -> Failed to create asset");
	UAnimNextDataInterface* C = CastChecked<UAnimNextDataInterface>(Factory->FactoryCreateNew(UAnimNextDataInterface::StaticClass(), GetTransientPackage(), TEXT("TestDataInterfaceC"), RF_Transient, nullptr, nullptr, NAME_None));
	UE_RETURN_ON_ERROR(C != nullptr, "FDataInterfaceCompile -> Failed to create asset");
	UAnimNextDataInterface* D = CastChecked<UAnimNextDataInterface>(Factory->FactoryCreateNew(UAnimNextDataInterface::StaticClass(), GetTransientPackage(), TEXT("TestDataInterfaceD"), RF_Transient, nullptr, nullptr, NAME_None));
	UE_RETURN_ON_ERROR(D != nullptr, "FDataInterfaceCompile -> Failed to create asset");

	auto CompileAndTest = [A, B, C, D](TFunctionRef<void()> InTestFunction)
	{
		UncookedOnly::FUtils::CompileVariables(A);
		UncookedOnly::FUtils::CompileVariables(B);
		UncookedOnly::FUtils::CompileVariables(C);
		UncookedOnly::FUtils::CompileVariables(D);

		InTestFunction();
	};

	auto ClearVariables = [A, B, C, D]()
	{
		UAnimNextRigVMAssetLibrary::RemoveAllEntries(A, false, false);
		UAnimNextRigVMAssetLibrary::RemoveAllEntries(B, false, false);
		UAnimNextRigVMAssetLibrary::RemoveAllEntries(C, false, false);
		UAnimNextRigVMAssetLibrary::RemoveAllEntries(D, false, false);
	};

	auto RunTest = [this, &ClearVariables](TFunctionRef<void()> InTestFunction)
	{
		InTestFunction();
		ClearVariables();
	};

	auto AddPublicVariable = [this](UAnimNextDataInterface* InDataInterface, FName InName, const auto& InValue)
	{
		using ValueType = std::remove_reference_t<decltype(InValue)>;
		FAnimNextParamType Type = FAnimNextParamType::GetType<ValueType>();
		UAnimNextVariableEntry* VariableEntry = UAnimNextRigVMAssetLibrary::AddVariable(InDataInterface, InName, Type.GetValueType(), Type.GetContainerType(), Type.GetValueTypeObject(), TEXT(""), false, false);
		UE_RETURN_ON_ERROR(VariableEntry != nullptr, "FDataInterfaceCompile::AddPublicVariable -> Failed to create variable");
		VariableEntry->SetExportAccessSpecifier(EAnimNextExportAccessSpecifier::Public, false);
		UE_RETURN_ON_ERROR(VariableEntry->SetDefaultValue(InValue, false), "FDataInterfaceCompile::AddPublicVariable -> Failed to set variable default value");
		return true;
	};

	auto OverrideVariable = [this](UAnimNextDataInterface* InDataInterface, FName InName, const auto& InValue)
	{
		UAnimNextDataInterface_EditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextDataInterface_EditorData>(InDataInterface);
		bool bSuccessfulOverride = false;
		EditorData->ForEachEntryOfType<UAnimNextDataInterfaceEntry>([this, &InName, &InValue, &bSuccessfulOverride](UAnimNextDataInterfaceEntry* InEntry)
		{
			bSuccessfulOverride = InEntry->SetValueOverride(InName, InValue, false);
			return !bSuccessfulOverride;
		});

		UE_RETURN_ON_ERROR(bSuccessfulOverride, "FDataInterfaceCompile::OverrideVariable -> Failed to override default value");

		return false;
	};

	auto Implement = [this](UAnimNextDataInterface* InA, UAnimNextDataInterface* InB)
	{
		UAnimNextDataInterfaceEntry* DataInterfaceEntry = UAnimNextRigVMAssetLibrary::AddDataInterface(InA, InB, false, false);
		UE_RETURN_ON_ERROR(DataInterfaceEntry != nullptr, "FDataInterfaceCompile::Implement -> Failed to add data interface");
		return true;
	};

	auto CheckValue = [this](UAnimNextDataInterface* InDataInterface, FName InName, const auto& InValue)
	{
		const FPropertyBagPropertyDesc* Desc = InDataInterface->VariableDefaults.FindPropertyDescByName(InName);
		UE_RETURN_ON_ERROR(Desc != nullptr, "FDataInterfaceCompile::CheckValue -> Failed to find value");
		using ValueType = std::remove_reference_t<decltype(InValue)>;
		FAnimNextParamType DesiredType = FAnimNextParamType::GetType<ValueType>();
		FAnimNextParamType FoundType = FAnimNextParamType(Desc->ValueType, Desc->ContainerTypes.GetFirstContainerType(), Desc->ValueTypeObject);
		UE_RETURN_ON_ERROR(DesiredType == FoundType, "FDataInterfaceCompile::CheckValue -> Type was incorrect");
		check(Desc->CachedProperty);
		const uint8* ValuePtr = Desc->CachedProperty->ContainerPtrToValuePtr<uint8>(InDataInterface->VariableDefaults.GetValue().GetMemory());
		UE_RETURN_ON_ERROR(Desc->CachedProperty->Identical(ValuePtr, &InValue), "FDataInterfaceCompile::CheckValue -> Values were not equal");
		return true;
	};

	// Add a variable, dont override it, check its value
	RunTest([&]()
	{
		AddPublicVariable(A, "A", 1);
		CompileAndTest([&]()
		{
			CheckValue(A, "A", 1);
		});
	});

	// Add a variable, override it, check its value
	RunTest([&]()
	{
		AddPublicVariable(A, "A", 1);
		Implement(B, A);
		OverrideVariable(B, "A", 2);
		CompileAndTest([&]()
		{
			CheckValue(A, "A", 1);
			CheckValue(B, "A", 2);
		});
	});

	// Add a variable, override it twice, check its value at each step
	RunTest([&]()
	{
		AddPublicVariable(A, "A", 1);
		Implement(B, A);
		Implement(C, A);
		OverrideVariable(B, "A", 2);
		OverrideVariable(C, "A", 3);
		CompileAndTest([&]()
		{
			CheckValue(A, "A", 1);
			CheckValue(B, "A", 2);
			CheckValue(C, "A", 3);
		});
	});

	// Diamond inheritance
	//   A=1
	//  /   \
	// B=2  C=3
	//  \   /
	//   D=3
	RunTest([&]()
	{
		AddPublicVariable(A, "A", 1);
		Implement(B, A);
		Implement(C, A);
		Implement(D, B);
		Implement(D, C);
		OverrideVariable(B, "A", 2);
		OverrideVariable(C, "A", 3);
		CompileAndTest([&]()
		{
			CheckValue(A, "A", 1);
			CheckValue(B, "A", 2);
			CheckValue(C, "A", 3);
			CheckValue(D, "A", 3);
		});
	});
	
	FUtils::CleanupAfterTests();

	return true;
}

}

#endif	// WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR