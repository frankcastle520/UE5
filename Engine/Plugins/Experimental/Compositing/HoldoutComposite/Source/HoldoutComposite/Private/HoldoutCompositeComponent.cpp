// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoldoutCompositeComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "HoldoutCompositeModule.h"
#include "HoldoutCompositeSettings.h"
#include "HoldoutCompositeSubsystem.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#define LOCTEXT_NAMESPACE "HoldoutComposite"

namespace
{
	// Ensure that the primitive class is allowed by checking against plugin settings.
	bool IsAllowedPrimitiveClass(const UPrimitiveComponent* InPrimitiveComponent)
	{
		const UHoldoutCompositeSettings* Settings = GetDefault<UHoldoutCompositeSettings>();
		if (ensure(Settings))
		{
			for (const FSoftClassPath& ObjectClass : Settings->DisabledPrimitiveClasses)
			{
				UClass* TransactionClass = ObjectClass.TryLoadClass<UObject>();
				if (TransactionClass && InPrimitiveComponent->IsA(TransactionClass))
				{
					return false;
				}
			}
		}

		return true;
	}

	// Find list of primitives from the parent component and its children. We need to traverse children to support objects such as UText3DComponent.
	static TArray<TSoftObjectPtr<UPrimitiveComponent>> FindPrimitiveComponents(USceneComponent* InParentComponent)
	{
		TArray<TSoftObjectPtr<UPrimitiveComponent>> OutPrimitiveComponents;

		if (IsValid(InParentComponent))
		{
			if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(InParentComponent))
			{
				if (IsAllowedPrimitiveClass(PrimitiveComponent))
				{
					OutPrimitiveComponents.Add(PrimitiveComponent);
				}
			}

			TArray<USceneComponent*> ParentChildComponents;
			InParentComponent->GetChildrenComponents(true /*bIncludeAllDescendants*/, ParentChildComponents);

			for (USceneComponent* ParentChild : ParentChildComponents)
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(ParentChild))
				{
					if (IsAllowedPrimitiveClass(PrimitiveComponent))
					{
						OutPrimitiveComponents.Add(PrimitiveComponent);
					}
				}
			}
		}

		return OutPrimitiveComponents;
	}
}

UHoldoutCompositeComponent::UHoldoutCompositeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UHoldoutCompositeComponent::OnRegister()
{
	Super::OnRegister();

	RegisterCompositeImpl();
}

void UHoldoutCompositeComponent::OnUnregister()
{
	UnregisterCompositeImpl();

	Super::OnUnregister();
}

void UHoldoutCompositeComponent::DetachFromComponent(const FDetachmentTransformRules& DetachmentRules)
{
	// Note: We also unregister here while the attached parent pointer is still valid.
	UnregisterCompositeImpl();

	Super::DetachFromComponent(DetachmentRules);
}

void UHoldoutCompositeComponent::OnAttachmentChanged()
{
	Super::OnAttachmentChanged();

	UnregisterCompositeImpl();

	USceneComponent* SceneComponent = GetAttachParent();
	if (IsValid(SceneComponent))
	{
		TArray<TSoftObjectPtr<UPrimitiveComponent>> ParentPrimitives = FindPrimitiveComponents(SceneComponent);
		if (ParentPrimitives.IsEmpty())
		{
#if WITH_EDITOR
			FNotificationInfo Info(LOCTEXT("CompositeParentNotification",
				"The composite component must be parented to a primitive component (or one that has primitives)."));
			Info.ExpireDuration = 5.0f;

			FSlateNotificationManager::Get().AddNotification(Info);
#endif
		}
		else
		{
			RegisterCompositeImpl();
		}
	}
}

bool UHoldoutCompositeComponent::IsEnabled() const
{
	return bIsEnabled;
}

void UHoldoutCompositeComponent::SetEnabled(bool bInIsEnabled)
{
	if (bIsEnabled != bInIsEnabled)
	{
		bIsEnabled = bInIsEnabled;

		if (bIsEnabled)
		{
			RegisterCompositeImpl();
		}
		else
		{
			UnregisterCompositeImpl();
		}
	}
}

void UHoldoutCompositeComponent::RegisterCompositeImpl()
{
	if (!bIsEnabled)
	{
		return;
	}

	UHoldoutCompositeSubsystem* Subsystem = UWorld::GetSubsystem<UHoldoutCompositeSubsystem>(GetWorld());
	TArray<TSoftObjectPtr<UPrimitiveComponent>> ParentPrimitives = FindPrimitiveComponents(GetAttachParent());

	if (IsValid(Subsystem) && !ParentPrimitives.IsEmpty())
	{
		Subsystem->RegisterPrimitives(ParentPrimitives);
	}
}

void UHoldoutCompositeComponent::UnregisterCompositeImpl()
{
	UHoldoutCompositeSubsystem* Subsystem = UWorld::GetSubsystem<UHoldoutCompositeSubsystem>(GetWorld());
	TArray<TSoftObjectPtr<UPrimitiveComponent>> ParentPrimitives = FindPrimitiveComponents(GetAttachParent());

	if (IsValid(Subsystem) && !ParentPrimitives.IsEmpty())
	{
		Subsystem->UnregisterPrimitives(ParentPrimitives);
	}
}

#undef LOCTEXT_NAMESPACE
