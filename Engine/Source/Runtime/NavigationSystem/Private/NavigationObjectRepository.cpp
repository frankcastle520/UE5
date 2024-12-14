// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavigationObjectRepository.h"
#include "NavigationSystem.h"
#include "NavLinkCustomInterface.h"
#include "AI/NavigationSystemBase.h"
#include "AI/Navigation/NavigationElement.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "UObject/ObjectKey.h"

TSharedPtr<const FNavigationElement> UNavigationObjectRepository::AddNavigationElement(FNavigationElement&& Element, const ENotifyOnSuccess NotifyOnSuccess /*= ENotifyOnSuccess::Yes*/)
{
#if DO_ENSURE // We don't want to execute the Find at all for targets where ensures are disabled
	{
		UE_MT_SCOPED_READ_ACCESS(NavElementAccessDetector);

		if (!ensureMsgf(NavRelevantElements.Find(Element.GetHandle()) == nullptr, TEXT("Same element can't be registered twice.")))
		{
			return nullptr;
		}
	}
#endif

	const TSharedRef SharedElement(MakeShared<FNavigationElement>(MoveTemp(Element)));
	{
		UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);
		NavRelevantElements.Emplace(Element.GetHandle(), SharedElement);
	}

	if (NotifyOnSuccess == ENotifyOnSuccess::Yes)
	{
		(void)OnNavigationElementAddedDelegate.ExecuteIfBound(SharedElement);
	}

	return SharedElement.ToSharedPtr();
}

void UNavigationObjectRepository::RemoveNavigationElement(const FNavigationElementHandle Handle)
{
	UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);

	TSharedPtr<const FNavigationElement> Element;
	if (ensureMsgf(NavRelevantElements.RemoveAndCopyValue(Handle, Element),
		TEXT("Navigation element can't be removed since it was not registered or already unregistered)")))
	{
		(void)OnNavigationElementRemovedDelegate.ExecuteIfBound(Element.ToSharedRef());
	}
}

void UNavigationObjectRepository::ForEachNavigationElement(TFunctionRef<void(const TSharedRef<const FNavigationElement>&)> PerElementCallback) const
{
	UE_MT_SCOPED_READ_ACCESS(NavElementAccessDetector);

	for (auto It = NavRelevantElements.CreateConstIterator(); It; ++It)
	{
		if (const TSharedPtr<const FNavigationElement>& Element = It.Value())
		{
			PerElementCallback(Element.ToSharedRef());
		}
	}
}

TSharedPtr<const FNavigationElement> UNavigationObjectRepository::RegisterNavRelevantObject(const INavRelevantInterface& NavRelevantObject)
{
	return RegisterNavRelevantObjectInternal(NavRelevantObject, *Cast<UObject>(&NavRelevantObject), ENotifyOnSuccess::Yes);
}

bool UNavigationObjectRepository::ShouldCreateSubsystem(UObject* Outer) const
{
	return (Super::ShouldCreateSubsystem(Outer))
		&& GetDefault<UNavigationSystemV1>()->ShouldCreateNavigationSystemInstance(Cast<UWorld>(Outer));
}

TSharedPtr<const FNavigationElement> UNavigationObjectRepository::RegisterNavRelevantObjectInternal(
	const INavRelevantInterface& NavRelevantInterface,
	const UObject& NavRelevantObject,
	const ENotifyOnSuccess NotifyOnSuccess)
{
	// In AActor/UActorComponent code paths it is possible that a component registration is performed more than once
	// (i.e., Actor registering its component, then individual component registers too)
	// In such case we update with the latest.
	if (const TSharedPtr<const FNavigationElement> ElementPtr = GetNavigationElementForUObject(&NavRelevantObject))
	{
		UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);
		const TSharedRef<const FNavigationElement> NewElement = FNavigationElement::CreateFromNavRelevantInterface(NavRelevantInterface);
		NavRelevantElements[ElementPtr->GetHandle()] = NewElement;

		if (NotifyOnSuccess == ENotifyOnSuccess::Yes)
		{
			(void)OnNavigationElementAddedDelegate.ExecuteIfBound(NewElement);
		}

		UE_LOG(LogNavigation, Verbose, TEXT("%hs [already registered - updating] (%s:%s) Bounds: [%s]->[%s]"), __FUNCTION__,
			*GetNameSafe(NavRelevantObject.GetOuter()), *GetNameSafe(&NavRelevantObject),
			*ElementPtr->GetBounds().ToString(), *NewElement->GetBounds().ToString());

		return NewElement.ToSharedPtr();
	}

	if (NavRelevantInterface.IsNavigationRelevant())
	{
		if (const TSharedPtr<const FNavigationElement> SharedElement = AddNavigationElement(FNavigationElement(NavRelevantInterface), NotifyOnSuccess))
		{
			UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);
			ObjectsToHandleMap.Emplace(FObjectKey(&NavRelevantObject), SharedElement->GetHandle());

			UE_LOG(LogNavigation, Verbose, TEXT("%hs [registered] (%s:%s) Bounds: [%s]"), __FUNCTION__,
				*GetNameSafe(NavRelevantObject.GetOuter()), *GetNameSafe(&NavRelevantObject),
				*NavRelevantInterface.GetNavigationBounds().ToString());

			return SharedElement;
		}

		return nullptr;
	}

	UE_LOG(LogNavigation, VeryVerbose, TEXT("%hs [skipped: not relevant] (%s:%s)"), __FUNCTION__,
		*GetNameSafe(NavRelevantObject.GetOuter()), *GetNameSafe(&NavRelevantObject));
	return nullptr;
}

