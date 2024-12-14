// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/Iris/ReplicationSystem/NetActorFactory.h"

#if UE_WITH_IRIS

#include "Engine/Level.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"

#include "GameFramework/Actor.h"

#include "HAL/UnrealMemory.h"
#include "HAL/LowLevelMemStats.h"

#include "Iris/Core/IrisLog.h"
#include "Iris/Core/IrisProfiler.h"

#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/ObjectNetSerializer.h"
#include "Iris/Serialization/PackedVectorNetSerializers.h"
#include "Iris/Serialization/VectorNetSerializers.h"

#include "Net/DataBunch.h"
#include "Net/Core/Connection/ConnectionHandle.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"

#include "ProfilingDebugging/AssetMetadataTrace.h"

#include "UObject/Package.h"

#endif // UE_WITH_IRIS

namespace UE::Net::Private
{
#if UE_WITH_IRIS
	static int32 SerializeNewActorMaxBunchSize = 1024;
	static FAutoConsoleVariableRef CVarSerializeNewActorMaxBunchSize(
	TEXT("net.Iris.SerializeNewActorMaxBunchSize"),
	SerializeNewActorMaxBunchSize,
	TEXT("Max allowed bits that can be added to the creation header via OnSerializeNewActor."));

	enum class EActorNetSpawnInfoFlags : uint32
	{
		None = 0U,
		QuantizeScale = 1U,
		QuantizeLocation = QuantizeScale << 1U,
		QuantizeVelocity = QuantizeLocation << 1U,
		BitCount = 3U,
	};
	ENUM_CLASS_FLAGS(EActorNetSpawnInfoFlags)
	
	EActorNetSpawnInfoFlags GetSpawnInfoFlags()
	{
		// Init spawninfo flags from CVARs
		EActorNetSpawnInfoFlags Flags = EActorNetSpawnInfoFlags::None;
		{
			bool bQuantizeActorScaleOnSpawn = false;
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.QuantizeActorScaleOnSpawn"));
			if (ensure(CVar))
			{
				bQuantizeActorScaleOnSpawn = CVar->GetBool();
			}
			Flags |= bQuantizeActorScaleOnSpawn ? EActorNetSpawnInfoFlags::QuantizeScale : EActorNetSpawnInfoFlags::None;
		}

		{
			bool bQuantizeActorLocationOnSpawn = true;
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.QuantizeActorLocationOnSpawn"));
			if (ensure(CVar))
			{
				bQuantizeActorLocationOnSpawn = CVar->GetBool();
			}
			Flags |= bQuantizeActorLocationOnSpawn ? EActorNetSpawnInfoFlags::QuantizeLocation : EActorNetSpawnInfoFlags::None;
		}

		{
			bool bQuantizeActorVelocityOnSpawn = true;
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.QuantizeActorVelocityOnSpawn"));
			if (ensure(CVar))
			{
				bQuantizeActorVelocityOnSpawn = CVar->GetBool();
			}
			Flags |= bQuantizeActorVelocityOnSpawn ? EActorNetSpawnInfoFlags::QuantizeVelocity : EActorNetSpawnInfoFlags::None;
		}

		return Flags;
	}

