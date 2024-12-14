// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/CustomizableSkeletalComponent.h"

#include "MuCO/CustomizableObjectInstanceUsagePrivate.h"
#include "MuCO/CustomizableSkeletalComponentPrivate.h"

#include "MuCO/CustomizableObjectInstanceUsage.h"
#include "UObject/UObjectGlobals.h"
#include "Components/SkeletalMeshComponent.h"
#include "MuCO/CustomizableObject.h"


UCustomizableSkeletalComponentPrivate::UCustomizableSkeletalComponentPrivate()
{
	// This object may get instantiated into a level, as a part of UCustomizableSkeletalComponent, so needs to be public to ensure
	// it can be serialized out.
	if (!HasAllFlags(RF_ClassDefaultObject))
	{
		SetFlags(RF_Public);
	}
}

void UCustomizableSkeletalComponentPrivate::CreateCustomizableObjectInstanceUsage()
{
	if (GetPublic()->CustomizableObjectInstanceUsage)
	{
		// CustomizableObjectInstanceUsage may already exist if duplicated from an existing Customizable Skeletal Component
		if (GetPublic()->CustomizableObjectInstanceUsage->GetPrivate()->GetCustomizableSkeletalComponent() != GetPublic())
		{
			GetPublic()->CustomizableObjectInstanceUsage = nullptr;
		}
	}

	AActor* RootActor = GetPublic()->GetAttachmentRootActor();
	bool bIsDefaultActor = RootActor ?
		RootActor->HasAnyFlags(RF_ClassDefaultObject) :
		false;

	if (!GetPublic()->CustomizableObjectInstanceUsage && !HasAnyFlags(RF_ClassDefaultObject) && !bIsDefaultActor)
	{
		GetPublic()->CustomizableObjectInstanceUsage = NewObject<UCustomizableObjectInstanceUsage>(this, TEXT("InstanceUsage"), RF_Transient);
		GetPublic()->CustomizableObjectInstanceUsage->GetPrivate()->SetCustomizableSkeletalComponent(GetPublic());
	}
}


UCustomizableSkeletalComponent::UCustomizableSkeletalComponent()
{
}


#if WITH_EDITOR
void UCustomizableSkeletalComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!PropertyChangedEvent.MemberProperty)
	{
		return;
	}
	
	const FName PropertyName = PropertyChangedEvent.MemberProperty->GetFName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCustomizableSkeletalComponent, ComponentName))
	{
		SetComponentName(ComponentName);
	}
}
#endif


void UCustomizableSkeletalComponentPrivate::Callbacks() const
{
	if (GetPublic()->CustomizableObjectInstanceUsage)
	{
		GetPublic()->CustomizableObjectInstanceUsage->GetPrivate()->Callbacks();
	}
}


USkeletalMesh* UCustomizableSkeletalComponentPrivate::GetSkeletalMesh() const
{
	if (GetPublic()->CustomizableObjectInstanceUsage)
	{
		return GetPublic()->CustomizableObjectInstanceUsage->GetPrivate()->GetSkeletalMesh();
	}

	return nullptr;
}


void UCustomizableSkeletalComponentPrivate::SetSkeletalMesh(USkeletalMesh* SkeletalMesh)
{
	if (GetPublic()->CustomizableObjectInstanceUsage)
	{
		GetPublic()->CustomizableObjectInstanceUsage->GetPrivate()->SetSkeletalMesh(SkeletalMesh);
	}
}


void UCustomizableSkeletalComponentPrivate::SetPhysicsAsset(UPhysicsAsset* PhysicsAsset)
{
	if (GetPublic()->CustomizableObjectInstanceUsage)
	{
		GetPublic()->CustomizableObjectInstanceUsage->GetPrivate()->SetPhysicsAsset(PhysicsAsset);
	}
}


void UCustomizableSkeletalComponentPrivate::SetPendingSetSkeletalMesh(bool bIsActive)
{
	if (GetPublic()->CustomizableObjectInstanceUsage)
	{
		GetPublic()->CustomizableObjectInstanceUsage->GetPrivate()->SetPendingSetSkeletalMesh(bIsActive);
	}
}


USkeletalMesh* UCustomizableSkeletalComponentPrivate::GetAttachedSkeletalMesh() const
{
	if (GetPublic()->CustomizableObjectInstanceUsage)
	{
		return GetPublic()->CustomizableObjectInstanceUsage->GetPrivate()->GetAttachedSkeletalMesh();
	}

	return nullptr;
}


