// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/CEClonerSubsystem.h"

#include "Cloner/CEClonerActor.h"
#include "Cloner/CEClonerComponent.h"
#include "Cloner/Extensions/CEClonerEffectorExtension.h"
#include "Cloner/Extensions/CEClonerExtensionBase.h"
#include "Cloner/Layouts/CEClonerCircleLayout.h"
#include "Cloner/Layouts/CEClonerCylinderLayout.h"
#include "Cloner/Layouts/CEClonerGridLayout.h"
#include "Cloner/Layouts/CEClonerHoneycombLayout.h"
#include "Cloner/Layouts/CEClonerLayoutBase.h"
#include "Cloner/Layouts/CEClonerLineLayout.h"
#include "Cloner/Layouts/CEClonerMeshLayout.h"
#include "Cloner/Layouts/CEClonerSphereRandomLayout.h"
#include "Cloner/Layouts/CEClonerSphereUniformLayout.h"
#include "Cloner/Layouts/CEClonerSplineLayout.h"
#include "Engine/Engine.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#endif

UCEClonerSubsystem::FOnSubsystemInitialized UCEClonerSubsystem::OnSubsystemInitializedDelegate;
UCEClonerSubsystem::FOnClonerSetEnabled UCEClonerSubsystem::OnClonerSetEnabledDelegate;

#define LOCTEXT_NAMESPACE "CEEffectorSubsystem"

UCEClonerSubsystem* UCEClonerSubsystem::Get()
{
	if (GEngine)
	{
		return GEngine->GetEngineSubsystem<UCEClonerSubsystem>();
	}

	return nullptr;
}

void UCEClonerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Register them here to match old order of layout enum
	RegisterLayoutClass(UCEClonerGridLayout::StaticClass());
	RegisterLayoutClass(UCEClonerLineLayout::StaticClass());
	RegisterLayoutClass(UCEClonerCircleLayout::StaticClass());
	RegisterLayoutClass(UCEClonerCylinderLayout::StaticClass());
	RegisterLayoutClass(UCEClonerSphereUniformLayout::StaticClass());
	RegisterLayoutClass(UCEClonerHoneycombLayout::StaticClass());
	RegisterLayoutClass(UCEClonerMeshLayout::StaticClass());
	RegisterLayoutClass(UCEClonerSplineLayout::StaticClass());
	RegisterLayoutClass(UCEClonerSphereRandomLayout::StaticClass());

	// Scan for new layouts
	ScanForRegistrableClasses();

	OnSubsystemInitializedDelegate.Broadcast();
}

bool UCEClonerSubsystem::RegisterLayoutClass(UClass* InClonerLayoutClass)
{
	if (!IsValid(InClonerLayoutClass))
	{
		return false;
	}

	if (!InClonerLayoutClass->IsChildOf(UCEClonerLayoutBase::StaticClass())
		|| InClonerLayoutClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return false;
	}

	if (IsLayoutClassRegistered(InClonerLayoutClass))
	{
		return false;
	}

	const UCEClonerLayoutBase* CDO = InClonerLayoutClass->GetDefaultObject<UCEClonerLayoutBase>();

	if (!CDO)
	{
		return false;
	}

	// Check niagara asset is valid
	if (!CDO->IsLayoutValid())
	{
		return false;
	}

	// Does not overwrite existing layouts
	const FName LayoutName = CDO->GetLayoutName();
	if (LayoutName.IsNone() || LayoutClasses.Contains(LayoutName))
	{
		return false;
	}

	LayoutClasses.Add(LayoutName, CDO->GetClass());

	return true;
}

bool UCEClonerSubsystem::UnregisterLayoutClass(UClass* InClonerLayoutClass)
{
	if (!IsValid(InClonerLayoutClass))
	{
		return false;
	}

	TSubclassOf<UCEClonerLayoutBase> LayoutClass(InClonerLayoutClass);
	if (const FName* LayoutName = LayoutClasses.FindKey(LayoutClass))
	{
		LayoutClasses.Remove(*LayoutName);
		return true;
	}

	return false;
}

bool UCEClonerSubsystem::IsLayoutClassRegistered(UClass* InClonerLayoutClass)
{
	if (!IsValid(InClonerLayoutClass))
	{
		return false;
	}

	TSubclassOf<UCEClonerLayoutBase> LayoutClass(InClonerLayoutClass);
	if (const FName* LayoutName = LayoutClasses.FindKey(LayoutClass))
	{
		return true;
	}

	return false;
}

void UCEClonerSubsystem::RegisterCustomActorResolver(FOnGetOrderedActors InCustomResolver)
{
	ActorResolver = InCustomResolver;
}

void UCEClonerSubsystem::UnregisterCustomActorResolver()
{
	ActorResolver = FOnGetOrderedActors();
}

UCEClonerSubsystem::FOnGetOrderedActors& UCEClonerSubsystem::GetCustomActorResolver()
{
	return ActorResolver;
}