	// Write vector using default value compression using specified serializer
	static void WriteConditionallyQuantizedVector(FNetBitStreamWriter* Writer, const FVector& Vector, const FVector& DefaultValue, bool bQuantize)
	{
		// We use 0.01f for comparing when using quantization, because we will only send a single point of precision anyway.
		// We could probably get away with 0.1f, but that may introduce edge cases for rounding.
		static constexpr float Epsilon_Quantized = 0.01f;
				
		// We use KINDA_SMALL_NUMBER for comparing when not using quantization, because that's the default for FVector::Equals.
		const float Epsilon = bQuantize ? Epsilon_Quantized : UE_KINDA_SMALL_NUMBER;
	
		if (Writer->WriteBool(!Vector.Equals(DefaultValue, Epsilon)))
		{
			Writer->WriteBool(bQuantize);

			const FNetSerializer& Serializer = bQuantize ? UE_NET_GET_SERIALIZER(FVectorNetQuantize10NetSerializer) : UE_NET_GET_SERIALIZER(FVectorNetSerializer);

			FNetSerializationContext Context(Writer);

			alignas(16) uint8 QuantizedState[32] = {};
			checkSlow(sizeof(QuantizedState) >= Serializer.QuantizedTypeSize);

			FNetQuantizeArgs QuantizeArgs;
			QuantizeArgs.Version = Serializer.Version;
			QuantizeArgs.Source = NetSerializerValuePointer(&Vector);
			QuantizeArgs.Target = NetSerializerValuePointer(&QuantizedState[0]);
			QuantizeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
			Serializer.Quantize(Context, QuantizeArgs);

			FNetSerializeArgs SerializeArgs;
			SerializeArgs.Version = Serializer.Version;
			SerializeArgs.Source = QuantizeArgs.Target;
			SerializeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
			Serializer.Serialize(Context, SerializeArgs);
		}
	}

	// Read default value compressed vector using specified serializer
	static void ReadConditionallyQuantizedVector(FNetBitStreamReader* Reader, FVector& OutVector, const FVector& DefaultValue)
	{
		if (Reader->ReadBool())
		{
			const bool bIsQuantized = Reader->ReadBool();

			const FNetSerializer& Serializer = bIsQuantized ? UE_NET_GET_SERIALIZER(FVectorNetQuantize10NetSerializer) : UE_NET_GET_SERIALIZER(FVectorNetSerializer);

			FNetSerializationContext Context(Reader);

			alignas(16) uint8 QuantizedState[32] = {};
			checkSlow(sizeof(QuantizedState) >= Serializer.QuantizedTypeSize);

			FNetDeserializeArgs DeserializeArgs;
			DeserializeArgs.Version = Serializer.Version;
			DeserializeArgs.Target = NetSerializerValuePointer(&QuantizedState[0]);
			DeserializeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
			Serializer.Deserialize(Context, DeserializeArgs);

			FNetDequantizeArgs DequantizeArgs;
			DequantizeArgs.Version = Serializer.Version;
			DequantizeArgs.Source = DeserializeArgs.Target;
			DequantizeArgs.Target = NetSerializerValuePointer(&OutVector);
			DequantizeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
			Serializer.Dequantize(Context, DequantizeArgs);
		}
		else
		{
			OutVector = DefaultValue;
		}
	}
#endif // UE_WITH_IRIS
} // end namespace UE::Net::Private

#if UE_WITH_IRIS

//------------------------------------------------------------------------
// UNetActorFactory
//------------------------------------------------------------------------

void UNetActorFactory::OnInit()
{
	Super::OnInit();
	SpawnInfoFlags = UE::Net::Private::GetSpawnInfoFlags();
}

