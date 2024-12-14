// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"

#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"

#include "Iris/IrisConfigInternal.h"

#include "Iris/Core/IrisCsv.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Core/IrisMemoryTracker.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/Core/IrisDebugging.h"
#include "Iris/Core/IrisDelegates.h"

#include "Net/Core/NetBitArrayPrinter.h"
#include "Net/Core/NetHandle/NetHandleManager.h"
#include "Net/Core/Connection/NetEnums.h"
#include "Net/Core/PropertyConditions/PropertyConditionsDelegates.h"
#include "Net/Core/Trace/NetTrace.h"
#include "Net/Core/Trace/NetDebugName.h"

#include "Iris/ReplicationSystem/LegacyPushModel.h"
#include "Iris/ReplicationSystem/ObjectPollFrequencyLimiter.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridgeConfig.h"
#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h"
#include "Iris/ReplicationSystem/ReplicationConnections.h"
#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Iris/ReplicationSystem/ReplicationFragmentInternal.h"
#include "Iris/ReplicationSystem/ReplicationFragmentUtil.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemTypes.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"
#include "Iris/ReplicationSystem/ReplicationWriter.h"
#include "Iris/ReplicationSystem/NetObjectFactory.h"
#include "Iris/ReplicationSystem/NetObjectFactoryRegistry.h"
#include "Iris/ReplicationSystem/RepTag.h"
#include "Iris/ReplicationSystem/Polling/ObjectPoller.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamUtil.h"

namespace UE::Net::Private::ObjectBridgeDebugging
{
	extern void RemoteProtocolMismatchDetected(TMap<FObjectKey, bool>& ArchetypesAlreadyPrinted, UReplicationSystem* , uint32, const FReplicationFragments&, const UObject*, const UObject*);
}

#ifndef UE_IRIS_VALIDATE_PROTOCOLS
#	define UE_IRIS_VALIDATE_PROTOCOLS !UE_BUILD_SHIPPING
#endif

DEFINE_LOG_CATEGORY(LogIrisFilterConfig)

#define UE_LOG_OBJECTREPLICATIONBRIDGE(Category, Format, ...)  UE_LOG(LogIrisBridge, Category, TEXT("ObjectReplicationBridge(%u)::") Format, GetReplicationSystem()->GetId(), ##__VA_ARGS__)

static bool bUseFrequencyBasedPolling = true;
static FAutoConsoleVariableRef CVarUseFrequencyBasedPolling(
		TEXT("net.Iris.UseFrequencyBasedPolling"),
		bUseFrequencyBasedPolling,
		TEXT("Whether to use frequency based polling or not. Default is true.")
		);

static bool bUseDormancyToFilterPolling = true;
static FAutoConsoleVariableRef CVarUseDormancyToFilterPolling(
		TEXT("net.Iris.UseDormancyToFilterPolling"),
		bUseDormancyToFilterPolling,
		TEXT("Whether we should use dormancy to filter out objects that we should not poll. Default is true.")
		);

static bool bAllowPollPeriodOverrides = true;
static FAutoConsoleVariableRef CVarAllowPollPeriodOverrides(
		TEXT("net.Iris.AllowPollPeriodOverrides"),
		bAllowPollPeriodOverrides,
		TEXT("Whether we allow poll period overrides set in ObjectReplicationBridgeConfig. Default is true.")
		);

static bool bEnableFilterMappings = true;
static FAutoConsoleVariableRef CVarEnableFilterMappings(
		TEXT("net.Iris.EnableFilterMappings"),
		bEnableFilterMappings,
		TEXT("Whether we honor filter mappings set in ObjectReplicationBridgeConfig. If filter mappings are enabled then objects may also be assigned the default spatial filter even if there aren't any specific mappings. Default is true.")
		);

static bool bEnableForceNetUpdate = false;
static FAutoConsoleVariableRef CVarEnableForceNetUpdate(
	TEXT("net.Iris.EnableForceNetUpdate"),
	bEnableForceNetUpdate,
	TEXT("When true the system only allows ForceNetUpdate to skip the poll frequency of objects. When false any MarkDirty object will be immediately polled.")
);

static int32 GEnsureNetRefHandleError = 0;
static FAutoConsoleVariableRef CvarEnsureNetRefHandleError(
	TEXT("net.Iris.EnsureNetRefHandleError"),
	GEnsureNetRefHandleError,
	TEXT("Chooses if we should ensure when a NetRefHandleError was reported. -1=never ensure. 0=always ensure. 1..X=ensure only for specific error type")
);

#if UE_BUILD_SHIPPING
static bool bUseVerboseIrisCsvStats = false;
#else
static bool bUseVerboseIrisCsvStats = true;
#endif
static FAutoConsoleVariableRef CVarUseVerboseCsvStats(
	TEXT("net.Iris.UseVerboseIrisCsvStats"),
	bUseVerboseIrisCsvStats,
	TEXT("Whether to use verbose per-class csv stats. Default is false in Shipping, otherwise True.")
);

namespace UE::Net::Private
{
	void CallRegisterReplicationFragments(UObject* Object, FFragmentRegistrationContext& Context, EFragmentRegistrationFlags RegistrationFlags)
	{
		Object->RegisterReplicationFragments(Context, RegistrationFlags);

		if (!Context.WasRegistered())
		{
			FReplicationFragmentUtil::CreateAndRegisterFragmentsForObject(Object, Context, RegistrationFlags);
			ensure(Context.WasRegistered());
		}
	}

	struct FReplicationInstanceProtocolDeleter
	{
		void operator()(FReplicationInstanceProtocol* InstanceProtocol)
		{ 
			if (InstanceProtocol != nullptr)
			{
				FReplicationProtocolManager::DestroyInstanceProtocol(InstanceProtocol);
			}
		}
	};
	typedef TUniquePtr<FReplicationInstanceProtocol, FReplicationInstanceProtocolDeleter> FReplicationInstanceProtocolPtr;
}

UObjectReplicationBridge::UObjectReplicationBridge()
: Super()
, PollFrequencyLimiter(new UE::Net::Private::FObjectPollFrequencyLimiter())
, DefaultSpatialFilterHandle(UE::Net::InvalidNetObjectFilterHandle)
{
	SetShouldUseDefaultSpatialFilterFunction([](const UClass*){ return false; });
	SetShouldSubclassUseSameFilterFunction([](const UClass*,const UClass*){ return true; });
}

UObjectReplicationBridge::~UObjectReplicationBridge()
{
	delete PollFrequencyLimiter;
}

bool UObjectReplicationBridge::ShouldUseVerboseCsvStats() const
{
	return bUseVerboseIrisCsvStats;
}

void UObjectReplicationBridge::Initialize(UReplicationSystem* InReplicationSystem)
{
	Super::Initialize(InReplicationSystem);

	const uint32 CurrentMaxInternalIndex = NetRefHandleManager->GetCurrentMaxInternalNetRefIndex();
	PollFrequencyLimiter->Init(CurrentMaxInternalIndex);
	ObjectsWithObjectReferences.Init(CurrentMaxInternalIndex);
	GarbageCollectionAffectedObjects.Init(CurrentMaxInternalIndex);

	NetRefHandleManager->GetOnMaxInternalNetRefIndexIncreasedDelegate().AddUObject(this, &UObjectReplicationBridge::OnMaxInternalNetRefIndexIncreased);

	LoadConfig();

	InitConditionalPropertyDelegates();
	InitNetObjectFactories();
}

void UObjectReplicationBridge::Deinitialize()
{
	NetRefHandleManager->GetOnMaxInternalNetRefIndexIncreasedDelegate().RemoveAll(this);

	DeinitNetObjectFactories();

	UE::Net::Private::FPropertyConditionDelegates::GetOnPropertyCustomConditionChangedDelegate().Remove(OnCustomConditionChangedHandle);
	UE::Net::Private::FPropertyConditionDelegates::GetOnPropertyDynamicConditionChangedDelegate().Remove(OnDynamicConditionChangedHandle);
	OnCustomConditionChangedHandle.Reset();
	OnDynamicConditionChangedHandle.Reset();
	PollFrequencyLimiter->Deinit();
	Super::Deinitialize();
}

void UObjectReplicationBridge::InitNetObjectFactories()
{
	using namespace UE::Net;
	TConstArrayView<FNetObjectFactoryRegistry::FFactoryData> RegisteredFactories = FNetObjectFactoryRegistry::GetRegisteredFactories();

	check(NetObjectFactories.IsEmpty());

	for(int32 Index=0; Index < RegisteredFactories.Num(); ++Index )
	{
		const FNetObjectFactoryRegistry::FFactoryData& Data = RegisteredFactories[Index];
		check(Data.Id == (FNetObjectFactoryId)Index);
		check(Data.NetFactoryClass);

		UNetObjectFactory* Factory = NewObject<UNetObjectFactory>(this, Data.NetFactoryClass);
		NetObjectFactories.Add(Factory);

		Factory->Init(Data.Id, this);
	}
}

void UObjectReplicationBridge::DeinitNetObjectFactories()
{
	for (UNetObjectFactory* Factory : NetObjectFactories)
	{
		Factory->Deinit();
	}

	NetObjectFactories.Empty();
}

UNetObjectFactory* UObjectReplicationBridge::GetNetFactory(UE::Net::FNetObjectFactoryId FactoryId) const
{
	if (ensureMsgf(FactoryId < NetObjectFactories.Num() && FactoryId != UE::Net::InvalidNetObjectFactoryId, TEXT("GetNetFactory invalid factory ID %u, num factories: %d"), FactoryId, NetObjectFactories.Num()))
	{
		return NetObjectFactories[FactoryId];
	}

	return nullptr;
}

void UObjectReplicationBridge::OnMaxInternalNetRefIndexIncreased(UE::Net::Private::FInternalNetRefIndex NewMaxInternalIndex)
{
	PollFrequencyLimiter->OnMaxInternalNetRefIndexIncreased(NewMaxInternalIndex);
	ObjectsWithObjectReferences.SetNumBits(NewMaxInternalIndex);
	GarbageCollectionAffectedObjects.SetNumBits(NewMaxInternalIndex);
}

UObject* UObjectReplicationBridge::GetObjectFromReferenceHandle(FNetRefHandle RefHandle) const
{
	return GetObjectReferenceCache()->GetObjectFromReferenceHandle(RefHandle);
}

UObject* UObjectReplicationBridge::ResolveObjectReference(const UE::Net::FNetObjectReference& Reference, const UE::Net::FNetObjectResolveContext& ResolveContext)
{
	return GetObjectReferenceCache()->ResolveObjectReference(Reference, ResolveContext);
}

FString UObjectReplicationBridge::DescribeObjectReference(const UE::Net::FNetObjectReference& Reference, const UE::Net::FNetObjectResolveContext& ResolveContext)
{
	return GetObjectReferenceCache()->DescribeObjectReference(Reference, ResolveContext);
}

UE::Net::FNetObjectReference UObjectReplicationBridge::GetOrCreateObjectReference(const UObject* Instance) const
{
	return GetObjectReferenceCache()->GetOrCreateObjectReference(Instance);
}

UE::Net::FNetObjectReference UObjectReplicationBridge::GetOrCreateObjectReference(const FString& Path, const UObject* Outer) const
{
	return GetObjectReferenceCache()->GetOrCreateObjectReference(Path, Outer);
}

void UObjectReplicationBridge::AddStaticDestructionInfo(const FString& ObjectPath, const UObject* Outer, const FEndReplicationParameters& Parameters)
{
	UE::Net::FNetObjectReference ObjectRef = GetOrCreateObjectReference(ObjectPath, Outer);
	if (ObjectRef.IsValid())
	{
		StoreDestructionInfo(ObjectRef.GetRefHandle(), Parameters);
	}
}

UObject* UObjectReplicationBridge::GetReplicatedObject(FNetRefHandle Handle) const
{
	return IsReplicatedHandle(Handle) ? GetObjectFromReferenceHandle(Handle) : nullptr;
};

UE::Net::FNetRefHandle UObjectReplicationBridge::GetReplicatedRefHandle(const UObject* Object, EGetRefHandleFlags GetRefHandleFlags) const
{
	const FNetRefHandle Handle = GetObjectReferenceCache()->GetObjectReferenceHandleFromObject(Object, GetRefHandleFlags);
	return IsReplicatedHandle(Handle) ? Handle : FNetRefHandle::GetInvalid();
}

UE::Net::FNetRefHandle UObjectReplicationBridge::GetReplicatedRefHandle(FNetHandle Handle) const
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	// If the object is replicated by the owning ReplicationSystem the internal handle should be valid.
	FInternalNetRefIndex ObjectInternalIndex = NetRefHandleManager->GetInternalIndexFromNetHandle(Handle);
	if (ObjectInternalIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return FNetRefHandle::GetInvalid();
	}

	const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectInternalIndex);
	return ObjectData.RefHandle;
}

UE::Net::FNetRefHandle UObjectReplicationBridge::StartReplicatingNetObject(UObject* Instance, UE::Net::EReplicationFragmentTraits Traits, UE::Net::FNetObjectFactoryId NetFactoryId)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	FNetRefHandle AllocatedRefHandle = ObjectReferenceCache->CreateObjectReferenceHandle(Instance);

	// If we failed to assign a handle, or if the Handle already is replicating, just return the handle
	if (!AllocatedRefHandle.IsValid())
	{
		return FNetRefHandle::GetInvalid();
	}

	if (IsReplicatedHandle(AllocatedRefHandle))
	{
		return AllocatedRefHandle;
	}

	IRIS_PROFILER_SCOPE(StartReplicatingNetObject);
	
	FFragmentRegistrationContext FragmentRegistrationContext(GetReplicationStateDescriptorRegistry(), GetReplicationSystem(), Traits);

	// For everything derived from UObject we can call the virtual function RegisterReplicationFragments	
	CallRegisterReplicationFragments(Instance, FragmentRegistrationContext, EFragmentRegistrationFlags::None);

	const FReplicationFragments& RegisteredFragments = FFragmentRegistrationContextPrivateAccessor::GetReplicationFragments(FragmentRegistrationContext);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (RegisteredFragments.IsEmpty() && !FragmentRegistrationContext.IsFragmentlessNetObject())
	{
		// Look if the class registered replicated properties
		TArray<FLifetimeProperty> ReplicatedProps;
		Instance->GetLifetimeReplicatedProps(ReplicatedProps);
		
		if (ReplicatedProps.IsEmpty())
		{
			ensureMsgf(false, TEXT("NetObject %s (class %s) registered no fragments. Call SetIsFragmentlessNetObject if this is intentional."), *GetNameSafe(Instance), *GetNameSafe(Instance->GetClass()));
		}
		else
		{
			ensureMsgf(false, TEXT("NetObject %s (class %s) registered no fragments but GetLifetimeReplicatedProps returned %d variables. Make sure to call CreateAndRegisterFragmentsForObject in RegisterReplicationFragments"),
				*GetNameSafe(Instance), *GetNameSafe(Instance->GetClass()), ReplicatedProps.Num());
		}
	}
