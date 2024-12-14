// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/ReplicationSystem/NetObjectFactory.h"

#if UE_WITH_IRIS
#include "Iris/Core/NetObjectReference.h"
#endif

#include "NetSubObjectFactory.generated.h"

/**
 * Responsible for creating headers allowing remote factories to spawn replicated actors
 */
UCLASS()
class UNetSubObjectFactory : public UNetObjectFactory
{
	GENERATED_BODY()

#if UE_WITH_IRIS

public:

	static FName GetFactoryName()
	{ 
		return TEXT("NetSubObjectFactory");
	}

	virtual FInstantiateResult InstantiateReplicatedObjectFromHeader(const FInstantiateContext& Context, const UE::Net::FNetObjectCreationHeader* Header) override;

protected:

	virtual TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateAndFillHeader(UE::Net::FNetRefHandle Handle) override;
	virtual TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateAndDeserializeHeader(const UE::Net::FCreationHeaderContext& Context) override;

	virtual bool SerializeHeader(const UE::Net::FCreationHeaderContext& Context, const UE::Net::FNetObjectCreationHeader* Header)  override;
#endif // UE_WITH_IRIS
};


namespace UE::Net
{
#if UE_WITH_IRIS

/**
 * Header information to be able to tell if its a dynamic or static header
 */
class FNetBaseSubObjectCreationHeader : public FNetObjectCreationHeader
{
public:
	
	virtual bool IsDynamic() const = 0;

	virtual bool Serialize(const FCreationHeaderContext& Context) const
	{ 
		return false;
	}
};


/**
 * Header information representing subobjects that can be found via their pathname (aka: static or stable name)
 */
class FNetStaticSubObjectCreationHeader : public FNetBaseSubObjectCreationHeader
{
public:

	virtual bool IsDynamic() const override
	{
		return false;
	}

	virtual bool Serialize(const FCreationHeaderContext& Context) const override;
	bool Deserialize(const FCreationHeaderContext& Context);

	virtual FString ToString() const override;

	FNetObjectReference ObjectReference; // Only for static objects
};

/**
 * Header information representing subobjects that must be instantiated
 */
class FNetDynamicSubObjectCreationHeader : public FNetBaseSubObjectCreationHeader
{
public:

	virtual bool Serialize(const FCreationHeaderContext& Context) const override;
	bool Deserialize(const FCreationHeaderContext& Context);

	virtual bool IsDynamic() const override
	{ 
		return true;
	}

	virtual FString ToString() const override;

	FNetObjectReference ObjectClassReference;
	FNetObjectReference OuterReference;
	uint8 bUsePersistentLevel : 1 = false;
	uint8 bOuterIsTransientLevel : 1 = false;	// When set the OuterReference was not sent because the Outer is the default transient level.
	uint8 bOuterIsRootObject : 1 = false;		// When set the OuterReference was not sent because the Outer is the known RootObject.

};
#endif // UE_WITH_IRIS
} // end namespace UE::Net