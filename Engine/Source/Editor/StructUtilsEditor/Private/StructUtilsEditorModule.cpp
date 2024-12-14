// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructUtilsEditorModule.h"
#include "CoreGlobals.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "PropertyBagDetails.h"
#include "PropertyEditorModule.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/InstancedStructContainer.h"
#include "StructUtils/UserDefinedStruct.h"
#include "StructUtilsDelegates.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

#define LOCTEXT_NAMESPACE "StructUtilsEditor"

IMPLEMENT_MODULE(FStructUtilsEditorModule, StructUtilsEditor)

void FStructUtilsEditorModule::StartupModule()
{
	// Register the details customizer
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout("InstancedStruct", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FInstancedStructDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("InstancedPropertyBag", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPropertyBagDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
}

void FStructUtilsEditorModule::ShutdownModule()
{
	// Unregister the details customization
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout("InstancedStruct");
		PropertyModule.UnregisterCustomPropertyTypeLayout("InstancedPropertyBag");
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

namespace UE::StructUtils::Private
{

static void VisitReferencedObjects(const UUserDefinedStruct* StructToReinstantiate)
{
	// Helper preference collector, does not collect anything, but makes sure AddStructReferencedObjects() gets called e.g. on instanced struct. 
	class FVisitorReferenceCollector : public FReferenceCollector
	{
	public:
		virtual bool IsIgnoringArchetypeRef() const override { return false; }
		virtual bool IsIgnoringTransient() const override { return false; }

		virtual void HandleObjectReference(UObject*& Object, const UObject* ReferencingObject, const FProperty* ReferencingProperty) override
		{
			// Empty
		}
	};

	FVisitorReferenceCollector Collector;

	// This sets global variable which read in the AddStructReferencedObjects().
	FStructureToReinstantiateScope StructureToReinstantiateScope(StructToReinstantiate);

	for (TObjectIterator<UObject> ObjectIt; ObjectIt; ++ObjectIt)
	{
		UObject* Object = *ObjectIt;
	
		// This sets global variable which read in the AddStructReferencedObjects().
		FCurrentReinstantiationOuterObjectScope CurrentReinstantiateOuterObjectScope(Object);
	
		Collector.AddPropertyReferencesWithStructARO(Object->GetClass(), Object);
	}

	// Handle CDOs and sparse class data 
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;

		// Handle Sparse class data
		if (void* SparseData = const_cast<void*>(Class->GetSparseClassData(EGetSparseClassDataMethod::ReturnIfNull)))
		{
			FCurrentReinstantiationOuterObjectScope CurrentReinstantiateOuterObjectScope(Class);
			const UScriptStruct* SparseDataStruct = Class->GetSparseClassDataStruct();
			Collector.AddPropertyReferencesWithStructARO(SparseDataStruct, SparseData);
		}

		// Handle CDO
		if (UObject* CDO = Class->GetDefaultObject())
		{
			FCurrentReinstantiationOuterObjectScope CurrentReinstantiateOuterObjectScope(CDO);
			Collector.AddPropertyReferencesWithStructARO(Class, CDO);
		}
	}
}

} // UE::StructUtils::Private

void FStructUtilsEditorModule::PreChange(const UUserDefinedStruct* StructToReinstantiate, FStructureEditorUtils::EStructureEditorChangeInfo Info)
{
	if (!StructToReinstantiate)
	{
		return;
	}

	// Make a duplicate of the existing struct, and point all instances of the struct to point to the duplicate.
	// This is done because the original struct will be changed.
	UUserDefinedStruct* DuplicatedStruct = nullptr;
	{
		const FString ReinstantiatedName = FString::Printf(TEXT("STRUCT_REINST_%s"), *StructToReinstantiate->GetName());
		const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UUserDefinedStruct::StaticClass(), FName(*ReinstantiatedName));

		TGuardValue<FIsDuplicatingClassForReinstancing, bool> IsDuplicatingClassForReinstancing(GIsDuplicatingClassForReinstancing, true);
		DuplicatedStruct = (UUserDefinedStruct*)StaticDuplicateObject(StructToReinstantiate, GetTransientPackage(), UniqueName, ~RF_Transactional); 

		DuplicatedStruct->Guid = StructToReinstantiate->Guid;
		DuplicatedStruct->Bind();
		DuplicatedStruct->StaticLink(true);
		DuplicatedStruct->PrimaryStruct = const_cast<UUserDefinedStruct*>(StructToReinstantiate);
		DuplicatedStruct->Status = EUserDefinedStructureStatus::UDSS_Duplicate;
		DuplicatedStruct->SetFlags(RF_Transient);
		DuplicatedStruct->AddToRoot();
	}

	UUserDefinedStructEditorData* DuplicatedEditorData = CastChecked<UUserDefinedStructEditorData>(DuplicatedStruct->EditorData);
	DuplicatedEditorData->RecreateDefaultInstance();

	UE::StructUtils::Private::VisitReferencedObjects(DuplicatedStruct);
	
	DuplicatedStruct->RemoveFromRoot();
}

void FStructUtilsEditorModule::PostChange(const UUserDefinedStruct* StructToReinstantiate, FStructureEditorUtils::EStructureEditorChangeInfo Info)
{
	if (!StructToReinstantiate)
	{
		return;
	}

	UE::StructUtils::Private::VisitReferencedObjects(StructToReinstantiate);

	if (UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.IsBound())
	{
		UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.Broadcast(*StructToReinstantiate);
	}
}

#undef LOCTEXT_NAMESPACE