#endif

	// We currently identify protocols by local archetype or CDO pointer and verified the protocol id received from server and the hash of the default state
	const UObject* ArchetypeOrCDOUsedAsKey = Instance->GetArchetype();

	// Create Protocols
	FReplicationProtocolManager* ProtocolManager = GetReplicationProtocolManager();

	FReplicationInstanceProtocolPtr InstanceProtocol(ProtocolManager->CreateInstanceProtocol(RegisteredFragments));
		
	FReplicationProtocolIdentifier ProtocolIdentifier = FReplicationProtocolManager::CalculateProtocolIdentifier(RegisteredFragments);
	const FReplicationProtocol* ReplicationProtocol = ProtocolManager->GetReplicationProtocol(ProtocolIdentifier, ArchetypeOrCDOUsedAsKey);
	if (!ReplicationProtocol)
	{
		FCreateReplicationProtocolParameters CreateProtocolParams {.ArchetypeOrCDOUsedAsKey = ArchetypeOrCDOUsedAsKey, .TypeStatsIndex = GetTypeStatsIndex(Instance->GetClass()) };
		ReplicationProtocol = ProtocolManager->CreateReplicationProtocol(ProtocolIdentifier, RegisteredFragments, *Instance->GetClass()->GetName(), CreateProtocolParams);
	}
#if UE_IRIS_VALIDATE_PROTOCOLS
	else
	{
		const bool bIsValidProtocol = FReplicationProtocolManager::ValidateReplicationProtocol(ReplicationProtocol, RegisteredFragments);
		if (!bIsValidProtocol)
		{
			UE_LOG_OBJECTREPLICATIONBRIDGE(Error, TEXT("StartReplicatingNetObject Found invalid protocol ProtocolId:0x%x for Object named %s"), ReplicationProtocol->ProtocolIdentifier, *Instance->GetName());
			return FNetRefHandle::GetInvalid();
		}
	}
#endif
		
	if (!ReplicationProtocol)
	{
		UE_LOG(LogIris, Error, TEXT("StartReplicatingNetObject - Failed to create protocol for %s"), ToCStr(Instance->GetPathName()));
		return FNetRefHandle::GetInvalid();
	}

	IRIS_PROFILER_PROTOCOL_NAME(ReplicationProtocol->DebugName->Name);		

	// Create NetHandle and bind instance
	FNetHandle NetHandle = FNetHandleManager::GetOrCreateNetHandle(Instance);
	FNetRefHandle RefHandle = InternalCreateNetObject(AllocatedRefHandle, NetHandle, ReplicationProtocol);

	if (!RefHandle.IsValid())
	{
		UE_LOG_OBJECTREPLICATIONBRIDGE(Warning, TEXT("StartReplicatingNetObject Failed to create NetRefHandle with ProtocolId:0x%x for Object named %s"), (ReplicationProtocol != nullptr ? ReplicationProtocol->ProtocolIdentifier : FReplicationProtocolIdentifier(0)), *Instance->GetName());
		return FNetRefHandle::GetInvalid();
	}

	UE_LOG_OBJECTREPLICATIONBRIDGE(Verbose, TEXT("StartReplicatingNetObject Created %s with ProtocolId:0x%x for Object named %s"), *RefHandle.ToString(), ReplicationProtocol->ProtocolIdentifier, *Instance->GetName());

	// Attach the instance and bind the instance protocol to dirty tracking
	constexpr bool bBindInstanceProtocol = true;
	InternalAttachInstanceToNetRefHandle(RefHandle, bBindInstanceProtocol, InstanceProtocol.Get(), Instance, NetHandle);
#if WITH_PUSH_MODEL
	SetNetPushIdOnInstance(InstanceProtocol.Get(), NetHandle);
#endif

	const FInternalNetRefIndex InternalReplicationIndex = NetRefHandleManager->GetInternalIndex(RefHandle);

	check(NetFactoryId != InvalidNetObjectFactoryId);
	NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalReplicationIndex).NetFactoryId = NetFactoryId;

	// Initialize conditionals
	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	ReplicationSystemInternal->GetConditionals().InitPropertyCustomConditions(InternalReplicationIndex);

	// Keep track of handles with object references for garbage collection's sake.
	ObjectsWithObjectReferences.SetBitValue(InternalReplicationIndex, EnumHasAnyFlags(InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::HasObjectReference));

	if (ShouldClassBeDeltaCompressed(Instance->GetClass()))
	{
		ReplicationSystem->SetDeltaCompressionStatus(RefHandle, ENetObjectDeltaCompressionStatus::Allow);
	}

	// Release instance protocol from the UniquePtr as it is now successfully bound to the handle
	(void)InstanceProtocol.Release();

	return RefHandle;
}

UE::Net::FNetRefHandle UObjectReplicationBridge::StartReplicatingRootObject(UObject* Instance, const FRootObjectReplicationParams& Params, UE::Net::FNetObjectFactoryId NetFactoryId)
{
	LLM_SCOPE_BYTAG(IrisState);

	using namespace UE::Net;
	using namespace UE::Net::Private;

	if (bBlockStartRootObjectReplication)
	{
		ensureMsgf(false, TEXT("StartReplicatingRootObject is not allowed during this operation. %s will not be replicated"), *GetNameSafe(Instance));
		return FNetRefHandle::GetInvalid();
	}
	
	EReplicationFragmentTraits Traits = EReplicationFragmentTraits::CanReplicate;
	Traits |= Params.bNeedsPreUpdate ? EReplicationFragmentTraits::NeedsPreSendUpdate : EReplicationFragmentTraits::None;
	Traits |= Params.bNeedsWorldLocationUpdate ? EReplicationFragmentTraits::NeedsWorldLocationUpdate : EReplicationFragmentTraits::None;

	// Register the object
	FNetRefHandle RefHandle = StartReplicatingNetObject(Instance, Traits, NetFactoryId);

	if (!RefHandle.IsValid())
	{
		return FNetRefHandle::GetInvalid();;
	}
	
	// Now configure the RootObject
	const FInternalNetRefIndex ObjectIndex = NetRefHandleManager->GetInternalIndex(RefHandle);

	// Set poll frame period
	float PollFrequency = Params.PollFrequency;
	FindOrCachePollFrequency(Instance->GetClass(), PollFrequency);
			
	uint8 PollFramePeriod = ConvertPollFrequencyIntoFrames(PollFrequency);
	PollFrequencyLimiter->SetPollFramePeriod(ObjectIndex, PollFramePeriod);

	if (Params.bNeedsWorldLocationUpdate)
	{
		FWorldLocations& WorldLocations = ReplicationSystem->GetReplicationSystemInternal()->GetWorldLocations();
		WorldLocations.InitObjectInfoCache(ObjectIndex);
	}

	// Set prioritizer
	const bool bRequireForceEnabled = Params.StaticPriority > 0.0f;
	const FNetObjectPrioritizerHandle PrioritizerHandle = GetPrioritizer(Instance->GetClass(), bRequireForceEnabled);

	// Set static priority if valid unless we have a force enabled prioritizer.
	if (Params.StaticPriority > 0.0f && PrioritizerHandle == InvalidNetObjectPrioritizerHandle)
	{
		ReplicationSystem->SetStaticPriority(RefHandle, Params.StaticPriority);
	}
	else
	{
		if (PrioritizerHandle != InvalidNetObjectPrioritizerHandle)
		{
			ReplicationSystem->SetPrioritizer(RefHandle, PrioritizerHandle);
		}
		else if (Params.bNeedsWorldLocationUpdate || HasRepTag(ReplicationSystem->GetReplicationProtocol(RefHandle), RepTag_WorldLocation))
		{
			ReplicationSystem->SetPrioritizer(RefHandle, DefaultSpatialNetObjectPrioritizerHandle);
		}
	}

	// Set dynamic filter
	AssignDynamicFilter(Instance, Params, RefHandle);

	// Spatially filtered non-dormant objects requires frequent world location updates. Expecting a better solution that instead of us polling will inform us when locations change, UE-193004.
	if (Params.bNeedsWorldLocationUpdate && !Params.bIsDormant)
	{
		OptionallySetObjectRequiresFrequentWorldLocationUpdate(RefHandle, true);
	}

	return RefHandle;
}

void UObjectReplicationBridge::AssignDynamicFilter(UObject* Instance, const FRootObjectReplicationParams& Params, FNetRefHandle RefHandle)
{
	using namespace UE::Net;

	if (!bEnableFilterMappings)
	{
		return;
	}

	FNetObjectFilterHandle FilterHandle = InvalidNetObjectFilterHandle;
	FName FilterConfigProfile;

	if (Params.bUseExplicitDynamicFilter)
	{
		if (Params.ExplicitDynamicFilterName != NAME_None)
		{
			FilterHandle = ReplicationSystem->GetFilterHandle(Params.ExplicitDynamicFilterName);
			
			UE_CLOG(FilterHandle == InvalidNetObjectFilterHandle, LogIrisBridge, Error, TEXT("Could not assign explicit dynamic filter to %s. No filters named %s exist"), *GetPathNameSafe(Instance), *Params.ExplicitDynamicFilterName.ToString() );
			ensure(FilterHandle != InvalidNetObjectFilterHandle);
		}
	}
	else if (Params.bUseClassConfigDynamicFilter)
	{
		constexpr bool bRequireForceEnabled = false;
		FilterHandle = GetDynamicFilter(Instance->GetClass(), bRequireForceEnabled, FilterConfigProfile);
	}

	if (FilterHandle != InvalidNetObjectFilterHandle)
	{
		UE_LOG_OBJECTREPLICATIONBRIDGE(Verbose, TEXT("StartReplicatingRootObject Filter: %s will be used for %s. (FilterProfile: %s)"), *(ReplicationSystem->GetFilterName(FilterHandle).ToString()), *NetRefHandleManager->PrintObjectFromNetRefHandle(RefHandle), *FilterConfigProfile.ToString());
		ReplicationSystem->SetFilter(RefHandle, FilterHandle, FilterConfigProfile);
	}
	
}

UE::Net::FNetRefHandle UObjectReplicationBridge::StartReplicatingSubObject(UObject* Instance, const FSubObjectReplicationParams& Params , UE::Net::FNetObjectFactoryId NetFactoryId)
{
	LLM_SCOPE_BYTAG(IrisState);

	using namespace UE::Net;
	using namespace UE::Net::Private;

	const FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	const FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();

	checkf(IsReplicatedHandle(Params.RootObjectHandle), TEXT("Owner %s (%s) must be replicated for subobject %s to replicate."), 
		*GetNameSafe(LocalNetRefHandleManager.GetReplicatedObjectInstance(LocalNetRefHandleManager.GetInternalIndex(Params.RootObjectHandle))), *Params.RootObjectHandle.ToString(), *GetNameSafe(Instance));

	checkf(!Instance->HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject), TEXT("Iris cannot replicate subobject %s owned by %s because it's an %s"), 
		*GetNameSafe(Instance), *GetNameSafe(LocalNetRefHandleManager.GetReplicatedObjectInstance(LocalNetRefHandleManager.GetInternalIndex(Params.RootObjectHandle))), Instance->HasAnyFlags(RF_ArchetypeObject)?TEXT("Archetype"):TEXT("DefaultObject"));

	FNetRefHandle SubObjectRefHandle = GetReplicatedRefHandle(Instance);
	if (SubObjectRefHandle.IsValid())
	{
		// Verify that the existing object is a subobject of the owner
		check(Params.RootObjectHandle == LocalNetRefHandleManager.GetRootObjectOfSubObject(SubObjectRefHandle));
		return SubObjectRefHandle;
	}
	
	const EReplicationFragmentTraits Traits = EReplicationFragmentTraits::CanReplicate;
	SubObjectRefHandle = StartReplicatingNetObject(Instance, Traits, NetFactoryId);

	if (!SubObjectRefHandle.IsValid())
	{
		return FNetRefHandle::GetInvalid();
	}

	// Add subobject
	InternalAddSubObject(Params.RootObjectHandle, SubObjectRefHandle, Params.InsertRelativeToSubObjectHandle, Params.InsertionOrder);

	UE_LOG_OBJECTREPLICATIONBRIDGE(Verbose, TEXT("StartReplicatingSubObject Added %s RelativeToSubObjectHandle %s"), *PrintObjectFromNetRefHandle(SubObjectRefHandle), *PrintObjectFromNetRefHandle(Params.InsertRelativeToSubObjectHandle));

	// SubObjects should always poll with owner
	SetPollWithObject(Params.RootObjectHandle, SubObjectRefHandle);

	// Copy pending dormancy from owner
	SetSubObjectDormancyStatus(SubObjectRefHandle, Params.RootObjectHandle);
	
	return SubObjectRefHandle;
}

void UObjectReplicationBridge::SetSubObjectDormancyStatus(FNetRefHandle SubObjectRefHandle, FNetRefHandle OwnerRefHandle)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	const FNetRefHandleManager& LocalNetRefHandleManager = *NetRefHandleManager;
	const FInternalNetRefIndex SubObjectInternalIndex = LocalNetRefHandleManager.GetInternalIndex(SubObjectRefHandle);
	const FInternalNetRefIndex OwnerInternalIndex = LocalNetRefHandleManager.GetInternalIndex(OwnerRefHandle);
	
	FNetBitArrayView DormantObjects = LocalNetRefHandleManager.GetWantToBeDormantInternalIndices();
	DormantObjects.SetBitValue(SubObjectInternalIndex, DormantObjects.GetBit(OwnerInternalIndex));
}