TUniquePtr<UE::Net::FNetObjectCreationHeader> UNetActorFactory::CreateAndFillHeader(UE::Net::FNetRefHandle Handle)
{
	using namespace UE::Net;

	AActor* Actor = Cast<AActor>(Bridge->GetReplicatedObject(Handle));
	
	if (!Actor)
	{
		ensureMsgf(Actor, TEXT("UNetActorFactory::CreateAndFillHeader could not find actor tied to handle: %s"), *Bridge->PrintObjectFromNetRefHandle(Handle));
		return nullptr;
	}

	UEngineReplicationBridge* ActorBridge = CastChecked<UEngineReplicationBridge>(Bridge);

	TUniquePtr<FBaseActorNetCreationHeader> BaseHeader;

	const FNetObjectReference ActorReference = ActorBridge->GetOrCreateObjectReference(Actor);	
		
	if (ActorReference.GetRefHandle().IsDynamic())
	{
		FDynamicActorNetCreationHeader* Header = new FDynamicActorNetCreationHeader;
		BaseHeader.Reset(Header);

		// This is more or less a straight copy from ClientPackageMap and needs to be updated accordingly
		UObject* Archetype = nullptr;
		UObject* ActorLevel = nullptr;

		// ChildActor's need to be spawned from the ChildActorTemplate otherwise any non-replicated 
		// customized properties will be incorrect on the Client.
		if (const UChildActorComponent* CAC = Actor->GetParentComponent())
		{
			Archetype = CAC->GetSpawnableChildActorTemplate();
		}

		if (Archetype == nullptr)
		{
			Archetype = Actor->GetArchetype();
		}

		ActorLevel = Actor->GetLevel();

		check(Archetype != nullptr);
		check(Actor->NeedsLoadForClient());			// We have no business sending this unless the client can load
		check(Archetype->NeedsLoadForClient());		// We have no business sending this unless the client can load

		// Fill in Header
		Header->ArchetypeReference = ActorBridge->GetOrCreateObjectReference(Archetype);
		Header->bUsePersistentLevel = (UE::Net::Private::SerializeNewActorOverrideLevel == 0) || (ActorBridge->GetNetDriver()->GetWorld()->PersistentLevel == ActorLevel);

		if (!Header->bUsePersistentLevel)
		{
			Header->LevelReference = ActorBridge->GetOrCreateObjectReference(ActorLevel);
		}

		if (const USceneComponent* RootComponent = Actor->GetRootComponent())
		{
			Header->SpawnInfo.Location = FRepMovement::RebaseOntoZeroOrigin(Actor->GetActorLocation(), Actor);
			Header->SpawnInfo.Rotation = Actor->GetActorRotation();
			Header->SpawnInfo.Scale = Actor->GetActorScale();
			FVector Scale = Actor->GetActorScale();

			if (USceneComponent* AttachParent = RootComponent->GetAttachParent())
			{
				// If this actor is attached, when the scale is serialized on the client, the attach parent property won't be set yet.
				// USceneComponent::SetWorldScale3D (which got called by AActor::SetActorScale3D, which we used to do but no longer).
				// would perform this transformation so that what is sent is relative to the parent. If we don't do this, we will
				// apply the world scale on the client, which will then get applied a second time when the attach parent property is received.
				FTransform ParentToWorld = AttachParent->GetSocketTransform(RootComponent->GetAttachSocketName());
				Scale = Scale * ParentToWorld.GetSafeScaleReciprocal(ParentToWorld.GetScale3D());
			}

			Header->SpawnInfo.Scale = Scale;
			Header->SpawnInfo.Velocity = Actor->GetVelocity();
		}
		else
		{
			check(!Header->SpawnInfo.Location.ContainsNaN());
		}
	}
	else 
	{
		FStaticActorNetCreationHeader* Header = new FStaticActorNetCreationHeader;
		BaseHeader.Reset(Header);
		// Refer by path for static actors
		Header->ObjectReference = ActorReference;
	}

	// Custom actor creation data
	{
		FOutBunch Bunch(UE::Net::Private::SerializeNewActorMaxBunchSize);
		Actor->OnSerializeNewActor(Bunch);
		BaseHeader->CustomCreationDataBitCount = IntCastChecked<uint16>(Bunch.GetNumBits());
		if (BaseHeader->CustomCreationDataBitCount > 0)
		{
			BaseHeader->CustomCreationData.SetNumZeroed(Align(Bunch.GetNumBytes(), 4));
			FMemory::Memcpy(BaseHeader->CustomCreationData.GetData(), Bunch.GetData(), Bunch.GetNumBytes());
		}
	}

	return BaseHeader;
}

bool UNetActorFactory::SerializeHeader(const UE::Net::FCreationHeaderContext& Context, const UE::Net::FNetObjectCreationHeader* Header) 
{
	using namespace UE::Net;

	FNetBitStreamWriter* Writer = Context.Serialization.GetBitStreamWriter();

	const FBaseActorNetCreationHeader* BaseActorHeader = static_cast<const FBaseActorNetCreationHeader*>(Header);

	Writer->WriteBool(BaseActorHeader->IsDynamic());
	
	if (BaseActorHeader->IsDynamic())
	{
		const FDynamicActorNetCreationHeader* DynamicHeader = static_cast<const FDynamicActorNetCreationHeader*>(Header);
		return DynamicHeader->Serialize(Context, SpawnInfoFlags, DefaultSpawnInfo);
	}
	else
	{
		const FStaticActorNetCreationHeader* StaticHeader = static_cast<const FStaticActorNetCreationHeader*>(Header);
		return StaticHeader->Serialize(Context);
	}
	
	
}

