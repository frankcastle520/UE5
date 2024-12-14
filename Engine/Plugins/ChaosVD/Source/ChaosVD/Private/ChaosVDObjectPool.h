// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDModule.h"
#include "Containers/Array.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Interfaces/ChaosVDPooledObject.h"
#include "UObject/Object.h"
#include "UObject/Package.h"

struct FChaosVDObjectPoolCVars
{
	static bool bUseObjectPool;
	static FAutoConsoleVariableRef CVarUseObjectPool;
};

/** Basic Pool system for UObjects */
template<typename ObjectType>
class TChaosVDObjectPool : public FGCObject
{
public:
	TChaosVDObjectPool() = default;
	virtual ~TChaosVDObjectPool() override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	void SetPoolName(const FString& InName) { PoolName = InName; };
	
	virtual FString GetReferencerName() const override
	{
		return PoolName;
	}

	ObjectType* AcquireObject(UObject* Outer, FName Name);

	void DisposeObject(UObject* Object);

	TFunction<ObjectType*(UObject*,FName)> ObjectFactoryOverride;

protected:
	FString PoolName = TEXT("ChaosVDObjectPool");
	int32 PoolHits = 0;
	int32 PoolRequests = 0;
	TArray<TObjectPtr<ObjectType>> PooledObjects;
};

template <typename ObjectType>
TChaosVDObjectPool<ObjectType>::~TChaosVDObjectPool()
{
	float HitMissRatio = PoolRequests > 0 ? ((static_cast<float>(PoolHits) / static_cast<float>(PoolRequests)) * 100.0f) : 0;
	UE_LOG(LogChaosVDEditor, Log, TEXT("Object pooling Statics for pool [%s] | Hits [%d] | Total Acquire requests [%d] | [%f] percent hit/miss ratio"), *PoolName, PoolHits,  PoolRequests, HitMissRatio);
}

template <typename ObjectType>
void TChaosVDObjectPool<ObjectType>::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(PooledObjects);
}

template <typename ObjectType>
ObjectType* TChaosVDObjectPool<ObjectType>::AcquireObject(UObject* Outer, FName Name)
{
	PoolRequests++;

	// If pooling is disabled, just fall through the code that will create the object
	if (FChaosVDObjectPoolCVars::bUseObjectPool && PooledObjects.Num() > 0)
	{
		if (ObjectType* Object = PooledObjects.Pop())
		{
			const FName NewName = MakeUniqueObjectName(Outer, ObjectType::StaticClass(), Name);
			Object->Rename(*NewName.ToString() , Outer, REN_NonTransactional | REN_DoNotDirty | REN_SkipGeneratedClasses | REN_DontCreateRedirectors);
		
			if (IChaosVDPooledObject* AsPooledObject = Cast<IChaosVDPooledObject>(Object))
			{
				AsPooledObject->OnAcquired();
			}

			PoolHits++;
			
			return Object;
		}
	}

	const FName NewName = MakeUniqueObjectName(Outer, ObjectType::StaticClass(), Name);

	ObjectType* CreatedObject = nullptr;
	if (ObjectFactoryOverride)
	{
		CreatedObject = ObjectFactoryOverride(Outer, NewName);
	}
	else
	{
		CreatedObject = NewObject<ObjectType>(Outer, NewName);
	}

	if (IChaosVDPooledObject* AsPooledObject = Cast<IChaosVDPooledObject>(CreatedObject))
	{
		AsPooledObject->OnAcquired();
	}
	
	return CreatedObject;
}

template <typename ObjectType>
void TChaosVDObjectPool<ObjectType>::DisposeObject(UObject* Object)
{
	// If pooling is disabled, just destroy the object
	//TODO: Should we provide a way to override how these are destroyed?
	if (!FChaosVDObjectPoolCVars::bUseObjectPool)
	{
		if (UActorComponent* AsActorComponent = Cast<UActorComponent>(Object))
		{
			AsActorComponent->DestroyComponent();
		}
		else if (AActor* AsActor = Cast<AActor>(Object))
		{
			AsActor->Destroy();
		}
		else
		{
			Object->ConditionalBeginDestroy();
		}
		
		return;
	}

	if (IChaosVDPooledObject* AsPooledObject = Cast<IChaosVDPooledObject>(Object))
	{
		AsPooledObject->OnDisposed();
	}

	UPackage* TransientPackage = GetTransientPackage();
	const FName NewName = MakeUniqueObjectName(TransientPackage, ObjectType::StaticClass());
	Object->Rename(*NewName.ToString(), TransientPackage, REN_NonTransactional | REN_DoNotDirty | REN_SkipGeneratedClasses | REN_DontCreateRedirectors);

	PooledObjects.Emplace(Cast<ObjectType>(Object));
}
