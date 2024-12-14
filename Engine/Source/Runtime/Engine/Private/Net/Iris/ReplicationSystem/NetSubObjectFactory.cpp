// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/Iris/ReplicationSystem/NetSubObjectFactory.h"

#if UE_WITH_IRIS

#include "GameFramework/Actor.h"

#include "HAL/UnrealMemory.h"

#include "Iris/Core/IrisLog.h"

#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"

#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/ObjectNetSerializer.h"

#include "UObject/Package.h"

#include "Net/DataBunch.h"

#endif // UE_WITH_IRIS

#if UE_WITH_IRIS

TUniquePtr<UE::Net::FNetObjectCreationHeader> UNetSubObjectFactory::CreateAndFillHeader(UE::Net::FNetRefHandle Handle)
{
	using namespace UE::Net;

	UObject* SubObject = Bridge->GetReplicatedObject(Handle);
	if (!SubObject)
	{
		ensureMsgf(SubObject, TEXT("UNetSubObjectFactory::CreateAndFillHeader could not find object tied to handle: %s"), *Bridge->PrintObjectFromNetRefHandle(Handle));
		return nullptr;
	}

	FNetObjectReference ObjectRef = Bridge->GetOrCreateObjectReference(SubObject);

	if (!ObjectRef.GetRefHandle().IsDynamic() || SubObject->IsNameStableForNetworking())
	{
		// No more information needed since we don't need to spawn the object on the remote.
		TUniquePtr<FNetStaticSubObjectCreationHeader> Header(new FNetStaticSubObjectCreationHeader);
		Header->ObjectReference = ObjectRef;
		return Header;
	}

	TUniquePtr<FNetDynamicSubObjectCreationHeader> Header(new FNetDynamicSubObjectCreationHeader);
	FNetDynamicSubObjectCreationHeader* SubObjectHeader = static_cast<FNetDynamicSubObjectCreationHeader*>(Header.Get());

	check(SubObject->NeedsLoadForClient() ); // We have no business sending this unless the client can load
	check(SubObject->GetClass()->NeedsLoadForClient());	// We have no business sending this unless the client can load

	SubObjectHeader->ObjectClassReference = Bridge->GetOrCreateObjectReference(SubObject->GetClass());

	// Find the right Outer
	UObject* OuterObject = SubObject->GetOuter();	
	if (OuterObject == GetTransientPackage())
	{
		SubObjectHeader->bOuterIsTransientLevel = true;
	}
	else 
	{
		FNetRefHandle RootObjectHandle = Bridge->GetRootObjectOfSubObject(Handle);
		UObject* RootObject = Bridge->GetReplicatedObject(RootObjectHandle);
		check(RootObject);

		if (OuterObject == RootObject)
		{
			SubObjectHeader->bOuterIsRootObject = true;
		}
		else
		{
			SubObjectHeader->OuterReference = Bridge->GetOrCreateObjectReference(OuterObject);

			if (!SubObjectHeader->OuterReference.IsValid())
			{
				UE_LOG(LogIris, Error, TEXT("UNetSubObjectFactory::CreateAndFillHeader %s could not create NetReference to Outer %s"), *Bridge->PrintObjectFromNetRefHandle(Handle), *GetNameSafe(OuterObject));
			}
		}
	}

	return Header;
}

TUniquePtr<UE::Net::FNetObjectCreationHeader> UNetSubObjectFactory::CreateAndDeserializeHeader(const UE::Net::FCreationHeaderContext& Context)
{
	using namespace UE::Net;

	FNetBitStreamReader* Reader = Context.Serialization.GetBitStreamReader();

	if (Reader->ReadBool())
	{
		TUniquePtr<FNetDynamicSubObjectCreationHeader> Header(new FNetDynamicSubObjectCreationHeader);
		Header->Deserialize(Context);
		return Header;
	}
	else
	{
		TUniquePtr<FNetStaticSubObjectCreationHeader> Header(new FNetStaticSubObjectCreationHeader);
		Header->Deserialize(Context);
		return Header;
	}
}