TUniquePtr<UE::Net::FNetObjectCreationHeader> UNetActorFactory::CreateAndDeserializeHeader(const UE::Net::FCreationHeaderContext& Context)
{
	using namespace UE::Net;

	TUniquePtr<FBaseActorNetCreationHeader> Header;

	FNetBitStreamReader* Writer = Context.Serialization.GetBitStreamReader();

	const bool bIsDynamic = Writer->ReadBool();
	if (bIsDynamic)
	{
		FDynamicActorNetCreationHeader* DynamicHeader = new FDynamicActorNetCreationHeader;
		Header.Reset(DynamicHeader);

		DynamicHeader->Deserialize(Context, DefaultSpawnInfo);
	}
	else
	{
		FStaticActorNetCreationHeader* StaticHeader = new FStaticActorNetCreationHeader;
		Header.Reset(StaticHeader);

		StaticHeader->Deserialize(Context);
	}
	
	return Header;
}

UNetObjectFactory::FInstantiateResult UNetActorFactory::InstantiateReplicatedObjectFromHeader(const FInstantiateContext& Context, const UE::Net::FNetObjectCreationHeader* Header)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	LLM_SCOPE(ELLMTag::EngineMisc);
	IRIS_PROFILER_SCOPE(NetActorFactory_InstantiateReplicatedObjectFromHeader);

	UEngineReplicationBridge* ActorBridge = CastChecked<UEngineReplicationBridge>(Bridge);
	UNetDriver* NetDriver = ActorBridge->GetNetDriver();

	const FBaseActorNetCreationHeader* BaseHeader = static_cast<const FBaseActorNetCreationHeader*>(Header);

	// For static actors, just find the object using the path
	if (!BaseHeader->IsDynamic())
	{
		const FStaticActorNetCreationHeader* StaticHeader = static_cast<const FStaticActorNetCreationHeader*>(Header);

		AActor* Actor = Cast<AActor>(ActorBridge->ResolveObjectReference(StaticHeader->ObjectReference, Context.ResolveContext));
		if (!Actor)
		{
			UE_LOG(LogIris, Error, TEXT("UNetActorFactory::InstantiateNetObjectFromHeader Failed to resolve ObjectReference: %s . Could not find static actor."), *ActorBridge->DescribeObjectReference(StaticHeader->ObjectReference, Context.ResolveContext));
			return FInstantiateResult();
		}

		UE_LOG(LogIris, Verbose, TEXT("UNetActorFactory::InstantiateNetObjectFromHeader Found static Actor: %s using ObjectReference: %s"), ToCStr(Actor->GetPathName()), *ActorBridge->DescribeObjectReference(StaticHeader->ObjectReference, Context.ResolveContext));

		FInstantiateResult Result {  .Instance = Actor };
			
		if (NetDriver->ShouldClientDestroyActor(Actor))
		{
			Result.Flags |= EReplicationBridgeCreateNetRefHandleResultFlags::AllowDestroyInstanceFromRemote;
		}

		return Result;
	}

	// For dynamic actors, spawn a new actor using the provided information
	const FDynamicActorNetCreationHeader* DynamicHeader = static_cast<const FDynamicActorNetCreationHeader*>(Header);

	// Find archetype
	AActor* Archetype = Cast<AActor>(Bridge->ResolveObjectReference(DynamicHeader->ArchetypeReference, Context.ResolveContext));
	if (!Archetype)
	{
		UE_LOG(LogIris, Error, TEXT("UNetActorFactory::InstantiateNetObjectFromHeader Unable to instantiate actor, failed to resolve archetype: %s"), *Bridge->DescribeObjectReference(DynamicHeader->ArchetypeReference, Context.ResolveContext));
		return FInstantiateResult();
	}

	// Find level
	ULevel* Level = nullptr;
	if (!DynamicHeader->bUsePersistentLevel)
	{
		Level = Cast<ULevel>(Bridge->ResolveObjectReference(DynamicHeader->LevelReference, Context.ResolveContext));
	}

	LLM_SCOPE_DYNAMIC_STAT_OBJECTPATH(Archetype->GetPackage(), ELLMTagSet::Assets);
	LLM_SCOPE_DYNAMIC_STAT_OBJECTPATH(Archetype->GetClass(), ELLMTagSet::AssetClasses);
	UE_TRACE_METADATA_SCOPE_ASSET(Archetype, Archetype->GetClass());
			
	// For streaming levels, it's possible that the owning level has been made not-visible but is still loaded.
	// In that case, the level will still be found but the owning world will be invalid.
	// If that happens, wait to spawn the Actor until the next time the level is streamed in.
	// At that point, the Server should resend any dynamic Actors.
			
	check(Level == nullptr || Level->GetWorld() != nullptr);

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Template = Archetype;
	SpawnInfo.OverrideLevel = Level;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnInfo.bRemoteOwned = true;
	SpawnInfo.bNoFail = true;

	UWorld* World = NetDriver->GetWorld();
	FVector SpawnLocation = FRepMovement::RebaseOntoLocalOrigin(DynamicHeader->SpawnInfo.Location, World->OriginLocation);
		
	AActor* Actor = World->SpawnActorAbsolute(Archetype->GetClass(), FTransform(DynamicHeader->SpawnInfo.Rotation, SpawnLocation), SpawnInfo);

	// For Iris we expect that we will be able to spawn the actor as streaming always is controlled from server
	if (!Actor)
	{
		ensureMsgf(Actor, TEXT("UNetActorFactory::InstantiateNetObjectFromHeader SpawnActor failed. Used Archetype: %s ObjectReference: %s"), *GetNameSafe(Archetype), *ActorBridge->DescribeObjectReference(DynamicHeader->ArchetypeReference, Context.ResolveContext));
		return FInstantiateResult();
	}

	static constexpr float Epsilon = UE_KINDA_SMALL_NUMBER;

	// Set Velocity if it differs from Default
	if (!DynamicHeader->SpawnInfo.Velocity.Equals(DefaultSpawnInfo.Velocity, Epsilon))
	{
		Actor->PostNetReceiveVelocity(DynamicHeader->SpawnInfo.Velocity);
	}
					
	// Set Scale if it differs from Default
	if (!DynamicHeader->SpawnInfo.Scale.Equals(DefaultSpawnInfo.Scale, Epsilon))
	{
		Actor->SetActorRelativeScale3D(DynamicHeader->SpawnInfo.Scale);
	}

	FInstantiateResult Result { .Instance = Actor };

	if (NetDriver->ShouldClientDestroyActor(Actor))
	{
		Result.Flags |= EReplicationBridgeCreateNetRefHandleResultFlags::AllowDestroyInstanceFromRemote;
	}

	UE_LOG(LogIris, Verbose, TEXT("UNetActorFactory::InstantiateNetObjectFromHeader Spawned Actor: %s using Archetype: %s"), *Actor->GetPathName(), *GetNameSafe(Archetype));

	return Result;
}