void UObjectReplicationBridge::SetSubObjectNetCondition(FNetRefHandle SubObjectRefHandle, ELifetimeCondition Condition)
{
	using namespace UE::Net::Private;

	// We assume that we can store the condition in an int8;
	static_assert(ELifetimeCondition::COND_Max <= 127);

	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();

	const FInternalNetRefIndex SubObjectInternalIndex = LocalNetRefHandleManager.GetInternalIndex(SubObjectRefHandle);
	if (LocalNetRefHandleManager.SetSubObjectNetCondition(SubObjectInternalIndex, (int8)Condition))
	{
		UE_LOG_OBJECTREPLICATIONBRIDGE(Verbose, TEXT("SetSubObjectNetCondition for SubObject %s Condition %s"), *PrintObjectFromNetRefHandle(SubObjectRefHandle), *UEnum::GetValueAsString<ELifetimeCondition>(Condition));
		MarkNetObjectStateDirty(ReplicationSystem->GetId(), SubObjectInternalIndex);
	}
	else
	{
		UE_LOG_OBJECTREPLICATIONBRIDGE(Warning, TEXT("Failed to Set SubObjectNetCondition for SubObject %s Condition %s"), *PrintObjectFromNetRefHandle(SubObjectRefHandle), *UEnum::GetValueAsString<ELifetimeCondition>(Condition));
	}
}

UE::Net::FNetRefHandle UObjectReplicationBridge::GetRootObjectOfSubObject(FNetRefHandle SubObjectHandle) const
{
	using namespace UE::Net::Private;

	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();

	return LocalNetRefHandleManager.GetRootObjectOfSubObject(SubObjectHandle);
}


void UObjectReplicationBridge::AddDependentObject(FNetRefHandle ParentHandle, FNetRefHandle DependentHandle, UE::Net::EDependentObjectSchedulingHint SchedulingHint)
{
	using namespace UE::Net::Private;

	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();

	if (LocalNetRefHandleManager.AddDependentObject(ParentHandle, DependentHandle, SchedulingHint))
	{
		FReplicationFiltering& Filtering = ReplicationSystemInternal->GetFiltering();
		const FInternalNetRefIndex DependentInternalIndex = LocalNetRefHandleManager.GetInternalIndex(DependentHandle);
		Filtering.NotifyAddedDependentObject(DependentInternalIndex);

		UE_LOG_OBJECTREPLICATIONBRIDGE(Verbose, TEXT("AddDependentObject Added dependent object %s to parent %s"), *PrintObjectFromNetRefHandle(DependentHandle), *PrintObjectFromNetRefHandle(ParentHandle));
	}
	else
	{
		UE_LOG_OBJECTREPLICATIONBRIDGE(Warning , TEXT("AddDependentObject Failed to add dependent object %s to parent %s"), *PrintObjectFromNetRefHandle(DependentHandle), *PrintObjectFromNetRefHandle(ParentHandle));
	}
}

void UObjectReplicationBridge::RemoveDependentObject(FNetRefHandle ParentHandle, FNetRefHandle DependentHandle)
{
	using namespace UE::Net::Private;

	UE_LOG_OBJECTREPLICATIONBRIDGE(Verbose, TEXT("RemoveDependentObject Removing dependent object %s from parent %s"), *DependentHandle.ToString(), *ParentHandle.ToString());

	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();

	// Remove dependent object
	FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();
	LocalNetRefHandleManager.RemoveDependentObject(ParentHandle, DependentHandle);

	const FInternalNetRefIndex DependentInternalIndex = LocalNetRefHandleManager.GetInternalIndex(DependentHandle);
	if (DependentInternalIndex != FNetRefHandleManager::InvalidInternalIndex)
	{
		FReplicationFiltering& Filtering = ReplicationSystemInternal->GetFiltering();
		Filtering.NotifyRemovedDependentObject(DependentInternalIndex);
	}
}

bool UObjectReplicationBridge::WriteNetRefHandleCreationInfo(FReplicationBridgeSerializationContext& Context, FNetRefHandle Handle)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	// If there is a cached header for this handle, use it!
	if (TUniquePtr<const FNetObjectCreationHeader>* CachedHeader = CachedCreationHeaders.Find(Handle))
	{
		const FNetObjectCreationHeader* Header = CachedHeader->Get();
		UNetObjectFactory* Factory = GetNetFactory(Header->GetNetFactoryId());
		UE_LOG(LogIrisBridge, VeryVerbose, TEXT("WriteNetRefHandleCreationInfo (cached) for %s : Factory: %s (id:%u) Header: %s"), *Handle.ToString(), *GetNameSafe(Factory), Header->GetNetFactoryId(), *Header->ToString());
		return Factory->WriteHeader(Handle, Context.SerializationContext, Header);
	}
	
	// Find the ProtocolId
	const UE::Net::FReplicationProtocol* Protocol = GetReplicationSystem()->GetReplicationProtocol(Handle);
	if (!Protocol)
	{
		UE_LOG_OBJECTREPLICATIONBRIDGE(Error, TEXT("WriteNetRefHandleCreationInfo: Cannot write creationinfo for %s, since protocol has been detached"), *Handle.ToString());
		ensureMsgf(Protocol, TEXT("WriteNetRefHandleCreationInfo: Cannot write creationinfo for %s, since protocol has been detached"), *Handle.ToString());
		return false;
	}

	const FInternalNetRefIndex ObjectIndex = NetRefHandleManager->GetInternalIndex(Handle);
	if (ObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		UE_LOG(LogIrisBridge, Error, TEXT("WriteNetRefHandleCreationInfo: Cannot write creationinfo for %s: No internal index assigned to it"), *Handle.ToString());
		return false;
	}

	const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);
	const FNetObjectFactoryId FactoryId = ObjectData.NetFactoryId;

	UNetObjectFactory* Factory = GetNetFactory(FactoryId);

	TUniquePtr<FNetObjectCreationHeader> Header = Factory->CreateHeader(Handle, Protocol->ProtocolIdentifier);

	UE_LOG(LogIrisBridge, VeryVerbose, TEXT("WriteNetRefHandleCreationInfo (new) for %s : Factory: %s (id:%u) Header: %s"), *Handle.ToString(), *GetNameSafe(Factory), FactoryId, *Header->ToString());

	return Factory->WriteHeader(Handle, Context.SerializationContext, Header.Get());
}

bool UObjectReplicationBridge::CacheNetRefHandleCreationInfo(FNetRefHandle Handle)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	const FReplicationProtocol* Protocol = GetReplicationSystem()->GetReplicationProtocol(Handle);	
	if (!Protocol)
	{
		return false;
	}

	const FInternalNetRefIndex ObjectIndex = NetRefHandleManager->GetInternalIndex(Handle);
	if (ObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		UE_LOG(LogIrisBridge, Error, TEXT("CacheNetRefHandleCreationInfo cannot cache creation info for %s: no internal index assigned to it"), *Handle.ToString());
		ensureMsgf(false, TEXT("CacheNetRefHandleCreationInfo cannot cache creation info for %s: no internal index assigned to it"), *Handle.ToString());
		return false;
	}

	const FNetObjectFactoryId FactoryId = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex).NetFactoryId;

	UNetObjectFactory* Factory = GetNetFactory(FactoryId);

	TUniquePtr<FNetObjectCreationHeader> Header = Factory->CreateHeader(Handle, Protocol->ProtocolIdentifier);

	if (!Header.IsValid())
	{
		return false;
	}
	
	CachedCreationHeaders.Add(Handle, MoveTemp(Header));
	return true;
}

void UObjectReplicationBridge::StopReplicatingNetObject(UObject* Instance, EEndReplicationFlags EndReplicationFlags)
{
	const FNetRefHandle RefHandle = GetReplicatedRefHandle(Instance, EGetRefHandleFlags::EvenIfGarbage);
	if (RefHandle.IsValid())
	{
		ensureMsgf(IsValid(Instance), TEXT("Calling EndReplication for Invalid Object: %s."), *PrintObjectFromNetRefHandle(RefHandle));
		StopReplicatingNetRefHandle(RefHandle, EndReplicationFlags);
	}
}

void UObjectReplicationBridge::DetachInstanceFromRemote(FNetRefHandle Handle, EReplicationBridgeDestroyInstanceReason DestroyReason, EReplicationBridgeDestroyInstanceFlags DestroyFlags)
{
	UE_LOG_OBJECTREPLICATIONBRIDGE(Verbose, TEXT("DetachInstanceFromRemote %s DestroyReason: %s DestroyFlags: %u"), *PrintObjectFromNetRefHandle(Handle), ToCStr(LexToString(DestroyReason)), unsigned(DestroyFlags));

	using namespace UE::Net::Private;

	FDestroyInstanceParams DestroyInstanceParams;
	
	DestroyInstanceParams.Instance = GetObjectFromReferenceHandle(Handle);
	DestroyInstanceParams.DestroyReason = DestroyReason;
	DestroyInstanceParams.DestroyFlags = DestroyFlags;

	if (DestroyReason != EReplicationBridgeDestroyInstanceReason::DoNotDestroy)
	{
		const FInternalNetRefIndex ObjectNetIndex = NetRefHandleManager->GetInternalIndex(Handle);
		const FNetRefHandleManager::FReplicatedObjectData& RepObjectData = NetRefHandleManager->GetReplicatedObjectData(ObjectNetIndex);
		DestroyInstanceParams.RootObject = RepObjectData.SubObjectRootIndex != FNetRefHandleManager::InvalidInternalIndex ? NetRefHandleManager->GetReplicatedObjectInstance(RepObjectData.SubObjectRootIndex) : nullptr;
	}
	
	UnregisterInstance(Handle);

	// Destroy instance if requested
	if (DestroyInstanceParams.Instance && DestroyReason != EReplicationBridgeDestroyInstanceReason::DoNotDestroy)
	{
		DestroyInstanceFromRemote(DestroyInstanceParams);
	}

	// $IRIS TODO: Cleanup any pending creation data if we have not yet instantiated the instance.

	Super::DetachInstanceFromRemote(Handle, DestroyReason, DestroyFlags);
}

void UObjectReplicationBridge::DetachInstance(FNetRefHandle RefHandle)
{
	UnregisterInstance(RefHandle);
	CachedCreationHeaders.Remove(RefHandle);
	Super::DetachInstance(RefHandle);
}

void UObjectReplicationBridge::UnregisterInstance(FNetRefHandle RefHandle)
{
	if (RefHandle.IsDynamic())
	{
		UObject* Instance = GetObjectFromReferenceHandle(RefHandle);
		GetObjectReferenceCache()->RemoveReference(RefHandle, Instance);
	}
}

void UObjectReplicationBridge::RegisterRemoteInstance(FNetRefHandle RefHandle, UObject* Instance, const UE::Net::FReplicationProtocol* Protocol, UE::Net::FReplicationInstanceProtocol* InstanceProtocol, uint32 ConnectionId)
{
	// Attach the instance protocol and instance to the handle
	constexpr bool bBindInstanceProtocol = false;
	InternalAttachInstanceToNetRefHandle(RefHandle, bBindInstanceProtocol, InstanceProtocol, Instance, FNetHandle());

	// Dynamic references needs to be promoted to find the instantiated object
	if (RefHandle.IsDynamic())
	{
		GetObjectReferenceCache()->AddRemoteReference(RefHandle, Instance);
	}

	UE_LOG_OBJECTREPLICATIONBRIDGE(Verbose, TEXT("RegisterRemoteInstance %s %s with ProtocolId:0x%" UINT64_x_FMT), *PrintObjectFromNetRefHandle(RefHandle), ToCStr(Instance->GetName()), Protocol->ProtocolIdentifier);
}