void UCustomizableSkeletalComponent::SetComponentName(const FName& Name)
{
	ComponentIndex = INDEX_NONE;
	ComponentName = Name;
}


FName UCustomizableSkeletalComponent::GetComponentName() const
{
	if (ComponentIndex == INDEX_NONE)
	{
		return ComponentName;	
	}
	else
	{
		return FName(FString::FromInt(ComponentIndex));
	}
}


UCustomizableObjectInstance* UCustomizableSkeletalComponent::GetCustomizableObjectInstance() const
{
	return CustomizableObjectInstance;
}


void UCustomizableSkeletalComponent::SetCustomizableObjectInstance(UCustomizableObjectInstance* Instance)
{
	CustomizableObjectInstance = Instance;
}


void UCustomizableSkeletalComponent::SetSkipSetReferenceSkeletalMesh(bool bSkip)
{
	bSkipSetReferenceSkeletalMesh = bSkip;
}


bool UCustomizableSkeletalComponent::GetSkipSetReferenceSkeletalMesh() const
{
	return bSkipSetReferenceSkeletalMesh;
}


void UCustomizableSkeletalComponent::SetSkipSetSkeletalMeshOnAttach(bool bSkip)
{
	bSkipSkipSetSkeletalMeshOnAttach = bSkip;
}


bool UCustomizableSkeletalComponent::GetSkipSetSkeletalMeshOnAttach() const
{
	return bSkipSkipSetSkeletalMeshOnAttach;
}


void UCustomizableSkeletalComponent::UpdateSkeletalMeshAsync(bool bNeverSkipUpdate)
{
	if (CustomizableObjectInstanceUsage)
	{
		CustomizableObjectInstanceUsage->UpdateSkeletalMeshAsync(bNeverSkipUpdate);
	}
}


void UCustomizableSkeletalComponent::UpdateSkeletalMeshAsyncResult(FInstanceUpdateDelegate Callback, bool bIgnoreCloseDist, bool bForceHighPriority)
{
	if (CustomizableObjectInstanceUsage)
	{
		CustomizableObjectInstanceUsage->UpdateSkeletalMeshAsyncResult(Callback, bIgnoreCloseDist, bForceHighPriority);
	}
}


UCustomizableSkeletalComponentPrivate* UCustomizableSkeletalComponent::GetPrivate()
{
	check(Private);
	return Private;
}


const UCustomizableSkeletalComponentPrivate* UCustomizableSkeletalComponent::GetPrivate() const
{
	check(Private);
	return Private;
}


#if WITH_EDITOR
void UCustomizableSkeletalComponentPrivate::EditorUpdateComponent()
{
	if (GetPublic()->CustomizableObjectInstanceUsage)
	{
		GetPublic()->CustomizableObjectInstanceUsage->GetPrivate()->EditorUpdateComponent();
	}
}
#endif


bool& UCustomizableSkeletalComponentPrivate::PendingSetSkeletalMesh()
{
	return GetPublic()->bPendingSetSkeletalMesh;	
}


UCustomizableSkeletalComponent* UCustomizableSkeletalComponentPrivate::GetPublic()
{
	UCustomizableSkeletalComponent* Public = StaticCast<UCustomizableSkeletalComponent*>(GetOuter());
	check(Public);

	return Public;
}


const UCustomizableSkeletalComponent* UCustomizableSkeletalComponentPrivate::GetPublic() const
{
	UCustomizableSkeletalComponent* Public = StaticCast<UCustomizableSkeletalComponent*>(GetOuter());
	check(Public);

	return Public;	
}


void UCustomizableSkeletalComponent::OnAttachmentChanged()
{
	Super::OnAttachmentChanged();

	if (Cast<USkeletalMeshComponent>(GetAttachParent()))
	{
		GetPrivate()->SetPendingSetSkeletalMesh(true);
	}
	else if(!GetAttachParent())
	{
		DestroyComponent();
	}
}


void UCustomizableSkeletalComponent::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAllFlags(RF_ClassDefaultObject))
	{
		if (!Private)
		{
			Private = NewObject<UCustomizableSkeletalComponentPrivate>(this, FName("Private"), RF_Public);
		}
		else if (Private->GetOuter() != this)
		{
			Private = Cast<UCustomizableSkeletalComponentPrivate>(StaticDuplicateObject(Private, this, FName("Private")));
		}
	
		GetPrivate()->CreateCustomizableObjectInstanceUsage();
	}
}