void UNetActorFactory::PostInstantiation(const FPostInstantiationContext& Context)
{
	using namespace UE::Net;

	AActor* Actor = CastChecked<AActor>(Context.Instance);
	if (!Actor)
	{
		return;
	}

	UEngineReplicationBridge* ActorBridge = CastChecked<UEngineReplicationBridge>(Bridge);
	UNetDriver* NetDriver = ActorBridge->GetNetDriver();

	const FBaseActorNetCreationHeader* BaseHeader = static_cast<const FBaseActorNetCreationHeader*>(Context.Header);

	// OnActorChannelOpen
	{
		UNetConnection* Connection = NetDriver->GetConnectionByHandle(FConnectionHandle(Context.ConnectionId));
		FInBunch Bunch(Connection, const_cast<uint8*>(BaseHeader->CustomCreationData.GetData()), BaseHeader->CustomCreationDataBitCount);
		Actor->OnActorChannelOpen(Bunch, Connection);

		if (Bunch.IsError() || Bunch.GetBitsLeft() != 0)
		{
			UE_LOG(LogIris, Error, TEXT("UNetActorFactory::PostInstantiation deserialization error in OnActorChannelOpen for Actor: %s"), *Actor->GetPathName());
			check(false);
			return;
		}
	}

	// Wake up from dormancy. This is important for client replays.
	ActorBridge->WakeUpObjectInstantiatedFromRemote(Actor);
}