FReplicationBridgeCreateNetRefHandleResult UObjectReplicationBridge::CreateNetRefHandleFromRemote(FNetRefHandle RootObjectOfSubObject, FNetRefHandle WantedNetHandle, FReplicationBridgeSerializationContext& Context)
{
	LLM_SCOPE_BYTAG(IrisState);

	using namespace UE::Net;
	using namespace UE::Net::Private;

	FNetBitStreamReader* Reader = Context.SerializationContext.GetBitStreamReader();

	const FNetObjectFactoryId FactoryId = IntCastChecked<FNetObjectFactoryId>(Reader->ReadBits(FNetObjectFactoryRegistry::GetMaxBits()));
	UNetObjectFactory* Factory = GetNetFactory(FactoryId);
	if (Factory == nullptr)
	{
		// There must be a valid factory. We're reading a creation header so a bitstream error seems likely and we should disconnect.
		Context.SerializationContext.SetError(GNetError_InvalidValue);
		return FReplicationBridgeCreateNetRefHandleResult();
	}

	TUniquePtr<FNetObjectCreationHeader> Header = Factory->ReadHeader(WantedNetHandle, Context.SerializationContext);
		
	if (!Header.IsValid() || Context.SerializationContext.HasErrorOrOverflow())
	{
		return FReplicationBridgeCreateNetRefHandleResult();
	}

	const FReplicationProtocolIdentifier ReceivedProtocolId = Header->GetProtocolId();	

	// Currently remote objects can only receive replicated data
	FFragmentRegistrationContext FragmentRegistrationContext(GetReplicationStateDescriptorRegistry(), GetReplicationSystem(), EReplicationFragmentTraits::CanReceive);
	FReplicationProtocolManager* ProtocolManager = GetReplicationProtocolManager();

	if (UE_LOG_ACTIVE(LogIrisBridge, Verbose))
	{
		if (RootObjectOfSubObject.IsValid())
		{
			UE_LOG(LogIrisBridge, Verbose, TEXT("CreateNetRefHandleFromRemote: SubObject: %s of RootObject: %s using header: %s"), *WantedNetHandle.ToString(), *RootObjectOfSubObject.ToString(), ToCStr(Header->ToString()));
		}
		else
		{
			UE_LOG(LogIrisBridge, Verbose, TEXT("CreateNetRefHandleFromRemote: RootObject: %s using header: %s"), *WantedNetHandle.ToString(), ToCStr(Header->ToString()));
		}
	}
	
	// Currently we need to always instantiate remote objects, moving forward we want to make this optional so that can be deferred until it is time to apply received state data.
	// https://jira.it.epicgames.com/browse/UE-127369	

	UObject* InstancePtr = nullptr;
	FReplicationBridgeCreateNetRefHandleResult CreateResult;
	
	// Instantiate the replicated object
	const UNetObjectFactory::FInstantiateContext InstantiateContext(WantedNetHandle, Context.SerializationContext.GetInternalContext()->ResolveContext, RootObjectOfSubObject);
	
	const UNetObjectFactory::FInstantiateResult Result = Factory->InstantiateReplicatedObjectFromHeader(InstantiateContext, Header.Get());

	// Failed ?
	if (!Result.Instance)
	{
		if (UE_LOG_ACTIVE(LogIrisBridge, Warning) && !bSuppressCreateInstanceFailedEnsure)
		{
			if (RootObjectOfSubObject.IsValid())
			{
				UE_LOG(LogIrisBridge, Warning, TEXT("CreateNetRefHandleFromRemote: Failed to instantiate SubObject NetHandle: %s of %s using header: %s"), *WantedNetHandle.ToString(), *PrintObjectFromNetRefHandle(RootObjectOfSubObject), ToCStr(Header->ToString()));
			}
			else
			{
				UE_LOG(LogIrisBridge, Warning, TEXT("CreateNetRefHandleFromRemote: Failed to instantiate RootObject NetHandle: %s using header: %s"), *WantedNetHandle.ToString(), ToCStr(Header->ToString()));
			}
		}
		
		ensureMsgf(bSuppressCreateInstanceFailedEnsure, TEXT("Failed to instantiate Handle: %s"), *WantedNetHandle.ToString());
		return FReplicationBridgeCreateNetRefHandleResult();
	}

	InstancePtr = Result.Instance;
	CreateResult.Flags |= Result.Flags;

	// Register all fragments
	CallRegisterReplicationFragments(InstancePtr, FragmentRegistrationContext, EFragmentRegistrationFlags::None);

	const FReplicationFragments& RegisteredFragments = FFragmentRegistrationContextPrivateAccessor::GetReplicationFragments(FragmentRegistrationContext);

	// We currently identify protocols by local archetype or CDO pointer and verified the protocol id received from server
	// We also should verify the default state that we use for delta compression
	const UObject* ArchetypeOrCDOUsedAsKey = InstancePtr->GetArchetype();

	// Create Protocols
	FReplicationInstanceProtocolPtr InstanceProtocol(ProtocolManager->CreateInstanceProtocol(RegisteredFragments));

	// See if the protocol already is known
	const FReplicationProtocol* ReplicationProtocol = ProtocolManager->GetReplicationProtocol(ReceivedProtocolId, ArchetypeOrCDOUsedAsKey);
	if (!ReplicationProtocol)
	{
		FCreateReplicationProtocolParameters CreateProtocolParams {.ArchetypeOrCDOUsedAsKey = ArchetypeOrCDOUsedAsKey, .bValidateProtocolId = true};
		ReplicationProtocol = ProtocolManager->CreateReplicationProtocol(ReceivedProtocolId, RegisteredFragments, *(InstancePtr->GetClass()->GetName()), CreateProtocolParams);
	}
	else
	{
		constexpr bool bDoNotLogErrors = false; // Don't log errors because it would spam for every individual object of the same class. 
		bool bIsValid = FReplicationProtocolManager::ValidateReplicationProtocol(ReplicationProtocol, RegisteredFragments, bDoNotLogErrors);
		if (!bIsValid)
		{
			ReplicationProtocol = nullptr;
		}
	}

	if (!ReplicationProtocol)
	{
		UE_LOG(LogIris, Error, TEXT("Protocol mismatch prevents binding %s to instanced object %s (CDO: %s)."), *WantedNetHandle.ToString(), *GetNameSafe(InstancePtr), *GetNameSafe(ArchetypeOrCDOUsedAsKey));

		if (UE_LOG_ACTIVE(LogIris, Error))
		{
			UE::Net::Private::ObjectBridgeDebugging::RemoteProtocolMismatchDetected(ArchetypesAlreadyPrinted, ReplicationSystem, Context.ConnectionId, RegisteredFragments, ArchetypeOrCDOUsedAsKey, InstancePtr);
		}

		FIrisDelegates::GetCriticalErrorDetectedDelegate().Broadcast(ReplicationSystem);

		OnProtocolMismatchDetected(WantedNetHandle);
	}
	else
	{
		// Create NetHandle
		FNetRefHandle Handle = InternalCreateNetObjectFromRemote(WantedNetHandle, ReplicationProtocol, FactoryId);
		CreateResult.NetRefHandle = Handle;
		if (Handle.IsValid())
		{
			RegisterRemoteInstance(Handle, InstancePtr, ReplicationProtocol, InstanceProtocol.Get(), Context.ConnectionId);

			// Release instance protocol from the uniquePtr as it is now successfully bound to the handle
			(void)InstanceProtocol.Release();

			Factory->PostInstantiation(UNetObjectFactory::FPostInstantiationContext{ .Instance = InstancePtr, .Header = Header.Get(), .ConnectionId = Context.ConnectionId });
		}
	}

	return CreateResult;
}

void UObjectReplicationBridge::SubObjectCreatedFromReplication(FNetRefHandle Handle)
{
	OnSubObjectCreatedFromReplication(Handle);
}

void UObjectReplicationBridge::PostApplyInitialState(UE::Net::Private::FInternalNetRefIndex InternalObjectIndex)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	const FNetObjectFactoryId FactoryId = NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalObjectIndex).NetFactoryId;
	UNetObjectFactory* Factory = GetNetFactory(FactoryId);

	const UNetObjectFactory::FPostInitContext Context 
	{ 
		.Instance = NetRefHandleManager->GetReplicatedObjectInstance(InternalObjectIndex),
		.Handle = NetRefHandleManager->GetNetRefHandleFromInternalIndex(InternalObjectIndex)
	};
	Factory->PostInit(Context);
}

void UObjectReplicationBridge::PreSendUpdateSingleHandle(FNetRefHandle RefHandle)
{
	ForcePollObject(RefHandle);
}

void UObjectReplicationBridge::OnStartPreSendUpdate()
{
	// During SendUpdate it is not supported to start replication of new root objects.
	bBlockStartRootObjectReplication = true;
}

void UObjectReplicationBridge::PreSendUpdate()
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	FNetRefHandleManager& LocalNetRefHandleManager = GetReplicationSystem()->GetReplicationSystemInternal()->GetNetRefHandleManager();

	{
		const uint32 InitialNumBits = LocalNetRefHandleManager.GetPolledObjectsInternalIndices().GetNumBits();
		
		FNetBitArray InitialPollList(InitialNumBits);
		FNetBitArrayView InitialPollListView(MakeNetBitArrayView(InitialPollList));
		BuildPollList(InitialPollListView);

		PreUpdate(InitialPollListView);
		
		// PreUpdate is allowed to generate new objects, so the netrefhandlemanager bitarrays may have grown.
		// Limit the size of the NetHandleManager list to the pre-grown array.
		FNetBitArrayView CachedPollList(LocalNetRefHandleManager.GetPolledObjectsInternalIndices().GetData(), InitialNumBits);

		CachedPollList.Copy(InitialPollList);
	}

	FinalizeDirtyObjects();

	{
		FNetBitArrayView ObjectsConsideredForPolling = LocalNetRefHandleManager.GetPolledObjectsInternalIndices();
		
		ReconcileNewSubObjects(ObjectsConsideredForPolling);

		PollAndCopy(ObjectsConsideredForPolling);
	}
}

void UObjectReplicationBridge::OnPostSendUpdate()
{
	bBlockStartRootObjectReplication = false;
}

void UObjectReplicationBridge::PruneStaleObjects()
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	IRIS_PROFILER_SCOPE(UObjectReplicationBridge_PruneStaleObjects);

	// Mark all objects with object references as potentially affected by GC
	GarbageCollectionAffectedObjects = ObjectsWithObjectReferences;

	FNetRefHandleManager& LocalNetRefHandleManager = GetReplicationSystem()->GetReplicationSystemInternal()->GetNetRefHandleManager();
	const TNetChunkedArray<TObjectPtr<UObject>>& ReplicatedInstances = LocalNetRefHandleManager.GetReplicatedInstances();

	TArray<FNetRefHandle> StaleObjects;

	// Detect stale references and try to kill/report them
	auto DetectStaleObjectsFunc = [&LocalNetRefHandleManager, &StaleObjects, &ReplicatedInstances](uint32 InternalNetHandleIndex)
	{
		if (!IsValid(ReplicatedInstances[InternalNetHandleIndex]))
		{
			const FNetRefHandleManager::FReplicatedObjectData& ObjectData = LocalNetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalNetHandleIndex);
			if (ObjectData.InstanceProtocol)
			{
				const FReplicationProtocol* Protocol = ObjectData.Protocol;
				const FNetDebugName* DebugName = Protocol ? Protocol->DebugName : nullptr;
				UE_LOG(LogIrisBridge, Warning, TEXT("UObjectReplicationBridge::PruneStaleObjects ObjectInstance replicated as: %s of Type named:%s has been destroyed without notifying the ReplicationSystem %u"), *ObjectData.RefHandle.ToString(), ToCStr(DebugName), ObjectData.RefHandle.GetReplicationSystemId());

				// If the instance protocol is bound, then this is an error and we cannot safely cleanup as unbinding abound instance protocol will modify bound states
				if (ensureMsgf(!EnumHasAnyFlags(ObjectData.InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::IsBound), TEXT("UObjectReplicationBridge::PruneStaleObjects Bound ObjectInstance replicated as: %s has been destroyed without notifying the ReplicationSystem."), *ObjectData.RefHandle.ToString()))
				{
					StaleObjects.Push(ObjectData.RefHandle);
				}
			}
		}
	};

	// Iterate over assigned indices and detect if any of the replicated instances has been garbagecollected (excluding DestroyedStartupObjectInternalIndices) as they never have an instance
	FNetBitArrayView::ForAllSetBits(LocalNetRefHandleManager.GetAssignedInternalIndices(), LocalNetRefHandleManager.GetDestroyedStartupObjectInternalIndices(), FNetBitArrayView::AndNotOp, DetectStaleObjectsFunc);

	// EndReplication/detach stale instances
	for (FNetRefHandle Handle : MakeArrayView(StaleObjects.GetData(), StaleObjects.Num()))
	{
		StopReplicatingNetRefHandle(Handle, EEndReplicationFlags::Destroy);
	}

	// Mark poll override info as dirty
	bHasDirtyClassesInPollPeriodOverrides = ClassHierarchyPollPeriodOverrides.Num() > 0;
}

void UObjectReplicationBridge::SetInstancePreUpdateFunction(FInstancePreUpdateFunction InPreUpdateFunction)
{
	PreUpdateInstanceFunction = InPreUpdateFunction;
}

UObjectReplicationBridge::FInstancePreUpdateFunction UObjectReplicationBridge::GetInstancePreUpdateFunction() const
{
	return PreUpdateInstanceFunction;
}

void UObjectReplicationBridge::SetInstanceGetWorldObjectInfoFunction(FInstanceGetWorldObjectInfoFunction InGetWorldObjectInfoFunction)
{
	GetInstanceWorldObjectInfoFunction = InGetWorldObjectInfoFunction;
}

UObjectReplicationBridge::FInstanceGetWorldObjectInfoFunction UObjectReplicationBridge::GetInstanceGetWorldObjectInfoFunction() const
{
	return GetInstanceWorldObjectInfoFunction;
}

void UObjectReplicationBridge::ForcePollObject(FNetRefHandle Handle)
{
	using namespace UE::Net::Private;

	if (!Handle.IsValid())
	{
		return;
	}

	IRIS_PROFILER_SCOPE(UObjectReplicationBridge_ForcePollAndCopyObject);

	const FNetRefHandleManager& LocalNetRefHandleManager = GetReplicationSystem()->GetReplicationSystemInternal()->GetNetRefHandleManager();

	const FInternalNetRefIndex ObjectIndex = LocalNetRefHandleManager.GetInternalIndex(Handle);
	if (ObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return;
	}

	if (PreUpdateInstanceFunction)
	{
		UObject* Instance = LocalNetRefHandleManager.GetReplicatedInstances()[ObjectIndex];
		if (Instance && LocalNetRefHandleManager.GetObjectsWithPreUpdate().GetBit(ObjectIndex))
		{
			PreUpdateInstanceFunction(MakeArrayView<UObject*>(&Instance, 1U), this);
		}
	}

	FObjectPoller::FInitParams PollerInitParams;
	PollerInitParams.ObjectReplicationBridge = this;
	PollerInitParams.ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	FObjectPoller Poller(PollerInitParams);

	Poller.PollAndCopySingleObject(ObjectIndex);
}

