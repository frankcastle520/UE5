// Copyright Epic Games, Inc. All Rights Reserved.

#include "UncookedOnlyUtils.h"

#include "K2Node_CallFunction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Module/AnimNextModule.h"
#include "AnimNextController.h"
#include "Module/AnimNextModule_EditorData.h"
#include "Graph/RigUnit_AnimNextGraphRoot.h"
#include "Serialization/MemoryReader.h"
#include "RigVMCore/RigVM.h"
#include "AnimNextRigVMAsset.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "AnimNextUncookedOnlyModule.h"
#include "IAnimNextRigVMExportInterface.h"
#include "GameFramework/Character.h"
#include "Graph/AnimNextGraphEntryPoint.h"
#include "Graph/AnimNextAnimationGraphSchema.h"
#include "Logging/StructuredLog.h"
#include "AnimNextAssetWorkspaceAssetUserData.h"
#include "IWorkspaceEditor.h"
#include "IWorkspaceEditorModule.h"
#include "Misc/EnumerateRange.h"
#include "Module/RigUnit_AnimNextModuleEvents.h"
#include "Variables/IVariableBindingType.h"
#include "Variables/RigUnit_CopyModuleProxyVariables.h"
#include "WorkspaceAssetRegistryInfo.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "DataInterface/AnimNextDataInterface_EditorData.h"

#define LOCTEXT_NAMESPACE "AnimNextUncookedOnlyUtils"

