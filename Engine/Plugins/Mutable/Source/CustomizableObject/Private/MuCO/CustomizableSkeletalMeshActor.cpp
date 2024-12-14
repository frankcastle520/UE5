// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/CustomizableSkeletalMeshActor.h"

#include "Components/SkeletalMeshComponent.h"
#include "MuCO/CustomizableSkeletalComponent.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectSystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableSkeletalMeshActor)

#define LOCTEXT_NAMESPACE "CustomizableObject"


ACustomizableSkeletalMeshActor::ACustomizableSkeletalMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CustomizableSkeletalComponent = CreateDefaultSubobject<UCustomizableSkeletalComponent>(TEXT("CustomizableSkeletalComponent0"));
	CustomizableSkeletalComponents.Add(CustomizableSkeletalComponent);
	if (USkeletalMeshComponent* SkeletalMeshComp = GetSkeletalMeshComponent()) 
	{
		SkeletalMeshComponents.Add(SkeletalMeshComp);
		bool Success = CustomizableSkeletalComponents[0]->AttachToComponent(SkeletalMeshComp, FAttachmentTransformRules::KeepRelativeTransform);
	}
}


void ACustomizableSkeletalMeshActor::AttachNewComponent()
{
	int32 CurrentIndex = CustomizableSkeletalComponents.Num();
	FString CustomizableComponentName = FString::Printf(TEXT("CustomizableSkeletalComponent%d"), CurrentIndex);
	FString SkeletalMeshComponentName = FString::Printf(TEXT("SkeletalMeshComponent%d"), CurrentIndex);

	//USkeletalMeshComponent* SkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(FName(*SkeletalMeshComponentName));
	USkeletalMeshComponent* SkeletalMesh = NewObject<USkeletalMeshComponent>(this, USkeletalMeshComponent::StaticClass(), FName(*SkeletalMeshComponentName));
	USkeletalMeshComponent* RootSkeletalMesh = GetSkeletalMeshComponent();
	
	if (SkeletalMesh && RootSkeletalMesh)
	{		
		bool Success = SkeletalMesh->AttachToComponent(RootSkeletalMesh, FAttachmentTransformRules::KeepRelativeTransform);
		if (Success)
		{
			//UCustomizableSkeletalComponent* CustomizableSkeletalComponent = CreateDefaultSubobject<UCustomizableSkeletalComponent>(FName(*CustomizableComponentName));
			UCustomizableSkeletalComponent* NewCustomizableSkeletalComponent = NewObject<UCustomizableSkeletalComponent>(this, UCustomizableSkeletalComponent::StaticClass(), FName(*CustomizableComponentName));

			if (NewCustomizableSkeletalComponent)
			{
				Success = NewCustomizableSkeletalComponent->AttachToComponent(SkeletalMesh, FAttachmentTransformRules::KeepRelativeTransform);

				if (Success)
				{
					SkeletalMeshComponents.Add(SkeletalMesh);
					CustomizableSkeletalComponents.Add(NewCustomizableSkeletalComponent);
				}
			}
		}
	}
}


UCustomizableObjectInstance* ACustomizableSkeletalMeshActor::GetComponentsCommonInstance()
{
	for (const UCustomizableSkeletalComponent* Component : CustomizableSkeletalComponents)
	{
		if (UCustomizableObjectInstance* COInstance = Component->CustomizableObjectInstance)
		{
			return COInstance;
		}
	}

	return nullptr;
}


void ACustomizableSkeletalMeshActor::SetDebugMaterial(UMaterialInterface* InDebugMaterial)
{
	if (!InDebugMaterial)
	{
		return;
	}

	DebugMaterial = InDebugMaterial;
}


void ACustomizableSkeletalMeshActor::EnableDebugMaterial(bool bEnableDebugMaterial)
{
	bRemoveDebugMaterial = bDebugMaterialEnabled && !bEnableDebugMaterial;
	bDebugMaterialEnabled = bEnableDebugMaterial;

	if (UCustomizableObjectInstance* COInstance = GetComponentsCommonInstance())
	{
		//Bind Instance Update delegate to Actor
		COInstance->UpdatedDelegate.AddUniqueDynamic(this, &ACustomizableSkeletalMeshActor::SwitchComponentsMaterials);
		SwitchComponentsMaterials(COInstance);
	}
}


void ACustomizableSkeletalMeshActor::SwitchComponentsMaterials(UCustomizableObjectInstance* Instance)
{
	if (!DebugMaterial)
	{
		return;
	}

	if (bDebugMaterialEnabled || bRemoveDebugMaterial)
	{
		UCustomizableObjectInstance* COInstance = GetComponentsCommonInstance();

		if (!COInstance)
		{
			return;
		}

		for (int32 CompIndex = 0; CompIndex < SkeletalMeshComponents.Num(); ++CompIndex)
		{
			int32 NumMaterials = SkeletalMeshComponents[CompIndex]->GetNumMaterials();

			if (bDebugMaterialEnabled)
			{
				for (int32 MatIndex = 0; MatIndex < NumMaterials; ++MatIndex)
				{
					SkeletalMeshComponents[CompIndex]->SetMaterial(MatIndex, DebugMaterial);
				}
			}
			else // Remove debugmaterial
			{
				// check if original materials already overriden
				const TArray<TObjectPtr<UMaterialInterface>>* OverrideMaterials = COInstance->GetOverrideMaterials(CompIndex);
				const bool bUseOverrideMaterials = COInstance->GetCustomizableObject()->bEnableMeshCache && CVarEnableMeshCache.GetValueOnAnyThread();

				if (bUseOverrideMaterials && OverrideMaterials->Num() > 0)
				{
					for (int32 MatIndex = 0; MatIndex < OverrideMaterials->Num(); ++MatIndex)
					{
						SkeletalMeshComponents[CompIndex]->SetMaterial(MatIndex, (*OverrideMaterials)[MatIndex]);
					}
				}
				else
				{
					SkeletalMeshComponents[CompIndex]->EmptyOverrideMaterials();
				}

				bRemoveDebugMaterial = false;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