void UObjectReplicationBridge::BuildPollList(UE::Net::FNetBitArrayView ObjectsConsideredForPolling)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	IRIS_PROFILER_SCOPE(UObjectReplicationBridge_BuildPollList);

	// Update every relevant objects from here
	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	FDirtyNetObjectTracker& DirtyNetObjectTracker = ReplicationSystemInternal->GetDirtyNetObjectTracker();

	const FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();
	const FNetBitArrayView RelevantObjects = LocalNetRefHandleManager.GetRelevantObjectsInternalIndices();
	const FNetBitArrayView WantToBeDormantObjects = LocalNetRefHandleManager.GetWantToBeDormantInternalIndices();

	if (bUseFrequencyBasedPolling)
	{
		if (bEnableForceNetUpdate)
		{
			// Find objects ready to be polled and add objects that called ForceNetupdate
			const FNetBitArrayView ForceNetUpdateObjects = DirtyNetObjectTracker.GetForceNetUpdateObjects();
			PollFrequencyLimiter->Update(RelevantObjects, ForceNetUpdateObjects, ObjectsConsideredForPolling);
		}
		else
		{
			// Find objects ready to be polled and add objects that were flagged Dirty.
			const FNetBitArrayView AccumulatedDirtyObjects = DirtyNetObjectTracker.GetAccumulatedDirtyNetObjects();
			PollFrequencyLimiter->Update(RelevantObjects, AccumulatedDirtyObjects, ObjectsConsideredForPolling);
		}
	}
	else
	{
		ObjectsConsideredForPolling.Copy(RelevantObjects);
	}

	// Mask off objects pending dormancy as we do not want to poll/pre-update them unless they are marked for flush
	if (bUseDormancyToFilterPolling)
	{
		IRIS_PROFILER_SCOPE(BuildPollList_Dormancy);

		// Mask off all dormant objects
		ObjectsConsideredForPolling.Combine(WantToBeDormantObjects, FNetBitArrayView::AndNotOp);

		// Force a poll on objects that requested a FlushNet
		FNetBitArrayView DormantObjectsPendingFlushNet = LocalNetRefHandleManager.GetDormantObjectsPendingFlushNet();
		FNetBitArrayView ForceNetUpdateObjects = ReplicationSystemInternal->GetDirtyNetObjectTracker().GetForceNetUpdateObjects();
		
		uint32 FlushNetCount = 0;		
		auto FlushNetDormancyForRelevantObjects = [&](uint32 DormantObjectIndex)
		{
			// Poll the dormant object this frame and treat it as a forcenetupdate in order to also schedule subobjects correctly
			ObjectsConsideredForPolling.SetBit(DormantObjectIndex);
			ForceNetUpdateObjects.SetBit(DormantObjectIndex);
			++FlushNetCount;
		};

		FNetBitArrayView::ForAllSetBits(DormantObjectsPendingFlushNet, RelevantObjects, FNetBitArrayView::AndOp, FlushNetDormancyForRelevantObjects);
		
		// Remove FlushNetDormancy request for the objects that were relevant
		DormantObjectsPendingFlushNet.Combine(RelevantObjects, FNetBitArrayView::AndNotOp);

		UE_NET_TRACE_FRAME_STATSCOUNTER(ReplicationSystem->GetId(), FlushDormancyObjectCount, FlushNetCount, ENetTraceVerbosity::Trace);
	}
	

	/**
	* Make sure to propagate polling for owners to subobjects and vice versa. If an actor is not due to update due to
	* polling frequency it can still be force net update or a dormant object marked for flush and polled for that reason. In order to make sure all recent state updates
	* are replicated atomically this polling propagation is required.
	*/
	{
		auto PropagateSubObjectDirtinessToOwner = [&LocalNetRefHandleManager, &ObjectsConsideredForPolling](uint32 InternalObjectIndex)
		{
			const FNetRefHandleManager::FReplicatedObjectData& ObjectData = LocalNetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalObjectIndex);
			ObjectsConsideredForPolling.SetBit(ObjectData.SubObjectRootIndex);
		};

		auto PropagateOwnerDirtinessToSubObjects = [&LocalNetRefHandleManager, &ObjectsConsideredForPolling](uint32 InternalObjectIndex)
		{
			const FNetRefHandleManager::FReplicatedObjectData& ObjectData = LocalNetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalObjectIndex);
			for (const FInternalNetRefIndex SubObjectInternalIndex : LocalNetRefHandleManager.GetSubObjects(InternalObjectIndex))
			{
				ObjectsConsideredForPolling.SetBit(SubObjectInternalIndex);
			}
		};

		IRIS_PROFILER_SCOPE(BuildPollList_PropagatePolling);

		// Update subobjects' owner first and owners' subobjects second. It's the only way to properly mark all groups of objects in two passes.
		const FNetBitArrayView SubObjects = MakeNetBitArrayView(LocalNetRefHandleManager.GetSubObjectInternalIndices());
		const FNetBitArrayView ForceNetUpdateObjects = DirtyNetObjectTracker.GetForceNetUpdateObjects();

		if (bEnableForceNetUpdate)
		{
			// Make a list of objects which forced an update and are also relevant
			FNetBitArray ForceNetUpdateAndRelevantObjects(RelevantObjects.GetNumBits(), FNetBitArray::NoResetNoValidate);
			FNetBitArrayView ForceNetUpdateAndRelevantObjectsView = MakeNetBitArrayView(ForceNetUpdateAndRelevantObjects, FNetBitArrayView::NoResetNoValidate);
			
			ForceNetUpdateAndRelevantObjectsView.Set(RelevantObjects, FNetBitArray::AndOp, ForceNetUpdateObjects);

			FNetBitArrayView::ForAllSetBits(ForceNetUpdateAndRelevantObjectsView, SubObjects, FNetBitArray::AndOp, PropagateSubObjectDirtinessToOwner);
		}
		else
		{
			// Make the list of objects which are dirty or forced an update, and are also relevant
			FNetBitArray DirtyAndRelevantObjects(RelevantObjects.GetNumBits(), FNetBitArray::NoResetNoValidate);
			FNetBitArrayView DirtyAndRelevantObjectsView = MakeNetBitArrayView(DirtyAndRelevantObjects, FNetBitArrayView::NoResetNoValidate);

			{
				FDirtyObjectsAccessor DirtyObjectsAccessor(ReplicationSystemInternal->GetDirtyNetObjectTracker());
				const FNetBitArrayView DirtyObjectsThisFrame = DirtyObjectsAccessor.GetDirtyNetObjects();

				DirtyAndRelevantObjectsView.Set(DirtyObjectsThisFrame, FNetBitArray::OrOp, ForceNetUpdateObjects);
				DirtyAndRelevantObjectsView.Combine(RelevantObjects, FNetBitArray::AndOp);
			}

			FNetBitArrayView::ForAllSetBits(DirtyAndRelevantObjectsView, SubObjects, FNetBitArray::AndOp, PropagateSubObjectDirtinessToOwner);
		}
			
		// If an object with dependents is about to be polled, force it's dependents to poll at the same time.
		{
			IRIS_PROFILER_SCOPE(BuildPollList_PatchDependentObjects);

			FNetBitArray TempObjectsConsideredForPolling;
			TempObjectsConsideredForPolling.InitAndCopy(ObjectsConsideredForPolling);
			FNetBitArrayView::ForAllSetBits(MakeNetBitArrayView(TempObjectsConsideredForPolling), NetRefHandleManager->GetObjectsWithDependentObjectsInternalIndices(), FNetBitArray::AndOp,
				[&LocalNetRefHandleManager, &ObjectsConsideredForPolling](FInternalNetRefIndex ObjectIndex) 
				{
					LocalNetRefHandleManager.ForAllDependentObjectsRecursive(ObjectIndex, [&ObjectsConsideredForPolling, ObjectIndex](FInternalNetRefIndex DependentObjectIndex)
					{ 
						ObjectsConsideredForPolling.SetBit(DependentObjectIndex); 
					});
				}
			);
		}

		// Currently we must enforce polling SubObjects with owner
		FNetBitArrayView::ForAllSetBits(ObjectsConsideredForPolling, SubObjects, FNetBitArray::AndNotOp, PropagateOwnerDirtinessToSubObjects);
	}
}
	
void UObjectReplicationBridge::PreUpdate(const UE::Net::FNetBitArrayView ObjectsConsideredForPolling)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	IRIS_CSV_PROFILER_SCOPE(Iris, ReplicationBridge_PreUpdate);
	IRIS_PROFILER_SCOPE_VERBOSE(PreUpdatePass);

	if (!PreUpdateInstanceFunction)
	{
		return;
	}

	const FNetRefHandleManager& LocalNetRefHandleManager = GetReplicationSystem()->GetReplicationSystemInternal()->GetNetRefHandleManager();

	const TNetChunkedArray<TObjectPtr<UObject>>& ReplicatedInstances = LocalNetRefHandleManager.GetReplicatedInstances();

	uint32 PreUpdatedObjectCount = 0;

	constexpr uint32 PreUpdateBatchCount = 128U;
	UObject* BatchedObjects[PreUpdateBatchCount];
	uint32 BatchedObjectCount = 0U;

	auto BatchedPreUpdate = [&](FInternalNetRefIndex Objectindex)
	{
		// Flush if needed
		if (BatchedObjectCount == PreUpdateBatchCount)
		{
			PreUpdateInstanceFunction(MakeArrayView<UObject*>(BatchedObjects, BatchedObjectCount), this);
			PreUpdatedObjectCount += BatchedObjectCount;
			BatchedObjectCount = 0U;
		}

		UObject* Instance = ReplicatedInstances[Objectindex];
		BatchedObjects[BatchedObjectCount] = Instance;
		BatchedObjectCount += Instance ? 1U : 0U;
	};

	// Make a copy of the list we'll iterate on since the PreUpdate callbacks can create new objects and realloc the NetRefHandleManager bitarrays
	FNetBitArray ObjectsWithPreUpdate;
	ObjectsWithPreUpdate.InitAndCopy(LocalNetRefHandleManager.GetObjectsWithPreUpdate());

	FNetBitArrayView::ForAllSetBits(ObjectsConsideredForPolling, MakeNetBitArrayView(ObjectsWithPreUpdate), FNetBitArrayView::AndOp, BatchedPreUpdate);

	// Flush last batch
	if (BatchedObjectCount > 0)
	{
		PreUpdateInstanceFunction(MakeArrayView(BatchedObjects, BatchedObjectCount), this);
		PreUpdatedObjectCount += BatchedObjectCount;
	}

	UE_NET_TRACE_FRAME_STATSCOUNTER(ReplicationSystem->GetId(), ReplicationSystem.PreUpdatedObjectCount, PreUpdatedObjectCount, ENetTraceVerbosity::Trace);
}

void UObjectReplicationBridge::PollAndCopy(const UE::Net::FNetBitArrayView ObjectsConsideredForPolling)
{
	using namespace UE::Net::Private;

	IRIS_CSV_PROFILER_SCOPE(Iris, ReplicationBridge_PollAndCopy);

	FObjectPoller::FInitParams PollerInitParams;
	PollerInitParams.ObjectReplicationBridge = this;
	PollerInitParams.ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();

	FObjectPoller Poller(PollerInitParams);
	Poller.PollAndCopyObjects(ObjectsConsideredForPolling);
	
	FObjectPoller::FPreUpdateAndPollStats Stats = Poller.GetPollStats();

	// Report stats
	UE_NET_TRACE_FRAME_STATSCOUNTER(ReplicationSystem->GetId(), ReplicationSystem.PolledObjectCount, Stats.PolledObjectCount, ENetTraceVerbosity::Trace);
	UE_NET_TRACE_FRAME_STATSCOUNTER(ReplicationSystem->GetId(), ReplicationSystem.PolledReferencesObjectCount, Stats.PolledReferencesObjectCount, ENetTraceVerbosity::Trace);
}

void UObjectReplicationBridge::FinalizeDirtyObjects()
{
	using namespace UE::Net::Private;

	IRIS_PROFILER_SCOPE(UObjectReplicationBridge_FinalizeDirtyObjects);

	// Look for new dirty pushmodel objects and then prevent future modifications to it.
	GetReplicationSystem()->GetReplicationSystemInternal()->GetDirtyNetObjectTracker().UpdateAndLockDirtyNetObjects();
}

void UObjectReplicationBridge::ReconcileNewSubObjects(UE::Net::FNetBitArrayView ObjectsConsideredForPolling)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	IRIS_PROFILER_SCOPE(UObjectReplicationBridge_ReconcileNewSubObjects);

	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	const FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();

	const FNetBitArrayView SubObjectList = LocalNetRefHandleManager.GetSubObjectInternalIndicesView();
	FReplicationConnections& Connections = ReplicationSystemInternal->GetConnections();
	FReplicationFiltering& Filtering = ReplicationSystemInternal->GetFiltering();

	auto HandleNewSubObject = [&](FInternalNetRefIndex SubObjectIndex)
	{
		const bool bIsSubObject = SubObjectList.IsBitSet(SubObjectIndex);
		checkf(bIsSubObject, TEXT("Found a root object %s (Index:%u) that was created after the start of PreSendUpdate(). This is not supported"), *GetNameSafe(LocalNetRefHandleManager.GetReplicatedObjectInstance(SubObjectIndex)), SubObjectIndex);
		if (UNLIKELY(!bIsSubObject))
		{
			return;
		}

		const FInternalNetRefIndex RootObjectIndex = LocalNetRefHandleManager.GetRootObjectInternalIndexOfSubObject(SubObjectIndex);
		if (UNLIKELY(RootObjectIndex == FNetRefHandleManager::InvalidInternalIndex))
		{
			ensureMsgf(RootObjectIndex != FNetRefHandleManager::InvalidInternalIndex, TEXT("SubObject %s (Index:%u) had invalid RootObjectIndex"), *GetNameSafe(LocalNetRefHandleManager.GetReplicatedObjectInstance(SubObjectIndex)), SubObjectIndex);
			return;
		}

		// Add the new subobject to the Poll list
		ObjectsConsideredForPolling.SetBit(SubObjectIndex);

		// Iterate over all connections and add the subobject if the root object is relevant to the connection
		auto UpdateConnectionScope = [&Filtering, &Connections, RootObjectIndex, SubObjectIndex](uint32 ConnectionId)
		{
			FReplicationConnection* Conn = Connections.GetConnection(ConnectionId);
			FNetBitArrayView ObjectsInScope = Filtering.GetRelevantObjectsInScope(ConnectionId);

			if (ObjectsInScope.IsBitSet(RootObjectIndex))
			{
				ObjectsInScope.SetBit(SubObjectIndex);
			}
		};

		const FNetBitArray& ValidConnections = Connections.GetValidConnections();
		ValidConnections.ForAllSetBits(UpdateConnectionScope);
	};

	// Find any objects that got added since the start of the PreSendUpdate
	const FNetBitArrayView GlobalScopeList = LocalNetRefHandleManager.GetGlobalScopableInternalIndices();
	const FNetBitArrayView CurrentFrameScopeList = LocalNetRefHandleManager.GetCurrentFrameScopableInternalIndices();
	FNetBitArrayView::ForAllSetBits(GlobalScopeList, CurrentFrameScopeList, FNetBitArrayView::AndNotOp, HandleNewSubObject);
}