namespace UE::AnimNext::UncookedOnly
{

TAutoConsoleVariable<bool> CVarDumpProgrammaticGraphs(
	TEXT("AnimNext.DumpProgrammaticGraphs"),
	false,
	TEXT("When true the transient programmatic graphs will be automatically opened for any that are generated."));

void FUtils::RecreateVM(UAnimNextRigVMAsset* InAsset)
{
	if (InAsset->VM == nullptr)
	{
		InAsset->VM = NewObject<URigVM>(InAsset, TEXT("VM"), RF_NoFlags);
	}
	InAsset->VM->Reset(InAsset->ExtendedExecuteContext);
	InAsset->RigVM = InAsset->VM; // Local serialization
}


void FUtils::CompileVariables(UAnimNextRigVMAsset* InAsset)
{
	check(InAsset);

	UAnimNextDataInterface* DataInterface = Cast<UAnimNextDataInterface>(InAsset);
	if(DataInterface == nullptr)
	{
		// Currently only support data interface types (TODO: could make UAnimNextDataInterface the common base rather than UAnimNextRigVMAsset)
		return;
	}

	FMessageLog Log("AnimNextCompilerResults");

	UAnimNextDataInterface_EditorData* EditorData = GetEditorData<UAnimNextDataInterface_EditorData>(DataInterface);

	struct FStructEntryInfo
	{
		const UAnimNextDataInterface* FromDataInterface;
		FName Name;
		FAnimNextParamType Type;
		EAnimNextExportAccessSpecifier AccessSpecifier;
		bool bAutoBindDataInterfaceToHost;
		TConstArrayView<uint8> Value;
	};

	// Gather all variables in this asset.
	// Variables are harvested from the valid entries and data interfaces.
	// Data interface harvesting is performed recursively
	// The topmost value for a data interface 'wins' if a value is to be supplied
	TMap<FName, int32> EntryInfoIndexMap;
	TArray<FStructEntryInfo> StructEntryInfos;
	StructEntryInfos.Reserve(EditorData->Entries.Num());
	int32 NumPublicVariables = 0;

	auto AddVariable = [&Log, &NumPublicVariables, &StructEntryInfos, &EntryInfoIndexMap](const UAnimNextVariableEntry* InVariable, const UAnimNextDataInterfaceEntry* InFromInterfaceEntry, const UAnimNextDataInterface* InFromInterface, bool bInAutoBindInterface)
	{
		const FName Name = InVariable->GetExportName();
		const FAnimNextParamType& Type = InVariable->GetExportType();
		if(!Type.IsValid())
		{
			Log.Error(FText::Format(LOCTEXT("InvalidVariableTypeFound", "Variable '{0}' with invalid type found"), FText::FromName(Name)));
			return;
		}

		const EAnimNextExportAccessSpecifier AccessSpecifier = InVariable->GetExportAccessSpecifier();

		// Check for type conflicts
		int32* ExistingIndexPtr = EntryInfoIndexMap.Find(Name);
		if(ExistingIndexPtr)
		{
			const FStructEntryInfo& ExistingInfo = StructEntryInfos[*ExistingIndexPtr];
			if(ExistingInfo.Type != Type)
			{
				Log.Error(FText::Format(LOCTEXT("ConflictingVariableTypeFound", "Variable '{0}' with conflicting type found ({1} vs {2})"), FText::FromName(Name), FText::FromString(ExistingInfo.Type.ToString()), FText::FromString(Type.ToString())));
				return;
			}

			if(ExistingInfo.AccessSpecifier != AccessSpecifier)
			{
				Log.Error(FText::Format(LOCTEXT("ConflictingVariableAccessFound", "Variable '{0}' with conflicting access specifier found ({1} vs {2})"), FText::FromName(Name), FText::FromString(UEnum::GetValueAsString(ExistingInfo.AccessSpecifier)), FText::FromString(UEnum::GetValueAsString(AccessSpecifier))));
				return;
			}
		}
		else
		{
			if(AccessSpecifier == EAnimNextExportAccessSpecifier::Public)
			{
				NumPublicVariables++;
			}
		}

		// Check the overrides to see if this variable's default is overriden
		const FProperty* OverrideProperty = nullptr;
		TConstArrayView<uint8> OverrideValue;
		if(InFromInterfaceEntry != nullptr)
		{
			InFromInterfaceEntry->FindValueOverrideRecursive(Name, OverrideProperty, OverrideValue);
		}

		TConstArrayView<uint8> Value;
		if(!OverrideValue.IsEmpty())
		{
			Value = OverrideValue;
		}
		else
		{
			Value = TConstArrayView<uint8>(InVariable->GetValuePtr(), Type.GetSize());
		}

		if(ExistingIndexPtr)
		{
			StructEntryInfos[*ExistingIndexPtr].Value = Value;
		}
		else
		{
			int32 Index = StructEntryInfos.Add(
			{
				InFromInterface,
				Name,
				FAnimNextParamType(Type.GetValueType(), Type.GetContainerType(), Type.GetValueTypeObject()),
				AccessSpecifier,
				bInAutoBindInterface,
				Value
			});

			EntryInfoIndexMap.Add(Name, Index);
		}
	};

	auto AddDataInterface = [&Log, &AddVariable, &DataInterface](const UAnimNextDataInterface* InDataInterface, const UAnimNextDataInterfaceEntry* InDataInterfaceEntry, bool bInPublicOnly, bool bInAutoBindInterface, auto& InAddDataInterface) -> void
	{
		const UAnimNextDataInterface_EditorData* DataInterfaceEditorData = GetEditorData<const UAnimNextDataInterface_EditorData>(InDataInterface);
		check(DataInterfaceEditorData != nullptr);

		for(UAnimNextRigVMAssetEntry* OtherEntry : DataInterfaceEditorData->Entries)
		{
			if(const UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(OtherEntry))
			{
				if(!bInPublicOnly || VariableEntry->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Public)
				{
					AddVariable(VariableEntry, InDataInterfaceEntry, InDataInterface, bInAutoBindInterface);
				}
			}
			else if(const UAnimNextDataInterfaceEntry* DataInterfaceEntry = Cast<UAnimNextDataInterfaceEntry>(OtherEntry))
			{
				UAnimNextDataInterface* SubDataInterface = DataInterfaceEntry->GetDataInterface();
				if(SubDataInterface == nullptr)
				{
					Log.Error(FText::Format(LOCTEXT("MissingDataInterfaceWarning", "Invalid data interface found: {0}"), FText::FromString(DataInterfaceEntry->GetDataInterfacePath().ToString())));
					return;
				}
				else if(DataInterface == SubDataInterface)
				{
					Log.Error(FText::Format(LOCTEXT("CircularDataInterfaceRefError", "Circular data interface reference found: {0}"), FText::FromString(DataInterfaceEntry->GetDataInterfacePath().ToString())));
					return;
				}
				else
				{
					bool bAutoBindInterface = DataInterfaceEntry->AutomaticBinding == EAnimNextDataInterfaceAutomaticBindingMode::BindSharedInterfaces;
					InAddDataInterface(SubDataInterface, DataInterfaceEntry, true, bAutoBindInterface, InAddDataInterface);
				}
			}
		}
	};

	AddDataInterface(DataInterface, nullptr, false, false, AddDataInterface);

	// Sort public entries first, then by data interface & then by size, largest first, for better packing
	static_assert(EAnimNextExportAccessSpecifier::Private < EAnimNextExportAccessSpecifier::Public, "Private must be less than Public as parameters are sorted internally according to this assumption");
	StructEntryInfos.Sort([](const FStructEntryInfo& InLHS, const FStructEntryInfo& InRHS)
	{
		if(InLHS.AccessSpecifier != InRHS.AccessSpecifier)
		{
			return InLHS.AccessSpecifier > InRHS.AccessSpecifier;
		}
		else if(InLHS.FromDataInterface != InRHS.FromDataInterface)
		{
			return InLHS.FromDataInterface->GetFName().LexicalLess(InRHS.FromDataInterface->GetFName());
		}
		else if(InLHS.Type.GetSize() != InRHS.Type.GetSize())
		{
			return InLHS.Type.GetSize() > InRHS.Type.GetSize();
		}
		else
		{
			return InLHS.Name.LexicalLess(InRHS.Name);
		}
	});

	if(StructEntryInfos.Num() > 0)
	{
		// Build PropertyDescs and values to batch-create the property bag
		TArray<FPropertyBagPropertyDesc> PropertyDescs;
		PropertyDescs.Reserve(StructEntryInfos.Num());
		TArray<TConstArrayView<uint8>> Values;
		Values.Reserve(StructEntryInfos.Num());

		DataInterface->ImplementedInterfaces.Empty();

		for (TEnumerateRef<const FStructEntryInfo> StructEntryInfo : EnumerateRange(StructEntryInfos))
		{
			PropertyDescs.Emplace(StructEntryInfo->Name, StructEntryInfo->Type.ContainerType, StructEntryInfo->Type.ValueType, StructEntryInfo->Type.ValueTypeObject);
			Values.Add(StructEntryInfo->Value);

			if(StructEntryInfo->AccessSpecifier != EAnimNextExportAccessSpecifier::Public)
			{
				continue;
			}

			// Now process any data interfaces (sets of public variables) 
			auto CheckForExistingDataInterface = [&StructEntryInfo](const FAnimNextImplementedDataInterface& InImplementedDataInterface)
			{
				return InImplementedDataInterface.DataInterface == StructEntryInfo->FromDataInterface;
			};

			FAnimNextImplementedDataInterface* ExistingImplementedDataInterface = DataInterface->ImplementedInterfaces.FindByPredicate(CheckForExistingDataInterface);
			if(ExistingImplementedDataInterface == nullptr)
			{
				FAnimNextImplementedDataInterface& NewImplementedDataInterface = DataInterface->ImplementedInterfaces.AddDefaulted_GetRef();
				NewImplementedDataInterface.DataInterface = StructEntryInfo->FromDataInterface;
				NewImplementedDataInterface.VariableIndex = StructEntryInfo.GetIndex();
				NewImplementedDataInterface.NumVariables = 1;
				NewImplementedDataInterface.bAutoBindToHost = StructEntryInfo->bAutoBindDataInterfaceToHost; 
			}
			else
			{
				ExistingImplementedDataInterface->NumVariables++;
			}
		}

		// Create new property bags and migrate
		EPropertyBagResult Result = DataInterface->VariableDefaults.ReplaceAllPropertiesAndValues(PropertyDescs, Values);
		check(Result == EPropertyBagResult::Success);

		if(NumPublicVariables > 0)
		{
			TConstArrayView<FPropertyBagPropertyDesc> PublicPropertyDescs(PropertyDescs.GetData(), NumPublicVariables);
			TConstArrayView<TConstArrayView<uint8>> PublicValues(Values.GetData(), NumPublicVariables);
			Result = DataInterface->PublicVariableDefaults.ReplaceAllPropertiesAndValues(PublicPropertyDescs, PublicValues);
			check(Result == EPropertyBagResult::Success);
		}
		else
		{
			DataInterface->PublicVariableDefaults.Reset();
		}

		// Rebuild external variables
		DataInterface->VM->SetExternalVariableDefs(DataInterface->GetExternalVariablesImpl(false));
	}
	else
	{
		DataInterface->ImplementedInterfaces.Empty();
		DataInterface->VariableDefaults.Reset();
		DataInterface->PublicVariableDefaults.Reset();
		DataInterface->VM->ClearExternalVariables(DataInterface->ExtendedExecuteContext);
	}
}

void FUtils::CompileVariableBindings(const FRigVMCompileSettings& InSettings, UAnimNextRigVMAsset* InAsset, TArray<URigVMGraph*>& OutGraphs)
{
	check(InAsset);

	FModule& Module = FModuleManager::LoadModuleChecked<FModule>("AnimNextUncookedOnly");
	UAnimNextRigVMAssetEditorData* EditorData = GetEditorData(InAsset);
	TMap<IVariableBindingType*, TArray<IVariableBindingType::FBindingGraphInput>> BindingGroups;

	for(const UAnimNextRigVMAssetEntry* Entry : EditorData->Entries)
	{
		const IAnimNextRigVMVariableInterface* Variable = Cast<IAnimNextRigVMVariableInterface>(Entry);
		if(Variable == nullptr)
		{
			continue;
		}

		TConstStructView<FAnimNextVariableBindingData> Binding = Variable->GetBinding();
		if(!Binding.IsValid() || !Binding.Get<FAnimNextVariableBindingData>().IsValid())
		{
			continue;
		}

		TSharedPtr<IVariableBindingType> BindingType = Module.FindVariableBindingType(Binding.GetScriptStruct());
		if(!BindingType.IsValid())
		{
			continue;
		}

		TArray<IVariableBindingType::FBindingGraphInput>& Group = BindingGroups.FindOrAdd(BindingType.Get());
		FRigVMTemplateArgumentType RigVMArg = Variable->GetType().ToRigVMTemplateArgument();
		Group.Add({ Variable->GetVariableName(), RigVMArg.CPPType.ToString(), RigVMArg.CPPTypeObject, Binding });
	}

	const bool bHasBindings = BindingGroups.Num() > 0;
	const bool bHasPublicVariablesToCopy = EditorData->IsA<UAnimNextModule_EditorData>() && EditorData->HasPublicVariables();
	if(!bHasBindings && !bHasPublicVariablesToCopy)
	{
		// Nothing to do here
		return;
	}

	URigVMGraph* BindingGraph = NewObject<URigVMGraph>(EditorData, NAME_None, RF_Transient);

	FRigVMClient* VMClient = EditorData->GetRigVMClient();
	URigVMController* Controller = VMClient->GetOrCreateController(BindingGraph);
	URigVMNode* ExecuteBindingsNode = Controller->AddUnitNode(FRigUnit_AnimNextExecuteBindings::StaticStruct(), FRigUnit_AnimNextExecuteBindings::GetMethodName(), FVector2D::ZeroVector, FString(), false);
	if(ExecuteBindingsNode == nullptr)
	{
		InSettings.ReportError(TEXT("Could not spawn Execute Bindings node"));
		return;
	}
	URigVMPin* ExecuteBindingsExecPin = ExecuteBindingsNode->FindPin(FRigVMStruct::ExecuteContextName.ToString());
	if(ExecuteBindingsExecPin == nullptr)
	{
		InSettings.ReportError(TEXT("Could not find execute pin on Execute Bindings node"));
		return;
	}
	URigVMPin* ExecPin = ExecuteBindingsExecPin;

	if(bHasPublicVariablesToCopy)
	{
		URigVMNode* CopyProxyVariablesNode = Controller->AddUnitNode(FRigUnit_CopyModuleProxyVariables::StaticStruct(), FRigUnit_CopyModuleProxyVariables::GetMethodName(), FVector2D(200, 0.0f), FString(), false);
		if(CopyProxyVariablesNode == nullptr)
		{
			InSettings.ReportError(TEXT("Could not spawn Copy Module Proxy Variables node"));
			return;
		}
		URigVMPin* CopyProxyVariablesExecPin = CopyProxyVariablesNode->FindPin(FRigVMStruct::ExecuteContextName.ToString());
		if(ExecPin == nullptr)
		{
			InSettings.ReportError(TEXT("Could not find execute pin on Execute Bindings node"));
			return;
		}
		bool bLinkAdded = Controller->AddLink(ExecuteBindingsExecPin, CopyProxyVariablesExecPin, false);
		if(!bLinkAdded)
		{
			InSettings.ReportError(TEXT("Could not link Execute Bindings node"));
			return;
		}
		ExecPin = CopyProxyVariablesExecPin;
	}

	IVariableBindingType::FBindingGraphFragmentArgs Args;
	Args.Event = FRigUnit_AnimNextExecuteBindings::StaticStruct();
	Args.Controller = Controller;
	Args.BindingGraph = BindingGraph;
	Args.ExecTail = ExecPin;

	FVector2D Location(0.0f, 0.0f);
	for(const TPair<IVariableBindingType*, TArray<IVariableBindingType::FBindingGraphInput>>& BindingGroupPair : BindingGroups)
	{
		Args.Inputs = BindingGroupPair.Value;
		BindingGroupPair.Key->BuildBindingGraphFragment(InSettings, Args, ExecPin, Location);
	}

	OutGraphs.Add(BindingGraph);
}

UAnimNextRigVMAsset* FUtils::GetAsset(UAnimNextRigVMAssetEditorData* InEditorData)
{
	check(InEditorData);
	return CastChecked<UAnimNextRigVMAsset>(InEditorData->GetOuter());
}

UAnimNextRigVMAssetEditorData* FUtils::GetEditorData(UAnimNextRigVMAsset* InAsset)
{
	check(InAsset);
	return CastChecked<UAnimNextRigVMAssetEditorData>(InAsset->EditorData);
}

FAnimNextParamType FUtils::GetParamTypeFromPinType(const FEdGraphPinType& InPinType)
{
	FAnimNextParamType::EValueType ValueType = FAnimNextParamType::EValueType::None;
	FAnimNextParamType::EContainerType ContainerType = FAnimNextParamType::EContainerType::None;
	UObject* ValueTypeObject = nullptr;

	if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		ValueType = FAnimNextParamType::EValueType::Bool;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		ValueType = FAnimNextParamType::EValueType::Byte;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Int)
	{
		ValueType = FAnimNextParamType::EValueType::Int32;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
	{
		ValueType = FAnimNextParamType::EValueType::Int64;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		if (InPinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
		{
			ValueType = FAnimNextParamType::EValueType::Float;
		}
		else if (InPinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
		{
			ValueType = FAnimNextParamType::EValueType::Double;
		}
		else
		{
			ensure(false);	// Reals should be either floats or doubles
		}
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Float)
	{
		ValueType = FAnimNextParamType::EValueType::Float;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Double)
	{
		ValueType = FAnimNextParamType::EValueType::Double;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		ValueType = FAnimNextParamType::EValueType::Name;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		ValueType = FAnimNextParamType::EValueType::String;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		ValueType = FAnimNextParamType::EValueType::Text;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
	{
		ValueType = FAnimNextParamType::EValueType::Enum;
		ValueTypeObject = InPinType.PinSubCategoryObject.Get();
		ensure(ValueTypeObject);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		ValueType = FAnimNextParamType::EValueType::Struct;
		ValueTypeObject = Cast<UScriptStruct>(InPinType.PinSubCategoryObject.Get());
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Object || InPinType.PinCategory == UEdGraphSchema_K2::AllObjectTypes)
	{
		ValueType = FAnimNextParamType::EValueType::Object;
		ValueTypeObject = Cast<UClass>(InPinType.PinSubCategoryObject.Get());
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
	{
		ValueType = FAnimNextParamType::EValueType::SoftObject;
		ValueTypeObject = Cast<UClass>(InPinType.PinSubCategoryObject.Get());
		ensure(ValueTypeObject);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		ValueType = FAnimNextParamType::EValueType::SoftClass;
		ValueTypeObject = Cast<UClass>(InPinType.PinSubCategoryObject.Get());
		ensure(ValueTypeObject);
	}

	if(InPinType.ContainerType == EPinContainerType::Array)
	{
		ContainerType = FAnimNextParamType::EContainerType::Array;
	}
	else if(InPinType.ContainerType == EPinContainerType::Set)
	{
		ensureMsgf(false, TEXT("Set pins are not yet supported"));
	}
	if(InPinType.ContainerType == EPinContainerType::Map)
	{
		ensureMsgf(false, TEXT("Map pins are not yet supported"));
	}
	
	return FAnimNextParamType(ValueType, ContainerType, ValueTypeObject);
}

FEdGraphPinType FUtils::GetPinTypeFromParamType(const FAnimNextParamType& InParamType)
{
	FEdGraphPinType PinType;
	PinType.PinSubCategory = NAME_None;

	// Container type
	switch (InParamType.ContainerType)
	{
	case FAnimNextParamType::EContainerType::Array:
		PinType.ContainerType = EPinContainerType::Array;
		break;
	default:
		PinType.ContainerType = EPinContainerType::None;
	}

	// Value type
	switch (InParamType.ValueType)
	{
	case EPropertyBagPropertyType::Bool:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		break;
	case EPropertyBagPropertyType::Byte:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		break;
	case EPropertyBagPropertyType::Int32:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		break;
	case EPropertyBagPropertyType::Int64:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		break;
	case EPropertyBagPropertyType::Float:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		break;
	case EPropertyBagPropertyType::Double:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		break;
	case EPropertyBagPropertyType::Name:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		break;
	case EPropertyBagPropertyType::String:
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		break;
	case EPropertyBagPropertyType::Text:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		break;
	case EPropertyBagPropertyType::Enum:
		// @todo: some pin coloring is not correct due to this (byte-as-enum vs enum). 
		PinType.PinCategory = UEdGraphSchema_K2::PC_Enum;
		PinType.PinSubCategoryObject = const_cast<UObject*>(InParamType.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::Struct:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = const_cast<UObject*>(InParamType.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::Object:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = const_cast<UObject*>(InParamType.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::SoftObject:
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
		PinType.PinSubCategoryObject = const_cast<UObject*>(InParamType.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::Class:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		PinType.PinSubCategoryObject = const_cast<UObject*>(InParamType.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::SoftClass:
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
		PinType.PinSubCategoryObject = const_cast<UObject*>(InParamType.ValueTypeObject.Get());
		break;
	default:
		break;
	}

	return PinType;
}

FRigVMTemplateArgumentType FUtils::GetRigVMArgTypeFromParamType(const FAnimNextParamType& InParamType)
{
	FRigVMTemplateArgumentType ArgType;

	FString CPPTypeString;

	// Value type
	switch (InParamType.ValueType)
	{
	case EPropertyBagPropertyType::Bool:
		CPPTypeString = RigVMTypeUtils::BoolType;
		break;
	case EPropertyBagPropertyType::Byte:
		CPPTypeString = RigVMTypeUtils::UInt8Type;
		break;
	case EPropertyBagPropertyType::Int32:
		CPPTypeString = RigVMTypeUtils::UInt32Type;
		break;
	case EPropertyBagPropertyType::Int64:
		ensureMsgf(false, TEXT("Unhandled value type %d"), InParamType.ValueType);
		break;
	case EPropertyBagPropertyType::Float:
		CPPTypeString = RigVMTypeUtils::FloatType;
		break;
	case EPropertyBagPropertyType::Double:
		CPPTypeString = RigVMTypeUtils::DoubleType;
		break;
	case EPropertyBagPropertyType::Name:
		CPPTypeString = RigVMTypeUtils::FNameType;
		break;
	case EPropertyBagPropertyType::String:
		CPPTypeString = RigVMTypeUtils::FStringType;
		break;
	case EPropertyBagPropertyType::Text:
		ensureMsgf(false, TEXT("Unhandled value type %d"), InParamType.ValueType);
		break;
	case EPropertyBagPropertyType::Enum:
		CPPTypeString = RigVMTypeUtils::CPPTypeFromEnum(Cast<UEnum>(InParamType.ValueTypeObject.Get()));
		ArgType.CPPTypeObject = const_cast<UObject*>(InParamType.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::Struct:
		CPPTypeString = RigVMTypeUtils::GetUniqueStructTypeName(Cast<UScriptStruct>(InParamType.ValueTypeObject.Get()));
		ArgType.CPPTypeObject = const_cast<UObject*>(InParamType.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::Object:
		CPPTypeString = RigVMTypeUtils::CPPTypeFromObject(Cast<UClass>(InParamType.ValueTypeObject.Get()), RigVMTypeUtils::EClassArgType::AsObject);
		ArgType.CPPTypeObject = const_cast<UObject*>(InParamType.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::SoftObject:
		ensureMsgf(false, TEXT("Unhandled value type %d"), InParamType.ValueType);
		break;
	case EPropertyBagPropertyType::Class:
		CPPTypeString = RigVMTypeUtils::CPPTypeFromObject(Cast<UClass>(InParamType.ValueTypeObject.Get()), RigVMTypeUtils::EClassArgType::AsClass);
		ArgType.CPPTypeObject = const_cast<UObject*>(InParamType.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::SoftClass:
		ensureMsgf(false, TEXT("Unhandled value type %d"), InParamType.ValueType);
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled value type %d"), InParamType.ValueType);
		break;
	}

	// Container type
	switch (InParamType.ContainerType)
	{
	case FAnimNextParamType::EContainerType::None:
		break;
	case FAnimNextParamType::EContainerType::Array:
		CPPTypeString = FString::Printf(RigVMTypeUtils::TArrayTemplate, *CPPTypeString);
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled container type %d"), InParamType.ContainerType);
		break;
	}

	ArgType.CPPType = *CPPTypeString;

	return ArgType;
}

void FUtils::SetupAnimGraph(const FName EntryName, URigVMController* InController)
{
	// Clear the graph
	InController->RemoveNodes(InController->GetGraph()->GetNodes());

	// Add root node
	URigVMUnitNode* MainEntryPointNode = InController->AddUnitNode(FRigUnit_AnimNextGraphRoot::StaticStruct(), FRigUnit_AnimNextGraphRoot::EventName, FVector2D(-400.0f, 0.0f), FString(), false);
	if(MainEntryPointNode == nullptr)
	{
		return;
	}

	URigVMPin* BeginExecutePin = MainEntryPointNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_AnimNextGraphRoot, Result));
	if(BeginExecutePin == nullptr)
	{
		return;
	}

	URigVMPin* EntryPointPin = MainEntryPointNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_AnimNextGraphRoot, EntryPoint));
	if(EntryPointPin == nullptr)
	{
		return;
	}

	InController->SetPinDefaultValue(EntryPointPin->GetPinPath(), EntryName.ToString());
}

void FUtils::SetupEventGraph(URigVMController* InController, UScriptStruct* InEventStruct)
{
	// Clear the graph
	InController->RemoveNodes(InController->GetGraph()->GetNodes());

	// Add entry point
	InController->AddUnitNode(InEventStruct, FRigVMStruct::ExecuteName, FVector2D(-200.0f, 0.0f), FString(), false);
}

FAnimNextParamType FUtils::GetParameterTypeFromName(FName InName)
{
	// Query the asset registry for other params
	TMap<FAssetData, FAnimNextAssetRegistryExports> ExportMap;
	GetExportedVariablesFromAssetRegistry(ExportMap);
	for(const TPair<FAssetData, FAnimNextAssetRegistryExports>& ExportPair : ExportMap)
	{
		for(const FAnimNextAssetRegistryExportedVariable& Parameter : ExportPair.Value.Variables)
		{
			if(Parameter.Name == InName)
			{
				return Parameter.Type;
			}
		}
	}

	return FAnimNextParamType();
}

bool FUtils::GetExportedVariablesForAsset(const FAssetData& InAsset, FAnimNextAssetRegistryExports& OutExports)
{
	const FString TagValue = InAsset.GetTagValueRef<FString>(UE::AnimNext::ExportsAnimNextAssetRegistryTag);
	return FAnimNextAssetRegistryExports::StaticStruct()->ImportText(*TagValue, &OutExports, nullptr, PPF_None, nullptr, FAnimNextAssetRegistryExports::StaticStruct()->GetName()) != nullptr;
}

bool FUtils::GetExportedVariablesFromAssetRegistry(TMap<FAssetData, FAnimNextAssetRegistryExports>& OutExports)
{
	TArray<FAssetData> AssetData;
	IAssetRegistry::GetChecked().GetAssetsByTags({UE::AnimNext::ExportsAnimNextAssetRegistryTag}, AssetData);

	for (const FAssetData& Asset : AssetData)
	{
		const FString TagValue = Asset.GetTagValueRef<FString>(UE::AnimNext::ExportsAnimNextAssetRegistryTag);
		FAnimNextAssetRegistryExports AssetExports;
		if (FAnimNextAssetRegistryExports::StaticStruct()->ImportText(*TagValue, &AssetExports, nullptr, PPF_None, nullptr, FAnimNextAssetRegistryExports::StaticStruct()->GetName()) != nullptr)
		{
			OutExports.Add(Asset, MoveTemp(AssetExports));
		}
	}

	return OutExports.Num() > 0;
}

bool FUtils::GetExportedFunctionsForAsset(const FAssetData& InAsset, FAnimNextAssetRegistryExports& OutExports)
{
	const FString TagValue = InAsset.GetTagValueRef<FString>(UE::AnimNext::ExportsAnimNextAssetRegistryTag);
	
	bool bContainsFunctions = false;
	FAnimNextAssetRegistryExports AssetExports;
	if (FAnimNextAssetRegistryExports::StaticStruct()->ImportText(*TagValue, &AssetExports, nullptr, PPF_None, nullptr, FAnimNextAssetRegistryExports::StaticStruct()->GetName()) != nullptr)
	{
		if (AssetExports.PublicHeaders.Num() > 0)
		{
			OutExports = MoveTemp(AssetExports);
			bContainsFunctions = true;
		}
	}

	return bContainsFunctions;
}

bool FUtils::GetExportedFunctionsFromAssetRegistry(FName Tag, TMap<FAssetData, FRigVMGraphFunctionHeaderArray>& OutExports)
{
	TArray<FAssetData> AssetData;
	IAssetRegistry::GetChecked().GetAssetsByTags({ Tag }, AssetData);

	const FArrayProperty* HeadersProperty = CastField<FArrayProperty>(FRigVMGraphFunctionHeaderArray::StaticStruct()->FindPropertyByName(TEXT("Headers")));

	for (const FAssetData& Asset : AssetData)
	{
		const FString TagValue = Asset.GetTagValueRef<FString>(Tag);
		FRigVMGraphFunctionHeaderArray AssetExports;

		if (HeadersProperty->ImportText_Direct(*TagValue, &AssetExports, nullptr, EPropertyPortFlags::PPF_None) != nullptr)
		{
			if (AssetExports.Headers.Num() > 0)
			{
				OutExports.Add(Asset, MoveTemp(AssetExports));
			}
		}
	}

	return OutExports.Num() > 0;
}

static void AddParamToSet(const FAnimNextAssetRegistryExportedVariable& InNewParam, TSet<FAnimNextAssetRegistryExportedVariable>& OutExports)
{
	if(FAnimNextAssetRegistryExportedVariable* ExistingEntry = OutExports.Find(InNewParam))
	{
		if(ExistingEntry->Type != InNewParam.Type)
		{
			UE_LOGFMT(LogAnimation, Warning, "Type mismatch between parameter {ParameterName}. {ParamType1} vs {ParamType1}", InNewParam.Name, InNewParam.Type.ToString(), ExistingEntry->Type.ToString());
		}
		ExistingEntry->Flags |= InNewParam.Flags;
	}
	else
	{
		OutExports.Add(InNewParam);
	}
}

void FUtils::GetAssetVariables(const UAnimNextRigVMAssetEditorData* EditorData, FAnimNextAssetRegistryExports& OutExports)
{
	OutExports.Variables.Reset();
	OutExports.Variables.Reserve(EditorData->Entries.Num());

	TSet<FAnimNextAssetRegistryExportedVariable> ExportSet;
	GetAssetVariables(EditorData, ExportSet);
	OutExports.Variables = ExportSet.Array();
}

void FUtils::GetAssetVariables(const UAnimNextRigVMAssetEditorData* InEditorData, TSet<FAnimNextAssetRegistryExportedVariable>& OutExports)
{
	for(const UAnimNextRigVMAssetEntry* Entry : InEditorData->Entries)
	{
		if(const IAnimNextRigVMExportInterface* ExportInterface = Cast<IAnimNextRigVMExportInterface>(Entry))
		{
			EAnimNextExportedVariableFlags Flags = EAnimNextExportedVariableFlags::Declared;
			if(ExportInterface->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Public)
			{
				Flags |= EAnimNextExportedVariableFlags::Public;
				FAnimNextAssetRegistryExportedVariable NewParam(ExportInterface->GetExportName(), ExportInterface->GetExportType(), Flags);
				AddParamToSet(NewParam, OutExports);
			}
		}
		else if(const UAnimNextDataInterfaceEntry* DataInterfaceEntry = Cast<UAnimNextDataInterfaceEntry>(Entry))
		{
			if(DataInterfaceEntry->DataInterface)
			{
				UAnimNextDataInterface_EditorData* EditorData = GetEditorData<UAnimNextDataInterface_EditorData>(DataInterfaceEntry->DataInterface.Get());
				GetAssetVariables(EditorData, OutExports);
			}
		}
	}
}

void FUtils::GetAssetOutlinerItems(const UAnimNextRigVMAssetEditorData* EditorData, FWorkspaceOutlinerItemExports& OutExports)
{
	FWorkspaceOutlinerItemExport AssetIdentifier = FWorkspaceOutlinerItemExport(EditorData->GetOuter()->GetFName(), EditorData->GetOuter());
	for(UAnimNextRigVMAssetEntry* Entry : EditorData->Entries)
	{
		if(IAnimNextRigVMGraphInterface* GraphInterface = Cast<IAnimNextRigVMGraphInterface>(Entry))
		{
			if (Entry->IsHiddenInOutliner())
			{
				if (URigVMEdGraph* RigVMEdGraph = GraphInterface->GetEdGraph())
				{
					CreateSubGraphsOutlinerItemsRecursive(EditorData, OutExports, AssetIdentifier, RigVMEdGraph);
				}
			}
			else
			{
				FWorkspaceOutlinerItemExport& Export = OutExports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(Entry->GetEntryName(), AssetIdentifier));

				Export.GetData().InitializeAsScriptStruct(FAnimNextGraphOutlinerData::StaticStruct());
				FAnimNextGraphOutlinerData& GraphData = Export.GetData().GetMutable<FAnimNextGraphOutlinerData>();
				GraphData.Entry = Entry;
				GraphData.GraphInterface = GraphInterface->_getUObject();

				if (URigVMEdGraph* RigVMEdGraph = GraphInterface->GetEdGraph())
				{
					CreateSubGraphsOutlinerItemsRecursive(EditorData, OutExports, Export, RigVMEdGraph);
				}
			}
		}
	}

	//CreateFunctionLibraryOutlinerItemsRecursive(EditorData, OutExports, AssetIdentifier, EditorData->GetRigVMGraphFunctionStore()->PublicFunctions, EditorData->GetRigVMGraphFunctionStore()->PrivateFunctions);
}

void FUtils::CreateSubGraphsOutlinerItemsRecursive(const UAnimNextRigVMAssetEditorData* EditorData, FWorkspaceOutlinerItemExports& OutExports, FWorkspaceOutlinerItemExport& ParentExport, URigVMEdGraph* RigVMEdGraph)
{
	if (RigVMEdGraph == nullptr)
	{
		return;
	}

	// ---- Collapsed graphs ---
	for (const TObjectPtr<UEdGraph>& SubGraph : RigVMEdGraph->SubGraphs)
	{
		URigVMEdGraph* EditorObject = Cast<URigVMEdGraph>(SubGraph);
		if (IsValid(EditorObject))
		{
			if(ensure(EditorObject->GetModel()))
			{
				URigVMCollapseNode* CollapseNode = CastChecked<URigVMCollapseNode>(EditorObject->GetModel()->GetOuter());

				FWorkspaceOutlinerItemExport& Export = OutExports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(CollapseNode->GetFName(), ParentExport));
				Export.GetData().InitializeAsScriptStruct(FAnimNextCollapseGraphOutlinerData::StaticStruct());
			
				FAnimNextCollapseGraphOutlinerData& FnGraphData = Export.GetData().GetMutable<FAnimNextCollapseGraphOutlinerData>();
				FnGraphData.EditorObject = EditorObject;

				CreateSubGraphsOutlinerItemsRecursive(EditorData, OutExports, Export, EditorObject);
			}
		}
	}

	// ---- Function References ---
	TArray<URigVMEdGraphNode*> EdNodes;
	RigVMEdGraph->GetNodesOfClass(EdNodes);

	for (URigVMEdGraphNode* EdNode : EdNodes)
	{
		if (URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(EdNode->GetModelNode()))
		{
			if (URigVMLibraryNode* ReferencedNode = Cast<URigVMLibraryNode>(FunctionReferenceNode->GetReferencedFunctionHeader().LibraryPointer.GetNodeSoftPath().ResolveObject()))
			{
				if (URigVMGraph* ContainedGraph = ReferencedNode->GetContainedGraph())
				{
					if (EditorData->GetEditorObjectForRigVMGraph(ContainedGraph) == nullptr)
					{
						continue; // Do not show references to other assets functions in the outliner
					}

					FWorkspaceOutlinerItemExport& Export = OutExports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(ReferencedNode->GetFName(), ParentExport));

					Export.GetData().InitializeAsScriptStruct(FAnimNextGraphFunctionOutlinerData::StaticStruct());
					FAnimNextGraphFunctionOutlinerData& FnGraphData = Export.GetData().GetMutable<FAnimNextGraphFunctionOutlinerData>();

					if (URigVMEdGraph* ContainedEdGraph = Cast<URigVMEdGraph>(EditorData->GetEditorObjectForRigVMGraph(ContainedGraph)))
					{
						FnGraphData.EditorObject = ContainedEdGraph;
						FnGraphData.EdGraphNode = EdNode;
						
						CreateSubGraphsOutlinerItemsRecursive(EditorData, OutExports, Export, ContainedEdGraph);
					}
				}
			}
		}
	}
}

void FUtils::CreateFunctionLibraryOutlinerItemsRecursive(const UAnimNextRigVMAssetEditorData* EditorData, FWorkspaceOutlinerItemExports& OutExports, FWorkspaceOutlinerItemExport& ParentExport, const TArray<FRigVMGraphFunctionData>& PublicFunctions, const TArray<FRigVMGraphFunctionData>& PrivateFunctions)
{
	if (PrivateFunctions.Num() > 0 || PublicFunctions.Num() > 0)
	{
		FWorkspaceOutlinerItemExport& Export = OutExports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(*GetFunctionLibraryDisplayName().ToString(), ParentExport));

		CreateFunctionsOutlinerItemsRecursive(EditorData, OutExports, Export, PrivateFunctions, false);
		CreateFunctionsOutlinerItemsRecursive(EditorData, OutExports, Export, PublicFunctions, true);
	}
}

void FUtils::CreateFunctionsOutlinerItemsRecursive(const UAnimNextRigVMAssetEditorData* EditorData, FWorkspaceOutlinerItemExports& OutExports, FWorkspaceOutlinerItemExport& ParentExport, const TArray<FRigVMGraphFunctionData>& Functions, bool bPublicFunctions)
{
	for (const FRigVMGraphFunctionData& FunctionData : Functions)
	{
		if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(FunctionData.Header.LibraryPointer.GetNodeSoftPath().ResolveObject()))
		{
			if (URigVMGraph* ContainedModelGraph = LibraryNode->GetContainedGraph())
			{
				if (URigVMEdGraph* EditorObject = Cast<URigVMEdGraph>(EditorData->GetEditorObjectForRigVMGraph(ContainedModelGraph)))
				{
					FWorkspaceOutlinerItemExport& Export = OutExports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(FunctionData.Header.Name, ParentExport));

					Export.GetData().InitializeAsScriptStruct(FAnimNextGraphFunctionOutlinerData::StaticStruct());
					FAnimNextGraphFunctionOutlinerData& FnGraphData = Export.GetData().GetMutable<FAnimNextGraphFunctionOutlinerData>();
					FnGraphData.EditorObject = EditorObject;
				}
			}
		}
	}
}

const FText& FUtils::GetFunctionLibraryDisplayName()
{
	static const FText FunctionLibraryName = LOCTEXT("WorkspaceFunctionLibraryName", "Function Library");
	return FunctionLibraryName;
}

#if WITH_EDITOR
void FUtils::OpenProgrammaticGraphs(UAnimNextRigVMAssetEditorData* EditorData, const TArray<URigVMGraph*>& ProgrammaticGraphs)
{
	UAnimNextRigVMAsset* OwningAsset = FUtils::GetAsset(EditorData);
	UE::Workspace::IWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::LoadModuleChecked<UE::Workspace::IWorkspaceEditorModule>("WorkspaceEditor");
	if(UE::Workspace::IWorkspaceEditor* WorkspaceEditor = WorkspaceEditorModule.OpenWorkspaceForObject(OwningAsset, UE::Workspace::EOpenWorkspaceMethod::Default))
	{
		TArray<UObject*> Graphs;
		for(URigVMGraph* ProgrammaticGraph : ProgrammaticGraphs)
		{
			// Some explanation needed here!
			// URigVMEdGraph caches its underlying model internally in GetModel depending on its outer if it is no attached to a RigVMClient
			// So here we rename the graph into the transient package so we dont get any notifications
			ProgrammaticGraph->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);

			// then create the graph (transient so it outers to the RigVMGraph)
			URigVMEdGraph* EdGraph = CastChecked<URigVMEdGraph>(EditorData->CreateEdGraph(ProgrammaticGraph, true));

			// Then cache the model
			EdGraph->GetModel();
			Graphs.Add(EdGraph);

			// Now rename into this asset again to be able to correctly create a controller (needed to view the graph and interact with it)
			ProgrammaticGraph->Rename(nullptr, EditorData, REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
			URigVMController* ProgrammaticController = EditorData->GetOrCreateController(ProgrammaticGraph);

			// Resend notifications to rebuild the EdGraph
			ProgrammaticController->ResendAllNotifications();
		}

		WorkspaceEditor->OpenObjects(Graphs);
	}
}
#endif // WITH_EDITOR
}

#undef LOCTEXT_NAMESPACE