bool UCEClonerSubsystem::RegisterExtensionClass(UClass* InClass)
{
	if (!IsValid(InClass))
	{
		return false;
	}

	if (!InClass->IsChildOf(UCEClonerExtensionBase::StaticClass())
		|| InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return false;
	}

	if (IsExtensionClassRegistered(InClass))
	{
		return false;
	}

	const UCEClonerExtensionBase* CDO = InClass->GetDefaultObject<UCEClonerExtensionBase>();

	if (!CDO)
	{
		return false;
	}

	const FName ExtensionName = CDO->GetExtensionName();
	if (ExtensionName.IsNone() || ExtensionClasses.Contains(ExtensionName))
	{
		return false;
	}

	ExtensionClasses.Add(ExtensionName, CDO->GetClass());

	return true;
}

bool UCEClonerSubsystem::UnregisterExtensionClass(UClass* InClass)
{
	if (!IsValid(InClass))
	{
		return false;
	}

	TSubclassOf<UCEClonerExtensionBase> ExtensionClass(InClass);
	if (const FName* ExtensionName = ExtensionClasses.FindKey(ExtensionClass))
	{
		ExtensionClasses.Remove(*ExtensionName);
		return true;
	}

	return false;
}

bool UCEClonerSubsystem::IsExtensionClassRegistered(UClass* InClass) const
{
	if (!IsValid(InClass))
	{
		return false;
	}

	TSubclassOf<UCEClonerExtensionBase> ExtensionClass(InClass);
	return !!ExtensionClasses.FindKey(ExtensionClass);
}

TSet<FName> UCEClonerSubsystem::GetExtensionNames() const
{
	TArray<FName> ExtensionNames;
	ExtensionClasses.GenerateKeyArray(ExtensionNames);
	return TSet<FName>(ExtensionNames);
}

TSet<TSubclassOf<UCEClonerExtensionBase>> UCEClonerSubsystem::GetExtensionClasses() const
{
	TArray<TSubclassOf<UCEClonerExtensionBase>> Extensions;
	ExtensionClasses.GenerateValueArray(Extensions);
	return TSet<TSubclassOf<UCEClonerExtensionBase>>(Extensions);
}

FName UCEClonerSubsystem::FindExtensionName(TSubclassOf<UCEClonerExtensionBase> InClass) const
{
	if (const FName* Key = ExtensionClasses.FindKey(InClass))
	{
		return *Key;
	}

	return NAME_None;
}

UCEClonerExtensionBase* UCEClonerSubsystem::CreateNewExtension(FName InExtensionName, UCEClonerComponent* InCloner)
{
	if (!IsValid(InCloner))
	{
		return nullptr;
	}

	TSubclassOf<UCEClonerExtensionBase> const* ExtensionClass = ExtensionClasses.Find(InExtensionName);

	if (!ExtensionClass)
	{
		return nullptr;
	}

	return NewObject<UCEClonerExtensionBase>(InCloner, ExtensionClass->Get(), NAME_None, RF_Transactional);
}

void UCEClonerSubsystem::SetClonersEnabled(const TSet<UCEClonerComponent*>& InCloners, bool bInEnable, bool bInShouldTransact)
{
	if (InCloners.IsEmpty())
	{
		return;
	}

#if WITH_EDITOR
	const FText TransactionText = bInEnable
		? LOCTEXT("SetClonersEnabled", "Cloners enabled")
		: LOCTEXT("SetClonersDisabled", "Cloners disabled");

	FScopedTransaction Transaction(TransactionText, bInShouldTransact);
#endif

	for (UCEClonerComponent* Cloner : InCloners)
	{
		if (!IsValid(Cloner))
		{
			continue;
		}

#if WITH_EDITOR
		Cloner->Modify();
#endif

		Cloner->SetEnabled(bInEnable);
	}
}

void UCEClonerSubsystem::SetLevelClonersEnabled(const UWorld* InWorld, bool bInEnable, bool bInShouldTransact)
{
	if (!IsValid(InWorld))
	{
		return;
	}

#if WITH_EDITOR
	const FText TransactionText = bInEnable
		? LOCTEXT("SetLevelClonersEnabled", "Level cloners enabled")
		: LOCTEXT("SetLevelClonersDisabled", "Level cloners disabled");

	FScopedTransaction Transaction(TransactionText, bInShouldTransact);
#endif

	OnClonerSetEnabledDelegate.Broadcast(InWorld, bInEnable, bInShouldTransact);
}

#if WITH_EDITOR
void UCEClonerSubsystem::ConvertCloners(const TSet<UCEClonerComponent*>& InCloners, ECEClonerMeshConversion InMeshConversion)
{
	if (InCloners.IsEmpty())
	{
		return;
	}

	using namespace UE::ClonerEffector::Conversion;

	for (UCEClonerComponent* ClonerComponent : InCloners)
	{
		if (!IsValid(ClonerComponent) || !ClonerComponent->GetEnabled())
		{
			continue;
		}

		switch(InMeshConversion)
		{
			case ECEClonerMeshConversion::StaticMesh:
				ClonerComponent->ConvertToStaticMesh();
			break;

			case ECEClonerMeshConversion::StaticMeshes:
				ClonerComponent->ConvertToStaticMeshes();
			break;

			case ECEClonerMeshConversion::DynamicMesh:
				ClonerComponent->ConvertToDynamicMesh();
			break;

			case ECEClonerMeshConversion::DynamicMeshes:
				ClonerComponent->ConvertToDynamicMeshes();
			break;

			case ECEClonerMeshConversion::InstancedStaticMesh:
				ClonerComponent->ConvertToInstancedStaticMeshes();
			break;

			default:;
		}
	}
}