void UObjectReplicationBridge::UpdateInstancesWorldLocation()
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	if (!GetInstanceWorldObjectInfoFunction)
	{
		return;
	}

	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	const FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();
	FWorldLocations& WorldLocations = ReplicationSystemInternal->GetWorldLocations();
	const TNetChunkedArray<TObjectPtr<UObject>>& ReplicatedInstances = LocalNetRefHandleManager.GetReplicatedInstances();

	// Retrieve the world location for instances that supports it. Only dirty objects are considered.
	auto UpdateInstanceWorldLocation = [this, &ReplicatedInstances, &LocalNetRefHandleManager, &WorldLocations](uint32 InternalObjectIndex)
	{
		const FNetRefHandleManager::FReplicatedObjectData& ObjectData = LocalNetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalObjectIndex);
		if (ObjectData.InstanceProtocol && EnumHasAnyFlags(ObjectData.InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::NeedsWorldLocationUpdate))
		{
			FWorldLocations::FObjectInfo CachedObjectInfo;
			
			GetInstanceWorldObjectInfoFunction(ObjectData.RefHandle, ReplicatedInstances[InternalObjectIndex], CachedObjectInfo.WorldLocation, CachedObjectInfo.CullDistance);

			// Check the position lies within world boundaries.
			ensureMsgf(CachedObjectInfo.WorldLocation.X >= WorldLocations.GetWorldMinPos().X && CachedObjectInfo.WorldLocation.X <= WorldLocations.GetWorldMaxPos().X && 
					   CachedObjectInfo.WorldLocation.Y >= WorldLocations.GetWorldMinPos().Y && CachedObjectInfo.WorldLocation.Y <= WorldLocations.GetWorldMaxPos().Y, TEXT("Object %s with position %s lies outside configured world boundary."), ToCStr(NetRefHandleManager->PrintObjectFromIndex((InternalObjectIndex))), *CachedObjectInfo.WorldLocation.ToString());
			
			WorldLocations.SetObjectInfo(InternalObjectIndex, CachedObjectInfo);
		}
	};

	// Objects marked as dirty or that have requested frequent world location updates will be updated. Failing to do either when the location has changed will result in replication issues when using spatial filters such as the NetObjectGridFilter.
	FDirtyObjectsAccessor DirtyObjectsAccessor(ReplicationSystemInternal->GetDirtyNetObjectTracker());
	const FNetBitArrayView DirtyObjectsThisFrame = DirtyObjectsAccessor.GetDirtyNetObjects();
	const FNetBitArrayView ObjectsRequiringFrequentUpdates = WorldLocations.GetObjectsRequiringFrequentWorldLocationUpdate();
	FNetBitArrayView::ForAllSetBits(DirtyObjectsThisFrame, ObjectsRequiringFrequentUpdates, FNetBitArrayBase::OrOp, UpdateInstanceWorldLocation);
}

void UObjectReplicationBridge::SetPollWithObject(FNetRefHandle ObjectToPollWithHandle, FNetRefHandle ObjectHandle)
{
	const UE::Net::Private::FInternalNetRefIndex PollWithInternalReplicationIndex = NetRefHandleManager->GetInternalIndex(ObjectToPollWithHandle);
	const UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex = NetRefHandleManager->GetInternalIndex(ObjectHandle);
	PollFrequencyLimiter->SetPollWithObject(PollWithInternalReplicationIndex, InternalReplicationIndex);
}

bool UObjectReplicationBridge::GetObjectWantsToBeDormant(FNetRefHandle Handle) const
{
	using namespace UE::Net::Private;

	const FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	const FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();

	if (const FInternalNetRefIndex InternalObjectIndex = LocalNetRefHandleManager.GetInternalIndex(Handle))
	{
		return LocalNetRefHandleManager.GetWantToBeDormantInternalIndices().GetBit(InternalObjectIndex);
	}

	return false;
}

void UObjectReplicationBridge::SetObjectWantsToBeDormant(FNetRefHandle Handle, bool bWantsToBeDormant)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	FNetRefHandleManager& LocalNetRefHandleManager = GetReplicationSystem()->GetReplicationSystemInternal()->GetNetRefHandleManager();

	const FInternalNetRefIndex InternalObjectIndex = LocalNetRefHandleManager.GetInternalIndex(Handle);
	if (InternalObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return;
	}

	UE_LOG_OBJECTREPLICATIONBRIDGE(Verbose, TEXT("SetObjectWantsToBeDormant: %s wants to be %s "), *LocalNetRefHandleManager.PrintObjectFromIndex(InternalObjectIndex), bWantsToBeDormant ? TEXT("Dormant"):TEXT("Awake"));

	// Only RootObjects can trigger dormancy changes
	ensureMsgf(!LocalNetRefHandleManager.GetSubObjectInternalIndices().IsBitSet(InternalObjectIndex), TEXT("Only root objects can become dormant: %s "), *LocalNetRefHandleManager.PrintObjectFromIndex(InternalObjectIndex));
	
	FNetBitArrayView WantToBeDormantObjects = LocalNetRefHandleManager.GetWantToBeDormantInternalIndices();

	// Update pending dormancy status
	WantToBeDormantObjects.SetBitValue(InternalObjectIndex, bWantsToBeDormant);

	// If we want to be dormant we want to make sure we poll the object immediately
	LocalNetRefHandleManager.GetDormantObjectsPendingFlushNet().SetBitValue(InternalObjectIndex, bWantsToBeDormant);

	// Since we use this as a mask when updating objects we must include subobjects as well
	// Subobjects added later will copy status from owner when they are added
	for (const FInternalNetRefIndex SubObjectInternalIndex : LocalNetRefHandleManager.GetSubObjects(InternalObjectIndex))
	{
		WantToBeDormantObjects.SetBitValue(SubObjectInternalIndex, bWantsToBeDormant);
	}

	// Request frequent world location updates for non-dormant spatially filtered objects.
	OptionallySetObjectRequiresFrequentWorldLocationUpdate(Handle, !bWantsToBeDormant);
}

void UObjectReplicationBridge::NetFlushDormantObject(FNetRefHandle Handle)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	FNetRefHandleManager& LocalNetRefHandleManager = GetReplicationSystem()->GetReplicationSystemInternal()->GetNetRefHandleManager();

	const FInternalNetRefIndex InternalObjectIndex = LocalNetRefHandleManager.GetInternalIndex(Handle);
	if (InternalObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return;
	}

	UE_LOG_OBJECTREPLICATIONBRIDGE(Verbose, TEXT("NetFlushDormantObject: %s"), *LocalNetRefHandleManager.PrintObjectFromIndex(InternalObjectIndex));

	LocalNetRefHandleManager.GetDormantObjectsPendingFlushNet().SetBit(InternalObjectIndex);

	// Mark the object dirty in order to trigger an update of its WorldLocation and to accumulate dirty flags for when he comes back out of dormancy.
	ReplicationSystem->MarkDirty(Handle);
}

void UObjectReplicationBridge::SetNetPushIdOnInstance(UE::Net::FReplicationInstanceProtocol* InstanceProtocol, FNetHandle NetHandle)
{
#if WITH_PUSH_MODEL
	using namespace UE::Net;

	// Set push ID only if any state supports it. If no state supports it then we might crash if setting the ID.
	if (EnumHasAnyFlags(InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::HasPartialPushBasedDirtiness | EReplicationInstanceProtocolTraits::HasFullPushBasedDirtiness))
	{
		const Private::FNetPushObjectHandle PushHandle(NetHandle);
		TArrayView<const FReplicationFragment* const> Fragments(InstanceProtocol->Fragments, InstanceProtocol->FragmentCount);
		SetNetPushIdOnFragments(Fragments, PushHandle);
	}
#endif
}

bool UObjectReplicationBridge::GetClassPollFrequency(const UClass* Class, float& OutPollFrequency) const
{
	if (!(bAllowPollPeriodOverrides & bHasPollOverrides))
	{
		return false;
	}

	const FName ClassName = Class->GetFName();
	if (const FPollInfo* PollInfo = ClassesWithPollPeriodOverride.Find(ClassName))
	{
		OutPollFrequency = PollInfo->PollFrequency;
		return true;
	}

	if (ClassesWithoutPollPeriodOverride.Find(ClassName))
	{
		return false;
	}

	bool bFoundOverride = false;
	const UClass* SuperclassWithPollInfo = nullptr;
	FPollInfo SuperclassPollInfo;
	for (const auto& ClassNameAndPollInfo : ClassHierarchyPollPeriodOverrides)
	{
		const UClass* ClassWithPollInfo = ClassNameAndPollInfo.Value.Class.Get();
		if (ClassWithPollInfo == nullptr)
		{
			continue;
		}

		if (Class->IsChildOf(ClassWithPollInfo))
		{
			// If we've already found a superclass with a config, see which one is closer in the hierarchy.
			if (SuperclassWithPollInfo != nullptr)
			{
				if (ClassWithPollInfo->IsChildOf(SuperclassWithPollInfo))
				{
					bFoundOverride = true;
					SuperclassWithPollInfo = ClassWithPollInfo;
					SuperclassPollInfo = ClassNameAndPollInfo.Value;
				}
			}
			else
			{
				bFoundOverride = true;
				SuperclassWithPollInfo = ClassWithPollInfo;
				SuperclassPollInfo = ClassNameAndPollInfo.Value;
			}
		}
	}

	if (bFoundOverride)
	{
		OutPollFrequency = SuperclassPollInfo.PollFrequency;
		return true;
	}

	return false;
}

bool UObjectReplicationBridge::FindOrCachePollFrequency(const UClass* Class, float& OutPollFrequency)
{
	if (!(bAllowPollPeriodOverrides & bHasPollOverrides))
	{
		return false;
	}

	const FName ClassName = Class->GetFName();
	if (const FPollInfo* PollInfo = ClassesWithPollPeriodOverride.Find(ClassName))
	{
		OutPollFrequency = PollInfo->PollFrequency;
		return true;
	}

	if (ClassesWithoutPollPeriodOverride.Find(ClassName))
	{
		return false;
	}

	/**
	 * Only if there are poll period overrides for class hierarchies does it make sense to add
	 * more class names to the exact match containers.
	 */
	if (ClassHierarchyPollPeriodOverrides.Num() == 0)
	{
		return false;
	}

	// We have not encountered this class before. Let's add it to the appropriate container for faster lookup next time.
	if (bHasDirtyClassesInPollPeriodOverrides)
	{
		FindClassesInPollPeriodOverrides();
	}

	const UClass* SuperclassWithPollInfo = nullptr;
	FPollInfo SuperclassPollInfo;
	for (const auto& ClassNameAndPollInfo : ClassHierarchyPollPeriodOverrides)
	{
		const UClass* ClassWithPollInfo = ClassNameAndPollInfo.Value.Class.Get();
		if (ClassWithPollInfo == nullptr)
		{
			continue;
		}

		if (Class->IsChildOf(ClassWithPollInfo))
		{
			// If we've already found a superclass with a config, see which one is closer in the hierarchy.
			if (SuperclassWithPollInfo != nullptr)
			{
				if (ClassWithPollInfo->IsChildOf(SuperclassWithPollInfo))
				{
					SuperclassWithPollInfo = ClassWithPollInfo;
					SuperclassPollInfo = ClassNameAndPollInfo.Value;
				}
			}
			else
			{
				SuperclassWithPollInfo = ClassWithPollInfo;
				SuperclassPollInfo = ClassNameAndPollInfo.Value;
			}
		}
	}

	if (SuperclassWithPollInfo != nullptr)
	{
		/*
		 * Reset class weak pointer as it's not used for exact class matches
		 * and we have no interest in maintaining a valid weak pointer for this case.
		 */
		SuperclassPollInfo.Class.Reset();

		// Add the class hiearchy to our set of classes with overrides.
		for (const UClass* ClassToAdd = Class; ClassToAdd != nullptr; ClassToAdd = ClassToAdd->GetSuperClass())
		{
			const FName ClassToAddName = ClassToAdd->GetFName();
			ClassesWithPollPeriodOverride.FindOrAdd(ClassToAddName, SuperclassPollInfo);
		}

		OutPollFrequency = SuperclassPollInfo.PollFrequency;
		return true;
	}
	else
	{
		// Add the class hierarchy to our set of classes without overrides.
		for (const UClass* ClassToAdd = Class; ClassToAdd != nullptr; ClassToAdd = ClassToAdd->GetSuperClass())
		{
			// We avoid adding classes that are in the exact match container, even though it's not strictly necessary.
			// It makes it easier to reason about things as a class will only be found in exactly one exact cast container.
			const FName ClassToAddName = ClassToAdd->GetFName();
			if (ClassesWithPollPeriodOverride.Contains(ClassToAddName))
			{
				continue;
			}

			ClassesWithoutPollPeriodOverride.Add(ClassToAddName);
		}
	}

	return false;
}

FName UObjectReplicationBridge::GetConfigClassPathName(const UClass* Class)
{
	if (FName* CachedPathName = ConfigClassPathNameCache.Find(Class))
	{
		return *CachedPathName;
	}

	const FName ClassPathName(Class->GetPathName());

	ConfigClassPathNameCache.Add(Class, ClassPathName);

	return ClassPathName;
}

bool UObjectReplicationBridge::ShouldClassBeDeltaCompressed(const UClass* Class)
{
	using namespace UE::Net;

	if (ClassesWithDeltaCompression.Num() > 0)
	{
		for (; Class != nullptr; Class = Class->GetSuperClass())
		{
			if (bool* bShouldBeDeltaCompressed = ClassesWithDeltaCompression.Find(GetConfigClassPathName(Class)))
			{
				return *bShouldBeDeltaCompressed;
			}
		}
	}

	return false;
}

bool UObjectReplicationBridge::IsClassCritical(const UClass* Class)
{
	const UObjectReplicationBridgeConfig* BridgeConfig = UObjectReplicationBridgeConfig::GetConfig();
	if (BridgeConfig->AreAllClassesCritical())
	{
		return true;
	}

	if (ClassesFlaggedCritical.Num() > 0)
	{
		for (; Class != nullptr; Class = Class->GetSuperClass())
		{
			if (bool* bIsClassCritical = ClassesFlaggedCritical.Find(GetConfigClassPathName(Class)))
			{
				return *bIsClassCritical;
			}
		}
	}

	return false;
}

