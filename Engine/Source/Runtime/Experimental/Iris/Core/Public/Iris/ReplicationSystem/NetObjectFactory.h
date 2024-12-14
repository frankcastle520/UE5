// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"

#include "Iris/ReplicationSystem/NetObjectFactoryRegistry.h"
#include "Iris/ReplicationSystem/ReplicationBridgeTypes.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"

#include "NetObjectFactory.generated.h"

class UObjectReplicationBridge;
class UNetObjectFactory;

namespace UE::Net
{
	class FNetSerializationContext;

	struct FNetObjectResolveContext;

	typedef uint32 FReplicationProtocolIdentifier;
}

namespace UE::Net
{

/** Contextual information passed to an header so it can serialize/deserialize itself  */
struct FCreationHeaderContext
{
	/** The handle of the replicated object represented by the header */
	FNetRefHandle Handle;
	/** The bridge responsible for the replicated object */
	UObjectReplicationBridge* Bridge;
	/** The factory that allocated the header */
	UNetObjectFactory* Factory;
	/** Access to the bitstream reader or writer */
	FNetSerializationContext& Serialization;

	FCreationHeaderContext(FNetRefHandle InHandle, UObjectReplicationBridge* InBridge, UNetObjectFactory* InFactory, FNetSerializationContext& InSerialization) : Handle(InHandle), Bridge(InBridge), Factory(InFactory), Serialization(InSerialization) {}
};

/*
 * Class holding the raw information allowing any client from retrieving or allocating a replicated UObject instance.
 * Can also implement the serialization of it's data into a bitstream
 */ 
class FNetObjectCreationHeader
{
public:

	virtual ~FNetObjectCreationHeader() {};

	void SetProtocolId(uint32 InId) { ProtocolIdentifier = InId; }
	void SetFactoryId(FNetObjectFactoryId InId) { FactoryId = InId; }

	FReplicationProtocolIdentifier GetProtocolId() const { return ProtocolIdentifier; }
	FNetObjectFactoryId GetNetFactoryId() const { return FactoryId; }

	/** Transform the header information into a readable format */
	virtual FString ToString() const { return TEXT("NotImplemented"); }

private:

	FReplicationProtocolIdentifier ProtocolIdentifier = 0;
	FNetObjectFactoryId  FactoryId = InvalidNetObjectFactoryId;
};

} // end namespace UE::Net

/**
 * The class is responsible for creating the header representing specific replicated object types.
 * Also responsible for instantiating the UObject from a replicated header.
 */
UCLASS(MinimalAPI, transient, abstract)
class UNetObjectFactory : public UObject
{
	GENERATED_BODY()

public:

	void Init(UE::Net::FNetObjectFactoryId InId, UObjectReplicationBridge* InBridge);
	void Deinit();

	struct FInstantiateResult;
	struct FInstantiateContext;
	struct FPostInstantiationContext;
	struct FPostInitContext;

	/**
	 * Creates the header containing all information required to instantiate a remote version of the object represented by the handle.
	 * @param Handle The handle of the object represented by the header
	 * @param ProtocolId The protocol id to add to the header
	 * @return Return a valid header filled with the information that will be sent to remote connections
	 */
	TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateHeader(UE::Net::FNetRefHandle Handle, UE::Net::FReplicationProtocolIdentifier ProtocolId);

	/** 
	 * Serializes a valid header so it can be replicated to remote connections 
	 * @param Handle The handle of the object represented by the header
	 * @param Header The filled header that will be serialized
	 * @return Returns true if the serialization was a success
	 */
	bool WriteHeader(UE::Net::FNetRefHandle Handle, UE::Net::FNetSerializationContext& Serialization, const UE::Net::FNetObjectCreationHeader* Header);

	/** 
	 * Deserialize the header data received and return a valid header
	 * @param Handle The handle of the object represented by the header
	 * @param Serialization Gives access to the bitstream reader
	 * @return Return a valid header filled with the information that represents the remote object
	 */
	TUniquePtr<UE::Net::FNetObjectCreationHeader> ReadHeader(UE::Net::FNetRefHandle Handle, UE::Net::FNetSerializationContext& Serialization);

