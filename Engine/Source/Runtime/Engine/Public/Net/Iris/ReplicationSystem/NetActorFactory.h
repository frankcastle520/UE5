// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/ReplicationSystem/NetObjectFactory.h"

#if UE_WITH_IRIS
#include "Iris/Core/NetObjectReference.h"
#endif

#include "NetActorFactory.generated.h"

namespace UE::Net::Private
{
	enum class EActorNetSpawnInfoFlags : uint32;
}


namespace UE::Net
{

#if UE_WITH_IRIS

/**
 * Header information to be able to tell if its a dynamic or static header
 */
class FBaseActorNetCreationHeader : public FNetObjectCreationHeader
{
public:
	virtual bool IsDynamic() const = 0;

	TArray<uint8> CustomCreationData;
	uint16 CustomCreationDataBitCount = 0;
};

/**
 * Header information representing static actors
 */
class FStaticActorNetCreationHeader : public FBaseActorNetCreationHeader
{
public:

	virtual bool IsDynamic() const
	{
		return false;
	}

	FNetObjectReference ObjectReference;
	
	bool Serialize(const FCreationHeaderContext& Context) const;
	bool Deserialize(const FCreationHeaderContext& Context);

	virtual FString ToString() const override;
};


/**
 * Header information representing dynamic actors
 */
class FDynamicActorNetCreationHeader : public FBaseActorNetCreationHeader
{
public:

	virtual bool IsDynamic() const override
	{ 
		return true;
	}

	virtual FString ToString() const override;

	struct FActorNetSpawnInfo
	{
		FActorNetSpawnInfo()
		: Location(EForceInit::ForceInitToZero)
		, Rotation(EForceInit::ForceInitToZero)
		, Scale(FVector::OneVector)
		, Velocity(EForceInit::ForceInitToZero)
		{}

		FVector Location;
		FRotator Rotation;
		FVector Scale;
		FVector Velocity;
	};

	FActorNetSpawnInfo SpawnInfo;

	FNetObjectReference ArchetypeReference;
	FNetObjectReference LevelReference; // Only when bUsePersistentLevel is false
	
	bool bUsePersistentLevel = false;

	bool Serialize(const FCreationHeaderContext& Context, UE::Net::Private::EActorNetSpawnInfoFlags SpawnFlags, const FActorNetSpawnInfo& DefaultSpawnInfo) const;
	bool Deserialize(const FCreationHeaderContext& Context, const FActorNetSpawnInfo& DefaultSpawnInfo);
};

#endif // UE_WITH_IRIS

} // end namespace UE::Net

/**
 * Responsible for creating headers allowing remote factories to spawn replicated actors
 */
UCLASS()
class UNetActorFactory : public UNetObjectFactory
{
	GENERATED_BODY()

#if UE_WITH_IRIS

public:

	static FName GetFactoryName()
	{ 
		return TEXT("NetActorFactory"); 
	}

	virtual void OnInit() override;

	virtual FInstantiateResult InstantiateReplicatedObjectFromHeader(const FInstantiateContext& Context, const UE::Net::FNetObjectCreationHeader* Header) override;

	virtual void PostInstantiation(const FPostInstantiationContext& Context) override;

	virtual void PostInit(const FPostInitContext& Context) override;

protected:

	virtual TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateAndFillHeader(UE::Net::FNetRefHandle Handle) override;
	virtual TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateAndDeserializeHeader(const UE::Net::FCreationHeaderContext& Context) override;

	virtual bool SerializeHeader(const UE::Net::FCreationHeaderContext& Context, const UE::Net::FNetObjectCreationHeader* Header)  override;

private:

	UE::Net::Private::EActorNetSpawnInfoFlags SpawnInfoFlags;

	const UE::Net::FDynamicActorNetCreationHeader::FActorNetSpawnInfo DefaultSpawnInfo;

#endif // UE_WITH_IRIS
};