UNetSubObjectFactory::FInstantiateResult UNetSubObjectFactory::InstantiateReplicatedObjectFromHeader(const FInstantiateContext& Context, const UE::Net::FNetObjectCreationHeader* Header)
{
	using namespace UE::Net;

	const FNetBaseSubObjectCreationHeader* BaseHeader = static_cast<const FNetBaseSubObjectCreationHeader*>(Header);

	if (!BaseHeader->IsDynamic())
	{
		const FNetStaticSubObjectCreationHeader* SubObjectHeader = static_cast<const FNetStaticSubObjectCreationHeader*>(BaseHeader);

		// Resolve by finding object relative to owner. We do not allow this object to be destroyed.
		UObject* SubObject = Bridge->ResolveObjectReference(SubObjectHeader->ObjectReference, Context.ResolveContext);

		if (!SubObject)
		{
			UE_LOG(LogIris, Error, TEXT("UNetSubObjectFactory::InstantiateNetObjectFromHeader %s: Failed to find static or stable name object referenced by SubObject: %s, Owner: %s, RootObject: %s"), *Context.Handle.ToString(), *Bridge->DescribeObjectReference(SubObjectHeader->ObjectReference, Context.ResolveContext), *Bridge->PrintObjectFromNetRefHandle(Context.RootObjectOfSubObject), *GetPathNameSafe(Bridge->GetReplicatedObject(Context.RootObjectOfSubObject)));
			
			return FInstantiateResult();
		}

		UE_LOG(LogIris, Verbose, TEXT("UNetSubObjectFactory::InstantiateNetObjectFromHeader %s: Found static or stable name SubObject using path %s"), *Context.Handle.ToString(), ToCStr(SubObject->GetPathName()));

		FInstantiateResult Result { .Instance = SubObject };
		return Result;
	}

	// For dynamic objects we have to spawn them

	const FNetDynamicSubObjectCreationHeader* SubObjectHeader = static_cast<const FNetDynamicSubObjectCreationHeader*>(Header);
		
	UObject* RootObject = Bridge->GetReplicatedObject(Context.RootObjectOfSubObject);
	AActor* RootActor = CastChecked<AActor>(RootObject);
			
	// Find the proper Outer
	UObject* OuterObject = nullptr;
	if (SubObjectHeader->bOuterIsTransientLevel)
	{
		OuterObject = GetTransientPackage();
	}
	else if (SubObjectHeader->bOuterIsRootObject)
	{
		OuterObject = RootActor;
	}				
	else
	{
		OuterObject = Bridge->ResolveObjectReference(SubObjectHeader->OuterReference, Context.ResolveContext);

		if (!OuterObject)
		{
			UE_LOG(LogIris, Error, TEXT("BeginInstantiateFromRemote Failed to find Outer %s for dynamic subobject %s"), *Bridge->DescribeObjectReference(SubObjectHeader->OuterReference, Context.ResolveContext), *Context.Handle.ToString())

			// Fallback to the rootobject instead
			OuterObject = RootActor;
		}
	}

	// We need to spawn the subobject
	UObject* SubObjClassObj = Bridge->ResolveObjectReference(SubObjectHeader->ObjectClassReference, Context.ResolveContext);
	UClass * SubObjClass = Cast<UClass>(SubObjClassObj);

	// Try to spawn SubObject
	UObject* SubObj = NewObject<UObject>(OuterObject, SubObjClass);

	// Sanity check some things
	checkf(SubObj != nullptr, TEXT("UNetSubObjectFactory::InstantiateNetObjectFromHeader: Subobject is NULL after instantiating. Class: %s, Outer %s, Actor %s"), *GetNameSafe(SubObjClass), *GetNameSafe(OuterObject), *GetNameSafe(RootActor));
	checkf(!OuterObject || SubObj->IsIn(OuterObject), TEXT("UNetSubObjectFactory::InstantiateNetObjectFromHeader: Subobject is not in Outer. SubObject: %s, Outer %s, Actor %s"), *SubObj->GetName(), *GetNameSafe(OuterObject), *GetNameSafe(RootActor));
	checkf(Cast<AActor>(SubObj) == nullptr, TEXT("UNetSubObjectFactory::InstantiateNetObjectFromHeader: Subobject is an Actor. SubObject: %s, Outer %s, Actor %s"), *SubObj->GetName(), *GetNameSafe(OuterObject), *GetNameSafe(RootActor));

	FInstantiateResult Result { .Instance = SubObj };

	// We must defer call OnSubObjectCreatedFromReplication after the state has been applied to the owning actor in order to behave like old system.
	Result.Flags |= EReplicationBridgeCreateNetRefHandleResultFlags::ShouldCallSubObjectCreatedFromReplication;

	// Created objects may be destroyed.
	Result.Flags |= EReplicationBridgeCreateNetRefHandleResultFlags::AllowDestroyInstanceFromRemote;
			
	return Result;
}