FString UObjectReplicationBridge::PrintConnectionInfo(uint32 ConnectionId) const
{
	return FString::Printf(TEXT("ConnectionId:%u"), ConnectionId);
}

void UObjectReplicationBridge::OptionallySetObjectRequiresFrequentWorldLocationUpdate(FNetRefHandle RefHandle, bool bDesiresFrequentWorldLocationUpdate)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	const FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();
	const FInternalNetRefIndex InternalObjectIndex = LocalNetRefHandleManager.GetInternalIndex(RefHandle);
	FWorldLocations& WorldLocations = ReplicationSystemInternal->GetWorldLocations();
	// When this function is called due to dormancy changes we don't know that the object requires world location updates at all. Checking if it has world location info is how we find that out.
	if (WorldLocations.HasInfoForObject(InternalObjectIndex))
	{
		const FReplicationFiltering& Filtering = ReplicationSystemInternal->GetFiltering();
		const bool bRequireFrequentWorldLocationUpdates = bDesiresFrequentWorldLocationUpdate && Filtering.IsUsingSpatialFilter(InternalObjectIndex);
		WorldLocations.SetObjectRequiresFrequentWorldLocationUpdate(InternalObjectIndex, bRequireFrequentWorldLocationUpdates);
	}
}

int32 UObjectReplicationBridge::GetTypeStatsIndex(const UClass* Class)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;
	
	if (ClassesWithTypeStats.Num() > 0)
	{
		FNetTypeStats& TypeStats = GetReplicationSystem()->GetReplicationSystemInternal()->GetNetTypeStats();
		for (; Class != nullptr; Class = Class->GetSuperClass())
		{
			if (FName* TypeStatsName = ClassesWithTypeStats.Find(GetConfigClassPathName(Class)))
			{
				return TypeStats.GetOrCreateTypeStats(*TypeStatsName);
			}
		}
	}

	return FNetTypeStats::DefaultTypeStatsIndex;
}

void UObjectReplicationBridge::SetClassTypeStatsConfig(FName ClassPathName, FName TypeStatsName)
{
	if (ClassPathName.IsNone())
	{
		return;
	}

	ClassesWithTypeStats.Add(ClassPathName, TypeStatsName);

}

void UObjectReplicationBridge::SetClassTypeStatsConfig(const FString& ClassPathName, const FString& TypeStatsName)
{
	SetClassTypeStatsConfig(FName(ClassPathName), FName(TypeStatsName));
}

void UObjectReplicationBridge::SetClassDynamicFilterConfig(FName ClassPathName, const UE::Net::FNetObjectFilterHandle FilterHandle, FName FilterProfile)
{
	if (ClassPathName.IsNone())
	{
		return;
	}

	if (UE_LOG_ACTIVE(LogIrisFilterConfig, Log))
	{
		if (const FClassFilterInfo* OldFilterInfo = ClassesWithDynamicFilter.Find(ClassPathName))
		{
			if (OldFilterInfo->FilterHandle != FilterHandle)
			{
				UE_LOG(LogIrisFilterConfig, Log, TEXT("SetClassDynamicFilterConfig assigned %s to use filter %s (Profile %s). Previously using filter %s."),
					*ClassPathName.ToString(), *(ReplicationSystem->GetFilterName(FilterHandle).ToString()), *FilterProfile.ToString(), *(ReplicationSystem->GetFilterName(OldFilterInfo->FilterHandle).ToString()));
			}
			else
			{
				UE_LOG(LogIrisFilterConfig, Log, TEXT("SetClassDynamicFilterConfig assigned %s to use filter %s but the class was already assigned to this filter."), *ClassPathName.ToString(), *(ReplicationSystem->GetFilterName(FilterHandle).ToString()));
			}
		}
		else
		{
			UE_LOG(LogIrisFilterConfig, Log, TEXT("SetClassDynamicFilterConfig assigned %s to use filter %s (Profile %s)."), *ClassPathName.ToString(), *(ReplicationSystem->GetFilterName(FilterHandle).ToString()), *FilterProfile.ToString());
		}
	}

	FClassFilterInfo FilterInfo;
	FilterInfo.FilterHandle = FilterHandle;
	FilterInfo.FilterProfile = FilterProfile;
	FilterInfo.bForceEnable = false;
	ClassesWithDynamicFilter.Add(ClassPathName, FilterInfo);
}

void UObjectReplicationBridge::SetClassDynamicFilterConfig(FName ClassPathName, FName FilterName, FName FilterProfile)
{
	using namespace UE::Net;

	if (ClassPathName.IsNone())
	{
		return;
	}

	if (FilterName != NAME_None)
	{
		FNetObjectFilterHandle FilterHandle = GetReplicationSystem()->GetFilterHandle(FilterName);

		if (ensureMsgf(FilterHandle != InvalidNetObjectFilterHandle, TEXT("SetClassDynamicFilterConfig for %s received invalid filter named %s"), *ClassPathName.ToString(), *FilterName.ToString()))
		{
			SetClassDynamicFilterConfig(ClassPathName, FilterHandle, FilterProfile);
		}
	}
	else
	{
		// Reset the filter so the class does not get assigned a dynamic filter anymore.
		SetClassDynamicFilterConfig(ClassPathName, InvalidNetObjectFilterHandle, FilterProfile);
	}
}

UE::Net::FNetObjectFilterHandle UObjectReplicationBridge::GetDynamicFilter(const UClass* Class, bool bRequireForceEnabled, FName& OutFilterProfile)
{
	using namespace UE::Net;

	if (ClassesWithDynamicFilter.IsEmpty())
	{
		/**
		 * For the cases when there are no configured filter mappings we just check whether to use a spatial filter or not.
		 * We don't add anything to the filter mapping.
		 */
		const FNetObjectFilterHandle FilterHandle = ShouldUseDefaultSpatialFilterFunction(Class) ? DefaultSpatialFilterHandle : InvalidNetObjectFilterHandle;
		return FilterHandle;
	}

	const FName ClassName = GetConfigClassPathName(Class);

	// Try exact match first.
	if (FClassFilterInfo* FilterInfoPtr = ClassesWithDynamicFilter.Find(ClassName))
	{
		const bool bUseFilter = !bRequireForceEnabled || FilterInfoPtr->bForceEnable;
		if (bUseFilter)
		{
			OutFilterProfile = FilterInfoPtr->FilterProfile;
			return FilterInfoPtr->FilterHandle;
		}
		else
		{
			return InvalidNetObjectFilterHandle;
		}
	}

	/**
	* Try to find superclass. If we find it and the classes are considered equal we copy the filter setting.
	* If it's not equal we check whether it can be spatialized or not and use the result of that.
	* In all cases we add the result to the mapping for faster lookup next time.
	*/
	for (const UClass* SuperClass = Class->GetSuperClass(); SuperClass != nullptr; SuperClass = SuperClass->GetSuperClass())
	{
		const FName SuperClassName = GetConfigClassPathName(SuperClass);

		// Try to get exact match first.
		if (FClassFilterInfo* FilterInfoPtr = ClassesWithDynamicFilter.Find(SuperClassName))
		{
			if (ShouldSubclassUseSameFilterFunction(SuperClass, Class))
			{
				FClassFilterInfo FilterInfo = *FilterInfoPtr;
				ClassesWithDynamicFilter.Add(ClassName, FilterInfo);

				const bool bUseFilter = !bRequireForceEnabled || FilterInfo.bForceEnable;

				if (bUseFilter)
				{
					OutFilterProfile = FilterInfo.FilterProfile;
					return FilterInfo.FilterHandle;
				}
				else
				{
					return InvalidNetObjectFilterHandle;
				}
					
			}

			// Here's a good place to put a line of code and set a breakpoint to debug inheritance issues.

			break;
		}
	}

	// Either super class wasn't found or it wasn't considered equal. Let's add a new filter mapping.
	FClassFilterInfo FilterInfo;
	FilterInfo.FilterHandle = ShouldUseDefaultSpatialFilterFunction(Class) ? DefaultSpatialFilterHandle : InvalidNetObjectFilterHandle;
	FilterInfo.bForceEnable = false;
	ClassesWithDynamicFilter.Add(ClassName, FilterInfo);
	return FilterInfo.FilterHandle;
}

UE::Net::FNetObjectPrioritizerHandle UObjectReplicationBridge::GetPrioritizer(const UClass* Class, bool bRequireForceEnabled)
{
	using namespace UE::Net;

	if (ClassesWithPrioritizer.Num() > 0)
	{
		const FName ClassName = GetConfigClassPathName(Class);

		// Try exact match first.
		if (const FClassPrioritizerInfo* PrioritizerInfo = ClassesWithPrioritizer.Find(ClassName))
		{
			const bool bUsePrioritizer = !bRequireForceEnabled || PrioritizerInfo->bForceEnable;
			return bUsePrioritizer ? PrioritizerInfo->PrioritizerHandle : InvalidNetObjectPrioritizerHandle;
		}

		// Try to find superclass with prioritizer config. If we find it we copy the config and add the result to the mapping for faster lookup next time.
		for (const UClass* SuperClass = Class->GetSuperClass(); SuperClass != nullptr; SuperClass = SuperClass->GetSuperClass())
		{
			const FName SuperClassName = GetConfigClassPathName(SuperClass);

			if (const FClassPrioritizerInfo* PrioritizerInfoPtr = ClassesWithPrioritizer.Find(SuperClassName))
			{
				// Copy info to this class
				FClassPrioritizerInfo PrioritizerInfo = *PrioritizerInfoPtr;
				ClassesWithPrioritizer.Add(ClassName, PrioritizerInfo);

				const bool bUsePrioritizer = !bRequireForceEnabled || PrioritizerInfo.bForceEnable;
				return bUsePrioritizer ? PrioritizerInfo.PrioritizerHandle : InvalidNetObjectPrioritizerHandle;
			}
		}
	}

	// No prioritizer has been configured for this class.
	return InvalidNetObjectPrioritizerHandle;
}

void UObjectReplicationBridge::LoadConfig()
{
	// Clear everything related to the config.
	bHasPollOverrides = false;
	bHasDirtyClassesInPollPeriodOverrides = false;
	ClassHierarchyPollPeriodOverrides.Empty();
	ClassesWithPollPeriodOverride.Empty();
	ClassesWithoutPollPeriodOverride.Empty();
	ClassesWithDynamicFilter.Empty();
	ClassesWithPrioritizer.Empty();
	ClassesWithDeltaCompression.Empty();
	ClassesFlaggedCritical.Empty();

	// Reset PathNameCache
	ConfigClassPathNameCache.Empty();

	const UObjectReplicationBridgeConfig* BridgeConfig = UObjectReplicationBridgeConfig::GetConfig();

	// Load poll configs
	
	// These classes are forbidden to override due to being too generic and could cause memory and performance issues.
	// If there's need for a global poll period override it should be implemented separately and not via class overrides.
	const FName ForbiddenNames[] = {NAME_Object, NAME_Actor};
	const TConstArrayView<FName> ForbiddenNamesArray = MakeArrayView(ForbiddenNames);

	for (const FObjectReplicationBridgePollConfig& PollOverride : BridgeConfig->GetPollConfigs())
	{
		if (!ensure(!ForbiddenNamesArray.Contains(PollOverride.ClassName)))
		{
			continue;
		}

		bHasPollOverrides = true;

		FPollInfo PollInfo;
		PollInfo.PollFrequency = FPlatformMath::Max(PollOverride.PollFrequency, 0.0f);
		if (PollOverride.bIncludeSubclasses)
		{
			bHasDirtyClassesInPollPeriodOverrides = true;
			ClassHierarchyPollPeriodOverrides.Add(PollOverride.ClassName, PollInfo);
		}
		else
		{
			ClassesWithPollPeriodOverride.Add(PollOverride.ClassName, PollInfo);
		}
	}

	if (bHasDirtyClassesInPollPeriodOverrides)
	{
		FindClassesInPollPeriodOverrides();
	}

	// Filter mappings.
	DefaultSpatialFilterHandle = ReplicationSystem->GetFilterHandle(BridgeConfig->GetDefaultSpatialFilterName());

	{
		for (const FObjectReplicationBridgeFilterConfig& FilterConfig : BridgeConfig->GetFilterConfigs())
		{
			FClassFilterInfo FilterInfo;
			FilterInfo.FilterHandle = ReplicationSystem->GetFilterHandle(FilterConfig.DynamicFilterName);
			FilterInfo.FilterProfile = FilterConfig.FilterProfile;
			FilterInfo.bForceEnable = FilterConfig.bForceEnableOnAllInstances;
			ClassesWithDynamicFilter.Add(FilterConfig.ClassName, FilterInfo);
		}
	}

	// Prioritizer mappings
	{
		for (const FObjectReplicationBridgePrioritizerConfig& PrioritizerConfig : BridgeConfig->GetPrioritizerConfigs())
		{
			FClassPrioritizerInfo PrioInfo;
			PrioInfo.PrioritizerHandle = ReplicationSystem->GetPrioritizerHandle(PrioritizerConfig.PrioritizerName);
			PrioInfo.bForceEnable = PrioritizerConfig.bForceEnableOnAllInstances;
			ClassesWithPrioritizer.Add(PrioritizerConfig.ClassName, PrioInfo);
		}
	}

	// Load delta compression settings
	{
		for (const FObjectReplicationBridgeDeltaCompressionConfig& DCConfig : BridgeConfig->GetDeltaCompressionConfigs())
		{
			if (!ensure(!ForbiddenNamesArray.Contains(DCConfig.ClassName)))
			{
				continue;
			}

			ClassesWithDeltaCompression.Add(DCConfig.ClassName, DCConfig.bEnableDeltaCompression);
		}
	}

	// Critical classes
	if (!BridgeConfig->AreAllClassesCritical())
	{
		for (const FObjectReplicatedBridgeCriticalClassConfig& CriticalClassConfig : BridgeConfig->GetCriticalClassConfigs())
		{
			if (!ensure(!ForbiddenNamesArray.Contains(CriticalClassConfig.ClassName)))
			{
				continue;
			}

			ClassesFlaggedCritical.Add(CriticalClassConfig.ClassName, CriticalClassConfig.bDisconnectOnProtocolMismatch);
		}
	}
	// Load TypeStats settings
	{
		for (const FObjectReplicationBridgeTypeStatsConfig& TypeStatsConfig : BridgeConfig->GetTypeStatsConfigs())
		{
			// Skip all non shipping TypeStats unless CVar is set
			if (!bUseVerboseIrisCsvStats && !TypeStatsConfig.bIncludeInMinimalCSVStats)
			{
				continue;
			}
			
			ClassesWithTypeStats.Add(TypeStatsConfig.ClassName, TypeStatsConfig.TypeStatsName);
		}
	}
}