void UCEClonerSubsystem::CreateLinkedEffector(const TSet<UCEClonerComponent*>& InCloners)
{
	if (InCloners.IsEmpty())
	{
		return;
	}

	for (UCEClonerComponent* ClonerComponent : InCloners)
	{
		if (!IsValid(ClonerComponent))
		{
			continue;
		}

		if (UCEClonerEffectorExtension* EffectorExtension = ClonerComponent->GetExtension<UCEClonerEffectorExtension>())
		{
			EffectorExtension->CreateLinkedEffector();
		}
	}
}
#endif

AActor* UCEClonerSubsystem::CreateClonerWithActors(UWorld* InWorld, const TSet<AActor*>& InActors, bool bInShouldTransact)
{
	ACEClonerActor* NewClonerActor = nullptr;

	if (!IsValid(InWorld))
	{
		return NewClonerActor;
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(LOCTEXT("CreateClonerWithActors", "Create cloner with actors attached"), bInShouldTransact);
#endif

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags = RF_Transactional;
#if WITH_EDITOR
	Parameters.bTemporaryEditorActor = false;
#endif

	NewClonerActor = InWorld->SpawnActor<ACEClonerActor>(Parameters);

	if (NewClonerActor)
	{
#if WITH_EDITOR
		NewClonerActor->Modify();
#endif

		if (!InActors.IsEmpty())
		{
			FVector NewAverageLocation;

			for (AActor* Actor : InActors)
			{
				if (IsValid(Actor))
				{
					NewAverageLocation += Actor->GetActorLocation() / InActors.Num();
				}
			}

			NewClonerActor->SetActorLocation(NewAverageLocation);

			for (AActor* Actor : InActors)
			{
				if (IsValid(Actor))
				{
#if WITH_EDITOR
					Actor->Modify();
#endif

					Actor->AttachToActor(NewClonerActor, FAttachmentTransformRules::KeepWorldTransform);
				}
			}
		}

#if WITH_EDITOR
		if (GEditor)
		{
			GEditor->SelectNone(/** SelectionChange */false, /** DeselectBSP */true);
			GEditor->SelectActor(NewClonerActor, /** Selected */true, /** Notify */true);
		}
#endif
	}

	return NewClonerActor;
}

TSet<FName> UCEClonerSubsystem::GetLayoutNames() const
{
	TArray<FName> LayoutNames;
	LayoutClasses.GenerateKeyArray(LayoutNames);
	return TSet<FName>(LayoutNames);
}

TSet<TSubclassOf<UCEClonerLayoutBase>> UCEClonerSubsystem::GetLayoutClasses() const
{
	TArray<TSubclassOf<UCEClonerLayoutBase>> Layouts;
	LayoutClasses.GenerateValueArray(Layouts);
	return TSet<TSubclassOf<UCEClonerLayoutBase>>(Layouts);
}

FName UCEClonerSubsystem::FindLayoutName(TSubclassOf<UCEClonerLayoutBase> InLayoutClass) const
{
	if (const FName* Key = LayoutClasses.FindKey(InLayoutClass))
	{
		return *Key;
	}

	return NAME_None;
}

TSubclassOf<UCEClonerLayoutBase> UCEClonerSubsystem::FindLayoutClass(FName InLayoutName) const
{
	if (const TSubclassOf<UCEClonerLayoutBase>* Value = LayoutClasses.Find(InLayoutName))
	{
		return *Value;
	}

	return TSubclassOf<UCEClonerLayoutBase>();
}

UCEClonerLayoutBase* UCEClonerSubsystem::CreateNewLayout(FName InLayoutName, UCEClonerComponent* InCloner)
{
	if (!IsValid(InCloner))
	{
		return nullptr;
	}

	TSubclassOf<UCEClonerLayoutBase> const* LayoutClass = LayoutClasses.Find(InLayoutName);

	if (!LayoutClass)
	{
		return nullptr;
	}

	return NewObject<UCEClonerLayoutBase>(InCloner, LayoutClass->Get());
}

void UCEClonerSubsystem::ScanForRegistrableClasses()
{
	{
		TArray<UClass*> DerivedLayoutClasses;
		GetDerivedClasses(UCEClonerLayoutBase::StaticClass(), DerivedLayoutClasses, true);

		for (UClass* LayoutClass : DerivedLayoutClasses)
		{
			RegisterLayoutClass(LayoutClass);
		}
	}

	{
		TArray<UClass*> DerivedExtensionClasses;
		GetDerivedClasses(UCEClonerExtensionBase::StaticClass(), DerivedExtensionClasses, true);

		for (UClass* ExtensionClass : DerivedExtensionClasses)
		{
			RegisterExtensionClass(ExtensionClass);
		}
	}
}

#undef LOCTEXT_NAMESPACE