	/**
	* Create or bind a replicated object from the received creation header.
	* @param Context Gives you access to useful info on the object to instantiate
	* @param Header The filled header information to use to spawn the object
	* @return The instantiated object if successful and relevant flags for the bridge to act on
	*/
	virtual FInstantiateResult InstantiateReplicatedObjectFromHeader(const FInstantiateContext& Context, const UE::Net::FNetObjectCreationHeader* Header) PURE_VIRTUAL(UNetObjectFactory::InstantiateReplicatedObjectFromHeader, return FInstantiateResult(););

	/**
	 * Optional callback triggered at the end of the instantiation process and before any replicated properties were applied.
	 * Useful to apply any additional data included with the header or to signal the new object to other systems.
	 * @param Context Gives you access to the new object and the header it was created from.
	 */
	virtual void PostInstantiation(const FPostInstantiationContext& Context) {}

	/**
	 * Optional callback triggered after we applied the initial replicated properties to the instantiated object.
	 * From here the remote object is ready to be used by the game engine.
	 * @param Context Gives you access to the new object.
	 */
	virtual void PostInit(const FPostInitContext& Context) {}

public:

	/** Result of the instantiate request */
	struct FInstantiateResult
	{
		/** The instantiated object represented by the header */
		UObject* Instance = nullptr;
		/** Flags to pass back to the bridge */
		EReplicationBridgeCreateNetRefHandleResultFlags Flags = EReplicationBridgeCreateNetRefHandleResultFlags::None;
	};

	/** Contextual information to use during instantiation */
	struct FInstantiateContext
	{
		/** The handle tied to the replicated object to instantiate */
		UE::Net::FNetRefHandle Handle;

		const UE::Net::FNetObjectResolveContext& ResolveContext;

		/** The handle of the object's root object (only if instantiating a subobject) */
		UE::Net::FNetRefHandle RootObjectOfSubObject;

		FInstantiateContext(UE::Net::FNetRefHandle InHandle, const UE::Net::FNetObjectResolveContext& InResolveContext, UE::Net::FNetRefHandle InRootObjectHandle) : Handle(InHandle), ResolveContext(InResolveContext), RootObjectOfSubObject(InRootObjectHandle) {}

		/** Tells if we instantiating a root object or a subobject. */
		bool IsRootObject() const { return !RootObjectOfSubObject.IsValid(); }
		bool IsSubObject() const { return !IsRootObject(); }
	};

	/** Contextual information to use in the PostInstantiation callback */
	struct FPostInstantiationContext
	{
		/** The object instantiated */
		UObject* Instance = nullptr;
		/** The header representing the replicated object */
		const UE::Net::FNetObjectCreationHeader* Header = nullptr;
		/** The connection that owns the replicated object */
		uint32 ConnectionId = 0;
	};

	/** Contextual information to use in the PostInit callback */
	struct FPostInitContext
	{
		/** The object instantiated */
		UObject* Instance = nullptr;
		/** The handle of the object */
		UE::Net::FNetRefHandle Handle;
	};

protected:

	virtual void OnInit() {}
	virtual void OnDeinit() {}

	/**
	 * Create the correct header type for a given replicated object and fill the header with the information representing it.
	 * @param Handle The net handle of the replicated object.
	 * @return A valid creation header if successful. 
	 */
	virtual TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateAndFillHeader(UE::Net::FNetRefHandle Handle) PURE_VIRTUAL(UNetObjectFactory::CreateAndFillHeader, return nullptr;);

	/**
	* Serialize the header into the bitstream
	* @param Context Gives access to the bitstream writer and other useful information.
	* @param Header A valid and filled header to serialize
	* @return Return true if the header serialization was successful.
	*/
	virtual bool SerializeHeader(const UE::Net::FCreationHeaderContext& Context, const UE::Net::FNetObjectCreationHeader* Header) PURE_VIRTUAL(UNetObjectFactory::SerializeHeader, return false;);

	/**
	* Create a new header and deserialize it's data from the incoming bitstream
	* @param Serialization Gives you access to the bit reader
	* @return Return a valid and filled header if successful.
	*/
	virtual TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateAndDeserializeHeader(const UE::Net::FCreationHeaderContext& Context) PURE_VIRTUAL(UNetObjectFactory::CreateAndDeserializeHeader, return nullptr;);

protected:

	UObjectReplicationBridge* Bridge = nullptr;
	UE::Net::FNetObjectFactoryId FactoryId = UE::Net::InvalidNetObjectFactoryId;

};