void UNavigationObjectRepository::UnregisterNavRelevantObject(const INavRelevantInterface& NavRelevantObject)
{
	UnregisterNavRelevantObject(Cast<UObject>(&NavRelevantObject));
}

void UNavigationObjectRepository::UnregisterNavRelevantObject(const UObject* NavRelevantObject)
{
	UE_LOG(LogNavigation, Verbose, TEXT("%hs (%s:%s)"), __FUNCTION__,
		NavRelevantObject ? *GetNameSafe(NavRelevantObject->GetOuter()) : TEXT("null outer"),
		*GetNameSafe(NavRelevantObject));

	FNavigationElementHandle Handle;
	{
		UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);
		ObjectsToHandleMap.RemoveAndCopyValue(FObjectKey(NavRelevantObject), Handle);
	}

	if (Handle)
	{
		RemoveNavigationElement(Handle);
	}
}

TSharedPtr<const FNavigationElement> UNavigationObjectRepository::GetNavigationElementForHandle(const FNavigationElementHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(NavElementAccessDetector);

	if (const TSharedPtr<const FNavigationElement>* Element = NavRelevantElements.Find(Handle))
	{
		return *Element;
	}

	return nullptr;
}

FNavigationElementHandle UNavigationObjectRepository::GetNavigationElementHandleForUObject(const UObject* NavRelevantObject)
{
	UE_MT_SCOPED_READ_ACCESS(NavElementAccessDetector);
	if (const FNavigationElementHandle* Handle = ObjectsToHandleMap.Find(FObjectKey(Cast<UObject>(NavRelevantObject))))
	{
		return *Handle;
	}

	return FNavigationElementHandle::Invalid;
}

TSharedPtr<const FNavigationElement> UNavigationObjectRepository::GetNavigationElementForUObject(const UObject* NavRelevantObject)
{
	UE_MT_SCOPED_READ_ACCESS(NavElementAccessDetector);

	if (const FNavigationElementHandle* Handle = ObjectsToHandleMap.Find(FObjectKey(NavRelevantObject)))
	{
		if (const TSharedPtr<const FNavigationElement>* Element = NavRelevantElements.Find(*Handle))
		{
			return *Element;
		}
	}

	return nullptr;
}

TSharedPtr<const FNavigationElement> UNavigationObjectRepository::UpdateNavigationElementForUObject(
	const INavRelevantInterface& NavRelevantInterface,
	const UObject& NavRelevantObject)
{
	// This method is called by the navigation system to make sure an up-to-date navigation element exists for a
	// given navigation relevant UObject.
	// In this case we only need to create, or update, the navigation element without sending
	// notification (i.e. ENotifyOnSuccess::No) since the caller (NavigationSystem) is already in the process of updating.
	return RegisterNavRelevantObjectInternal(NavRelevantInterface, NavRelevantObject, ENotifyOnSuccess::No);
}

void UNavigationObjectRepository::RegisterCustomNavLinkObject(INavLinkCustomInterface& CustomNavLinkObject)
{
	{
		UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);

#if DO_ENSURE // We don't want to execute the Find at all for targets where ensures are disabled
		if (!ensureMsgf(CustomLinkObjects.Find(&CustomNavLinkObject) == INDEX_NONE, TEXT("Same interface pointer can't be registered twice.")))
		{
			return;
		}
#endif

		CustomLinkObjects.Emplace(&CustomNavLinkObject);
	}

	OnCustomNavLinkObjectRegistered.ExecuteIfBound(CustomNavLinkObject);
}

void UNavigationObjectRepository::UnregisterCustomNavLinkObject(INavLinkCustomInterface& CustomNavLinkObject)
{
	{
		UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);
		ensureMsgf(CustomLinkObjects.Remove(&CustomNavLinkObject) > 0, TEXT("Interface can't be removed since it was not registered or already unregistered)"));
	}

	OnCustomNavLinkObjectUnregistered.ExecuteIfBound(CustomNavLinkObject);
}