void UObjectReplicationBridge::FindClassesInPollPeriodOverrides()
{
	bool bFailedToFindClass = false;
	for (auto& ClassNameAndPollInfo : ClassHierarchyPollPeriodOverrides)
	{
		FPollInfo& PollInfo = ClassNameAndPollInfo.Value;
		if (!PollInfo.Class.IsValid())
		{
			constexpr UObject* ClassOuter = nullptr;
			constexpr bool bExactClass = true;
			const UClass* Class = CastChecked<UClass>(StaticFindObject(UClass::StaticClass(), ClassOuter, ToCStr(ClassNameAndPollInfo.Key.ToString()), bExactClass));
			bFailedToFindClass |= (Class == nullptr);
			PollInfo.Class = Class;
		}
	}

	bHasDirtyClassesInPollPeriodOverrides = bFailedToFindClass;
}

void UObjectReplicationBridge::SetShouldUseDefaultSpatialFilterFunction(TFunction<bool(const UClass*)> InShouldUseDefaultSpatialFilterFunction)
{
	if (!ensureMsgf((bool)InShouldUseDefaultSpatialFilterFunction, TEXT("%s"), TEXT("A valid function must be provided for SetShouldUseDefaultSpatialFilterFunction.")))
	{
		return;
	}

	ShouldUseDefaultSpatialFilterFunction = InShouldUseDefaultSpatialFilterFunction;
}

void UObjectReplicationBridge::SetShouldSubclassUseSameFilterFunction(TFunction<bool(const UClass* Class, const UClass* Subclass)> InShouldSubclassUseSameFilterFunction)
{
	if (!ensureMsgf((bool)InShouldSubclassUseSameFilterFunction, TEXT("%s"), TEXT("A valid function must be provided for SetShouldSubclassUseSameFilterFunction.")))
	{
		return;
	}

	ShouldSubclassUseSameFilterFunction = InShouldSubclassUseSameFilterFunction;
}

void UObjectReplicationBridge::InitConditionalPropertyDelegates()
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	// Hookup delegate for when a property custom condition is changed
	OnCustomConditionChangedHandle = UE::Net::Private::FPropertyConditionDelegates::GetOnPropertyCustomConditionChangedDelegate().AddLambda([this](const UObject* Owner, uint16 RepIndex, bool bEnable)
	{
		const FNetRefHandle RefHandle = this->GetReplicatedRefHandle(Owner);
		if (RefHandle.IsValid())
		{
			FReplicationSystemInternal* ReplicationSystemInternal = this->GetReplicationSystem()->GetReplicationSystemInternal();
			const FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();
			FReplicationConditionals& Conditionals = ReplicationSystemInternal->GetConditionals();

			Conditionals.SetPropertyCustomCondition(LocalNetRefHandleManager.GetInternalIndex(RefHandle), Owner, RepIndex, bEnable);
		}
	});

	// Hookup delegate for when a property dynamic condition is changed
	OnDynamicConditionChangedHandle = UE::Net::Private::FPropertyConditionDelegates::GetOnPropertyDynamicConditionChangedDelegate().AddLambda([this](const UObject* Owner, uint16 RepIndex, ELifetimeCondition Condition)
	{
		const FNetRefHandle RefHandle = this->GetReplicatedRefHandle(Owner);
		if (RefHandle.IsValid())
		{
			FReplicationSystemInternal* ReplicationSystemInternal = this->GetReplicationSystem()->GetReplicationSystemInternal();
			const FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();
			FReplicationConditionals& Conditionals = ReplicationSystemInternal->GetConditionals();

			Conditionals.SetPropertyDynamicCondition(LocalNetRefHandleManager.GetInternalIndex(RefHandle), Owner, RepIndex, Condition);
		}
	});
}

uint8 UObjectReplicationBridge::ConvertPollFrequencyIntoFrames(float PollFrequency) const
{
	if (PollFrequency <= 0.0f)
	{
		return 0U;
	}

	uint32 FramesBetweenUpdatesForObject = static_cast<uint32>(MaxTickRate / FPlatformMath::Max(0.001f, PollFrequency));
	if (FramesBetweenUpdatesForObject > 0)
	{
		FramesBetweenUpdatesForObject--;
	}

	return static_cast<uint8>(FMath::Clamp<uint32>(FramesBetweenUpdatesForObject, 0U, UE::Net::Private::FObjectPollFrequencyLimiter::GetMaxPollingFrames()));
}

float UObjectReplicationBridge::GetPollFrequencyOfRootObject(const UObject* ReplicatedObject) const
{
	float PollFrequency = 0.0f;
	GetClassPollFrequency(ReplicatedObject->GetClass(), PollFrequency);

	return PollFrequency;
}

void UObjectReplicationBridge::ReinitPollFrequency()
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	const FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();

	auto UpdatePollFrequency = [this, &LocalNetRefHandleManager](uint32 RootObjectIndex)
	{
		if (UObject* RootObjectInstance = LocalNetRefHandleManager.GetReplicatedObjectInstance(RootObjectIndex))
		{
			const float PollFrequency = GetPollFrequencyOfRootObject(RootObjectInstance);
			const uint8 PollFramePeriod = ConvertPollFrequencyIntoFrames(PollFrequency);

			PollFrequencyLimiter->SetPollFramePeriod(RootObjectIndex, PollFramePeriod);

			// Make sure the subobjects are polled the same frame as the root object.
			for (const FInternalNetRefIndex SubObjectIndex : LocalNetRefHandleManager.GetSubObjects(RootObjectIndex))
			{
				PollFrequencyLimiter->SetPollWithObject(RootObjectIndex, SubObjectIndex);
			}
		}
	};

	const FNetBitArrayView RootObjects = LocalNetRefHandleManager.GetGlobalScopableInternalIndices();
	const FNetBitArrayView SubObjects = MakeNetBitArrayView(LocalNetRefHandleManager.GetSubObjectInternalIndices());

	FNetBitArrayView::ForAllSetBits(RootObjects, SubObjects, FNetBitArrayView::AndNotOp, UpdatePollFrequency);
}

void UObjectReplicationBridge::SetPollFrequency(FNetRefHandle RefHandle, float PollFrequency)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	FReplicationSystemInternal* ReplicationSystemInternal = GetReplicationSystem()->GetReplicationSystemInternal();
	const FNetRefHandleManager& LocalNetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();

	FInternalNetRefIndex RootObjectIndex = LocalNetRefHandleManager.GetInternalIndex(RefHandle);
	if (RootObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return;
	}

	const uint8 PollFramePeriod = ConvertPollFrequencyIntoFrames(PollFrequency);
	PollFrequencyLimiter->SetPollFramePeriod(RootObjectIndex, PollFramePeriod);

	// Make sure the subobjects are polled the same frame as the root object.
	for (const FInternalNetRefIndex SubObjectIndex : LocalNetRefHandleManager.GetSubObjects(RootObjectIndex))
	{
		PollFrequencyLimiter->SetPollWithObject(RootObjectIndex, SubObjectIndex);
	}
}

void UObjectReplicationBridge::OnProtocolMismatchReported(FNetRefHandle RefHandle, uint32 ConnectionId)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	// Ensure at the end so the log contains all the relevant information
	ON_SCOPE_EXIT
	{
		ensureMsgf(false, TEXT("Protocol mismatch detected from %s. Compare the CDO state in the server and client logs to find the source of the issue."), *PrintConnectionInfo(ConnectionId));
	};
	

	const FInternalNetRefIndex ObjectInternalIndex = NetRefHandleManager->GetInternalIndex(RefHandle);
	if (ObjectInternalIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		UE_LOG(LogIris, Warning, TEXT("OnProtocolMismatchReported from Connection:%s for %s. But object has no InternalIndex."), *PrintConnectionInfo(ConnectionId), *RefHandle.ToString());
		return;
	}

	UObject* ObjInstance = NetRefHandleManager->GetReplicatedObjectInstance(ObjectInternalIndex);
	UObject* ObjArchetype = ObjInstance ? ObjInstance->GetArchetype() : nullptr;

	UE_LOG(LogIris, Error, TEXT("OnProtocolMismatchReported from client:%s when instancing %s. CDO:%s ReplicatedObject:%s NetObject:%s"), *PrintConnectionInfo(ConnectionId), *RefHandle.ToString(), *GetNameSafe(ObjArchetype), *GetNameSafe(ObjInstance), *NetRefHandleManager->PrintObjectFromIndex(ObjectInternalIndex));

	if (UE_LOG_ACTIVE(LogIris, Error))
	{
		const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectInternalIndex);
		const FReplicationInstanceProtocol* InstanceProtocol = ObjectData.InstanceProtocol;
		if (!InstanceProtocol)
		{
			UE_LOG(LogIris, Warning, TEXT("OnProtocolMismatchReported from Connection:%s for %s. But object %s has no InstanceProtocol."), *PrintConnectionInfo(ConnectionId), *RefHandle.ToString(), *GetNameSafe(ObjInstance));
			return;
		}

		const FReplicationProtocol* Protocol = ObjectData.Protocol;
		if (!Protocol)
		{
			UE_LOG(LogIris, Warning, TEXT("OnProtocolMismatchReported from Connection:%s for %s. But object %s has no Protocol."), *PrintConnectionInfo(ConnectionId), *RefHandle.ToString(), *GetNameSafe(ObjInstance));
			return;
		}

		check(Protocol->ReplicationStateCount == InstanceProtocol->FragmentCount);
	
		// Build the list of fragments of this object
		FReplicationFragments Fragments;
		for (uint16 FragmentIndex=0; FragmentIndex < InstanceProtocol->FragmentCount; ++FragmentIndex)
		{
			FReplicationFragmentInfo FragmentInfo;
			FragmentInfo.Fragment = InstanceProtocol->Fragments[FragmentIndex];
			FragmentInfo.Descriptor = Protocol->ReplicationStateDescriptors[FragmentIndex];

			Fragments.Emplace(MoveTemp(FragmentInfo));
		}

		UE::Net::Private::ObjectBridgeDebugging::RemoteProtocolMismatchDetected(ArchetypesAlreadyPrinted, ReplicationSystem, 0 /*TODO: Local ConnectionId*/, Fragments, ObjArchetype, ObjInstance);
	}
}

void UObjectReplicationBridge::OnErrorWithNetRefHandleReported(UE::Net::ENetRefHandleError ErrorType, FNetRefHandle RefHandle, uint32 ConnectionId)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	// Ensure at the end so the log contains all the relevant information
	ON_SCOPE_EXIT
	{
		if (GEnsureNetRefHandleError != -1 && (GEnsureNetRefHandleError==0 || EnumHasAnyFlags((ENetRefHandleError)GEnsureNetRefHandleError,ErrorType)))
		{
			// Use different calls for every error type to prevent only reporting the first error that occured.
			switch(ErrorType)
			{
				case ENetRefHandleError::BitstreamCorrupted:
				{
					ensureMsgf(false, TEXT("NetRefHandle error: Bitstream corrupted reported with %s. Look at the log for important information on the object tied to the handle."), *NetRefHandleManager->PrintObjectFromNetRefHandle(RefHandle));
				} break;
				case ENetRefHandleError::ReplicationDisabled:
				{
					ensureMsgf(false, TEXT("NetRefHandle error: Replication disabled for %s. Client was unable to read the creation data and did not spawn the netobject."), *NetRefHandleManager->PrintObjectFromNetRefHandle(RefHandle));
				} break;
				default:
				{
					ensureMsgf(false, TEXT("NetRefHandle error: %s reported for %s. This should not occur. Missing switch case or wrong error type used."), LexToString(ErrorType), *NetRefHandleManager->PrintObjectFromNetRefHandle(RefHandle));
				} break;
			}
		}
	};

	const FInternalNetRefIndex ObjectInternalIndex = NetRefHandleManager->GetInternalIndex(RefHandle);
	if (ObjectInternalIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		UE_LOG(LogIris, Warning, TEXT("OnErrorWithNetRefHandleReported: %s from client %s for %s but object has no InternalIndex."), LexToString(ErrorType), *PrintConnectionInfo(ConnectionId), *RefHandle.ToString());
		return;
	}

	UE_LOG(LogIris, Error, TEXT("OnErrorWithNetRefHandleReported: %s from client %s. Problematic object was %s"),  LexToString(ErrorType), *PrintConnectionInfo(ConnectionId), *NetRefHandleManager->PrintObjectFromIndex(ObjectInternalIndex));
}

TArray<uint32> UObjectReplicationBridge::FindConnectionsFromArgs(const TArray<FString>& Args) const
{
	using namespace UE::Net::Private;

	TArray<uint32> ConnectionList;

	// If ConnectionId=XX was specified
	if (const FString* ArgConnectionIds = Args.FindByPredicate([](const FString& Str) { return Str.Contains(TEXT("ConnectionId=")); }))
	{
		constexpr bool bIgnoreSeperators = false;

		// Find all the Ids passed in the argument
		FString StrConnectionIds;
		if (FParse::Value(**ArgConnectionIds, TEXT("ConnectionId="), StrConnectionIds, bIgnoreSeperators))
		{
			TArray<FString> StrIds;
			StrConnectionIds.ParseIntoArray(StrIds, TEXT(","));

			for (const FString& StrId : StrIds)
			{
				int32 Id = INDEX_NONE;
				LexFromString(Id, *StrId);
				if (Id > INDEX_NONE)
				{
					ConnectionList.AddUnique((uint32)Id);
				}
			}
		}
	}

	return ConnectionList;
}