void UNetActorFactory::PostInit(const FPostInitContext& Context)
{
	// PostNetInit is only called for dynamic actors
	if (Context.Handle.IsDynamic())
	{
		AActor* Actor = CastChecked<AActor>(Context.Instance);
		Actor->PostNetInit();
	}
}

 namespace UE::Net
 {

//------------------------------------------------------------------------
// FStaticActorNetCreationHeader
//------------------------------------------------------------------------

bool FStaticActorNetCreationHeader::Serialize(const FCreationHeaderContext& Context) const
{
	using namespace UE::Net::Private;

	FNetBitStreamWriter* Writer = Context.Serialization.GetBitStreamWriter();

	WriteFullNetObjectReference(Context.Serialization, ObjectReference);

	if (Writer->WriteBool(CustomCreationDataBitCount > 0))
	{
		Writer->WriteBits(CustomCreationDataBitCount - 1U, 16U);
		Writer->WriteBitStream(reinterpret_cast<const uint32*>(CustomCreationData.GetData()), 0U, CustomCreationDataBitCount);
	}

	return true;
}

bool FStaticActorNetCreationHeader::Deserialize(const FCreationHeaderContext& Context)
{
	using namespace UE::Net::Private;

	FNetBitStreamReader* Reader = Context.Serialization.GetBitStreamReader();

	ReadFullNetObjectReference(Context.Serialization, ObjectReference);

	if (Reader->ReadBool())
	{
		CustomCreationDataBitCount = 1U + Reader->ReadBits(16U);
		CustomCreationData.SetNumZeroed(((CustomCreationDataBitCount + 31U) & ~31U) >> 3U);
		Reader->ReadBitStream(reinterpret_cast<uint32*>(CustomCreationData.GetData()), CustomCreationDataBitCount);
	}
	
	return true;
}


FString FStaticActorNetCreationHeader::ToString() const
{
	return FString::Printf(TEXT("FStaticActorNetCreationHeader (ProtocolId:0x%x):\n\t"
								"ObjectReference=%s\n\t"
								"CustomCreationData=%u bits"),
						   		GetProtocolId(),
						   		*ObjectReference.ToString(),
								CustomCreationDataBitCount);
}

//------------------------------------------------------------------------
// FDynamicActorNetCreationHeader
//------------------------------------------------------------------------

bool FDynamicActorNetCreationHeader::Serialize(const FCreationHeaderContext& Context, UE::Net::Private::EActorNetSpawnInfoFlags SpawnFlags, const FActorNetSpawnInfo& DefaultSpawnInfo) const
{
	using namespace UE::Net::Private;

	FNetBitStreamWriter* Writer = Context.Serialization.GetBitStreamWriter();

	// Write Archetype and LevelPath
	WriteFullNetObjectReference(Context.Serialization, ArchetypeReference);
		
	// Only write the LevelPath if it differs from the persistent level
	if (!Writer->WriteBool(bUsePersistentLevel))
	{
		WriteFullNetObjectReference(Context.Serialization, LevelReference);
	}

	const bool bQuantizeLocation = (SpawnFlags&EActorNetSpawnInfoFlags::QuantizeLocation)!=EActorNetSpawnInfoFlags::None;
	const bool bQuantizeScale = (SpawnFlags&EActorNetSpawnInfoFlags::QuantizeScale)!=EActorNetSpawnInfoFlags::None;
	const bool bQuantizeVelocity = (SpawnFlags&EActorNetSpawnInfoFlags::QuantizeVelocity)!=EActorNetSpawnInfoFlags::None;

	// Write actor spawn info
	WriteConditionallyQuantizedVector(Writer, SpawnInfo.Location, DefaultSpawnInfo.Location, bQuantizeLocation);
	WriteConditionallyQuantizedVector(Writer, SpawnInfo.Scale, DefaultSpawnInfo.Scale, bQuantizeScale);
	WriteConditionallyQuantizedVector(Writer, SpawnInfo.Velocity, DefaultSpawnInfo.Velocity, bQuantizeVelocity);

	// For rotation we use 0.001f for Rotation comparison to keep consistency with old behavior.
	static constexpr float RotationEpsilon = 0.001f;
	WriteRotator(Writer, SpawnInfo.Rotation, DefaultSpawnInfo.Rotation, RotationEpsilon);

	if (Writer->WriteBool(CustomCreationDataBitCount > 0))
	{
		Writer->WriteBits(CustomCreationDataBitCount - 1U, 16U);
		Writer->WriteBitStream(reinterpret_cast<const uint32*>(CustomCreationData.GetData()), 0U, CustomCreationDataBitCount);
	}

	return true;
}

bool FDynamicActorNetCreationHeader::Deserialize(const FCreationHeaderContext& Context, const FActorNetSpawnInfo& DefaultSpawnInfo)
{
	using namespace UE::Net::Private;

	FNetBitStreamReader* Reader = Context.Serialization.GetBitStreamReader();

	// Read Archetype
	ReadFullNetObjectReference(Context.Serialization, ArchetypeReference);
		
	bUsePersistentLevel = Reader->ReadBool();
	if (!bUsePersistentLevel)
	{
		ReadFullNetObjectReference(Context.Serialization, LevelReference);
	}

	// Read actor spawn info
	ReadConditionallyQuantizedVector(Reader, SpawnInfo.Location, DefaultSpawnInfo.Location);
	ReadConditionallyQuantizedVector(Reader, SpawnInfo.Scale, DefaultSpawnInfo.Scale);
	ReadConditionallyQuantizedVector(Reader, SpawnInfo.Velocity, DefaultSpawnInfo.Velocity);

	ReadRotator(Reader, SpawnInfo.Rotation, DefaultSpawnInfo.Rotation);
	
	if (Reader->ReadBool())
	{
		CustomCreationDataBitCount = 1U + Reader->ReadBits(16U);
		CustomCreationData.SetNumZeroed(((CustomCreationDataBitCount + 31U) & ~31U) >> 3U);
		Reader->ReadBitStream(reinterpret_cast<uint32*>(CustomCreationData.GetData()), CustomCreationDataBitCount);
	}
	
	return true;
}


FString FDynamicActorNetCreationHeader::ToString() const
{
	return FString::Printf(TEXT("FDynamicActorNetCreationHeader (ProtocolId:0x%x):\n\t"
								"ArchetypeReference=%s\n\t"
								"SpawnInfo.Location=%s\n\t"
								"SpawnInfo.Rotation=%s\n\t"
								"SpawnInfo.Scale=%s\n\t"
								"SpawnInfo.Velocity=%s\n\t"		
								"bUsePersistentLevel=%s\n\t"
								"LevelReference=%s\n\t"
								"CustomCreationData=%u bits"),
						   		GetProtocolId(),
								*ArchetypeReference.ToString(),
								*SpawnInfo.Location.ToCompactString(),
								*SpawnInfo.Rotation.ToCompactString(),
								*SpawnInfo.Scale.ToCompactString(),
								*SpawnInfo.Velocity.ToCompactString(),		
								bUsePersistentLevel?TEXT("True"):TEXT("False"),
								*LevelReference.ToString(),
								CustomCreationDataBitCount);
}


} // end namespace UE::Net

#endif // UE_WITH_IRIS