bool UNetSubObjectFactory::SerializeHeader(const UE::Net::FCreationHeaderContext& Context, const UE::Net::FNetObjectCreationHeader* Header)
{
	using namespace UE::Net;
	const FNetBaseSubObjectCreationHeader* SubObjectHeader = static_cast<const FNetBaseSubObjectCreationHeader*>(Header);
	
	FNetBitStreamWriter* Writer = Context.Serialization.GetBitStreamWriter();
	
	Writer->WriteBool(SubObjectHeader->IsDynamic());

	return SubObjectHeader->Serialize(Context);
}

//------------------------------------------------------------------------

namespace UE::Net
{

//------------------------------------------------------------------------
// FNetStaticSubObjectCreationHeader
//------------------------------------------------------------------------
FString FNetStaticSubObjectCreationHeader::ToString() const
{
	return FString::Printf(TEXT("FNetStaticSubObjectCreationHeader (ProtocolId:0x%x):\n\t"
								"ObjectReference=%s\n\t"),								
								GetProtocolId(),
								*ObjectReference.ToString());

}

bool FNetStaticSubObjectCreationHeader::Serialize(const FCreationHeaderContext& Context) const
{
	WriteFullNetObjectReference(Context.Serialization, ObjectReference);
	return true;
}

bool FNetStaticSubObjectCreationHeader::Deserialize(const FCreationHeaderContext& Context)
{
	ReadFullNetObjectReference(Context.Serialization, ObjectReference);
	return true;
}

//------------------------------------------------------------------------
// FNetDynamicSubObjectCreationHeader
//------------------------------------------------------------------------

FString FNetDynamicSubObjectCreationHeader::ToString() const
{
	return FString::Printf(TEXT("FNetDynamicSubObjectCreationHeader (ProtocolId:0x%x):\n\t"
								"ObjectClassReference=%s\n\t"
								"OuterReference=%s\n\t"
								"bUsePersistenLevel=%u\n\t"
								"bOuterIsTransientLevel=%u\n\t"
								"bOuterIsRootObject=%u\n\t"),
								GetProtocolId(),
								*ObjectClassReference.ToString(),
								*OuterReference.ToString(),
								bUsePersistentLevel,
								bOuterIsTransientLevel,
								bOuterIsRootObject);

}

bool FNetDynamicSubObjectCreationHeader::Serialize(const FCreationHeaderContext& Context) const
{
	FNetBitStreamWriter* Writer = Context.Serialization.GetBitStreamWriter();

	WriteFullNetObjectReference(Context.Serialization, ObjectClassReference);

	if (!Writer->WriteBool(bOuterIsTransientLevel))
	{
		if (!Writer->WriteBool(bOuterIsRootObject))
		{
			WriteFullNetObjectReference(Context.Serialization, OuterReference);
		}
	}

	return true;
}

bool FNetDynamicSubObjectCreationHeader::Deserialize(const FCreationHeaderContext& Context)
{
	FNetBitStreamReader* Reader = Context.Serialization.GetBitStreamReader();

	ReadFullNetObjectReference(Context.Serialization, ObjectClassReference);

	bOuterIsTransientLevel = Reader->ReadBool();
	if (!bOuterIsTransientLevel)
	{
		bOuterIsRootObject = Reader->ReadBool();
		if (!bOuterIsRootObject)
		{
			ReadFullNetObjectReference(Context.Serialization, OuterReference);
		}
	}

	return true;
}

} // end namespace UE::Net

#endif //UE_WITH_IRIS