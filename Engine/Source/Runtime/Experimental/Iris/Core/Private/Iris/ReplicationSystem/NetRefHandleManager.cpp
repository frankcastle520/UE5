// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetRefHandleManager.h"
#include "ReplicationOperationsInternal.h"
#include "ReplicationProtocol.h"
#include "ReplicationProtocolManager.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "Net/Core/Trace/NetTrace.h"
#include "UObject/CoreNetTypes.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/CoreNetTypes.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/StatsMisc.h"

CSV_DEFINE_CATEGORY(IrisCommon, true);

namespace UE::Net::Private
{

FNetRefHandleManager::FNetRefHandleManager(FReplicationProtocolManager& InReplicationProtocolManager)
	: ReplicationProtocolManager(InReplicationProtocolManager)
{
	static_assert(InvalidInternalIndex == 0, "FNetRefHandleManager::InvalidInternalIndex has an unexpected value");
}

void FNetRefHandleManager::Init(const FInitParams& InitParams)
{
	MaxActiveObjectCount = FNetBitArray::RoundUpToMaxWordBitCount(InitParams.MaxActiveObjectCount);
	InternalNetRefIndexGrowSize = InitParams.InternalNetRefIndexGrowSize > 0 ? FNetBitArray::RoundUpToMaxWordBitCount(InitParams.InternalNetRefIndexGrowSize) : MaxActiveObjectCount;

	ReplicationSystemId = InitParams.ReplicationSystemId;

	// Must be a minimum of 1 to account for InvalidInternalIndex.
	uint32 PreAllocatedNetChunkedArrayCount = FMath::Clamp(InitParams.NetChunkedArrayCount, 1U, MaxActiveObjectCount);

	// Calculate the highest internal index possible with the current preallocated buffers. Must be a minimum of 0 to support InvalidInternalIndex.
	HighestNetChunkedArrayInternalIndex = PreAllocatedNetChunkedArrayCount - 1;

	// Initialize NetObjectList configs
	CurrentMaxInternalNetRefIndex = InitParams.InternalNetRefIndexInitSize > 0 ? FMath::Min(InitParams.InternalNetRefIndexInitSize, MaxActiveObjectCount) : MaxActiveObjectCount;
	CurrentMaxInternalNetRefIndex = FNetBitArray::RoundUpToMaxWordBitCount(CurrentMaxInternalNetRefIndex);

	UE_LOG(LogIris, Log, TEXT("NetRefHandleManager: Configured with MaxActiveObjectCount=%d, MaxInternalNetRefIndex: %u, Grow=%u, NetChunkedArray: Init=%u|Highest=%u"),
		MaxActiveObjectCount, CurrentMaxInternalNetRefIndex, InternalNetRefIndexGrowSize, PreAllocatedNetChunkedArrayCount, HighestNetChunkedArrayInternalIndex);

	// Initialize TNetChunkedArrays with PreAllocatedObjectCount
	ReplicatedObjectData = TNetChunkedArray<FReplicatedObjectData>(PreAllocatedNetChunkedArrayCount, EInitMemory::Constructor);
	ReplicatedObjectRefCount = TNetChunkedArray<uint16>(PreAllocatedNetChunkedArrayCount, EInitMemory::Zero);
	ReplicatedObjectStateBuffers = TNetChunkedArray<uint8*>(PreAllocatedNetChunkedArrayCount, EInitMemory::Zero);
	ReplicatedInstances = TNetChunkedArray<TObjectPtr<UObject>>(PreAllocatedNetChunkedArrayCount, EInitMemory::Zero);

	// For convenience we initialize ReplicatedObjectData for InvalidInternalIndex so that GetReplicatedObjectDataNoCheck returns something useful.
	ReplicatedObjectData[InvalidInternalIndex] = FReplicatedObjectData();

	// Init all NetBitArrays here
	{
		InitNetBitArray(&ScopeFrameData.CurrentFrameScopableInternalIndices);
		InitNetBitArray(&ScopeFrameData.PrevFrameScopableInternalIndices);
		InitNetBitArray(&GlobalScopableInternalIndices);
		InitNetBitArray(&RelevantObjectsInternalIndices);
		InitNetBitArray(&PolledObjectsInternalIndices);
		InitNetBitArray(&DirtyObjectsToQuantize);
		InitNetBitArray(&AssignedInternalIndices);
		InitNetBitArray(&SubObjectInternalIndices);
		InitNetBitArray(&DependentObjectInternalIndices);
		InitNetBitArray(&ObjectsWithDependentObjectsInternalIndices);
		InitNetBitArray(&DestroyedStartupObjectInternalIndices);
		InitNetBitArray(&WantToBeDormantInternalIndices);
		InitNetBitArray(&ObjectsWithPreUpdate);
		InitNetBitArray(&DormantObjectsPendingFlushNet);
	}

	// Mark the invalid index as used
	AssignedInternalIndices.SetBit(0);
}

void FNetRefHandleManager::Deinit()
{
	AssignedInternalIndices.ClearBit(0);
	AssignedInternalIndices.ForAllSetBits([this](uint32 InternalIndex) 
	{
		this->InternalDestroyNetObject(InternalIndex);
	});

	OwnedNetBitArrays.Empty();

	ensureMsgf(OnMaxInternalNetRefIndexIncreased.IsBound() == false, TEXT("FNetRefHandleManager still has delegates registered to OnMaxInternalNetRefIndexIncreased while deinitializing."));
}

void FNetRefHandleManager::InitNetBitArray(FNetBitArray* NetBitArray)
{
	NetBitArray->Init(CurrentMaxInternalNetRefIndex);
	OwnedNetBitArrays.Add(NetBitArray);
}

FInternalNetRefIndex FNetRefHandleManager::GrowNetObjectLists()
{
	check(AssignedInternalIndices.GetNumBits() == CurrentMaxInternalNetRefIndex);

	// The old max is the next available index
	const FInternalNetRefIndex NextFreeIndex = CurrentMaxInternalNetRefIndex;

	// We already are at the max, return InvalidIndex and abort.
	if (CurrentMaxInternalNetRefIndex >= MaxActiveObjectCount)
	{
		return InvalidInternalIndex;
	}

    CurrentMaxInternalNetRefIndex += InternalNetRefIndexGrowSize;

	if (CurrentMaxInternalNetRefIndex > MaxActiveObjectCount)
	{
		// Last grow possibility before we cause a critical failure
		CurrentMaxInternalNetRefIndex = MaxActiveObjectCount;
	}

	UE_LOG(LogIris, Log, TEXT("FNetRefHandleManager::GrowNetObjectLists grew MaxInternalIndex from %u to %u (+%u)"), NextFreeIndex, CurrentMaxInternalNetRefIndex, CurrentMaxInternalNetRefIndex - NextFreeIndex);

	MaxInternalNetRefIndexIncreased(CurrentMaxInternalNetRefIndex);

	return NextFreeIndex;
}

void FNetRefHandleManager::MaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalNetRefIndex)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FNetRefHandleManager_MaxInternalNetRefIndexIncreased);
	CSV_CUSTOM_STAT(IrisCommon, MaxInternalIndexIncreasedCount, 1, ECsvCustomStatOp::Accumulate);

	// Start by reallocating all the NetBitArrays we own
	for (FNetBitArray* NetBitArray : OwnedNetBitArrays)
	{
		NetBitArray->SetNumBits(NewMaxInternalNetRefIndex);
	}

	// Tell other systems to increase their lists too
	OnMaxInternalNetRefIndexIncreased.Broadcast(NewMaxInternalNetRefIndex);
}

void FNetRefHandleManager::GrowNetChunkedArrayBuffers(FInternalNetRefIndex InternalIndex)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FNetRefHandleManager_GrowNetChunkedArrayBuffers);
	CSV_CUSTOM_STAT(IrisCommon, DynamicNetChunkedArrayGrowCount, 1, ECsvCustomStatOp::Accumulate);

	// This call will add the necessary number of elements and chunks to ReplicatedObjectRefCount
	// to accommodate InternalIndex. Once this is done, we determine how many more elements could be
	// added to the array without adding a new chunk and ensure all of the other buffers have this
	// many elements. 
	// 
	// This optimization will reduce the number of calls to AddToIndexUninitialized() and AddToIndexZeroed()
	// but assumes each buffer is going to have the same number of elements all the time.
	ReplicatedObjectRefCount.AddToIndexUninitialized(InternalIndex);
	const int32 LargestIndexInCurrentChunk = ReplicatedObjectRefCount.Capacity() - 1;

	ReplicatedObjectRefCount.AddToIndexUninitialized(LargestIndexInCurrentChunk);
	ReplicatedObjectStateBuffers.AddToIndexZeroed(LargestIndexInCurrentChunk);
	ReplicatedInstances.AddToIndexZeroed(LargestIndexInCurrentChunk);
	ReplicatedObjectData.AddToIndexZeroed(LargestIndexInCurrentChunk);
		
	HighestNetChunkedArrayInternalIndex = static_cast<uint32>(LargestIndexInCurrentChunk);

	OnNetChunkedArrayIncrease.Broadcast(HighestNetChunkedArrayInternalIndex);
}

uint64 FNetRefHandleManager::GetNextNetRefHandleId(uint64 HandleId) const
{
	// Since we use the lowest bit in the index to indicate if the handle is static or dynamic we cannot use all bits as the index
	constexpr uint64 NetHandleIdIndexBitMask = (1ULL << (FNetRefHandle::IdBits - 1)) - 1;

	uint64 NextHandleId = (HandleId + 1) & NetHandleIdIndexBitMask;
	if (NextHandleId == 0)
	{
		++NextHandleId;
	}
	return NextHandleId;
}

FInternalNetRefIndex FNetRefHandleManager::GetNextFreeInternalIndex() const
{
	const uint32 NextFreeIndex = AssignedInternalIndices.FindFirstZero();
	return NextFreeIndex != FNetBitArray::InvalidIndex ? NextFreeIndex : InvalidInternalIndex;
}

FInternalNetRefIndex FNetRefHandleManager::InternalCreateNetObject(const FNetRefHandle NetRefHandle, const FNetHandle GlobalHandle, const FReplicationProtocol* ReplicationProtocol)
{
	if (ActiveObjectCount >= MaxActiveObjectCount)
	{
		UE_LOG(LogIris, Error, TEXT("NetRefHandleManager: Maximum active object count reached (%d/%d)."), ActiveObjectCount, MaxActiveObjectCount);
		ensureMsgf(false, TEXT("NetRefHandleManager: Maximum active object count reached (%d/%d)."), ActiveObjectCount, MaxActiveObjectCount);
		return InvalidInternalIndex;
	}

	// Verify that the handle is free
	if (RefHandleToInternalIndex.Contains(NetRefHandle))
	{
		ensureMsgf(false, TEXT("NetRefHandleManager::InternalCreateNetObject %s already exists"), *NetRefHandle.ToString());
		return InvalidInternalIndex;
	}

	uint32 InternalIndex = GetNextFreeInternalIndex();

	// Try to grow the NetObjectLists if no more indexes are available.
	if (InternalIndex == InvalidInternalIndex)
	{
		InternalIndex = GrowNetObjectLists();

		// If we could not grow anymore, kill the process now. The system cannot replicate objects anymore and the game behavior is undefined.
		if (InternalIndex == InvalidInternalIndex)
		{
			UE_LOG(LogIris, Fatal, TEXT("NetRefHandleManager: Hit the maximum limit of active replicated objects: %u. Aborting since we cannot replicate %s"), MaxActiveObjectCount, ReplicationProtocol->DebugName->Name);
			return InvalidInternalIndex;
		}
	}

	UE_LOG(LogIris, Verbose, TEXT("FNetRefHandleManager::InternalCreateNetObject: (InternalIndex: %u) (%s)"), InternalIndex, *NetRefHandle.ToString());

	// Track the largest internal index and grow internal buffers if necessary.
	if (InternalIndex > HighestNetChunkedArrayInternalIndex)
	{
		GrowNetChunkedArrayBuffers(InternalIndex);
	}

	// Store data;
	FReplicatedObjectData& Data = ReplicatedObjectData[InternalIndex];

	Data = FReplicatedObjectData();

	Data.RefHandle = NetRefHandle;
	Data.NetHandle = GlobalHandle;
	Data.Protocol = ReplicationProtocol;
	Data.InstanceProtocol = nullptr;
	ObjectsWithPreUpdate.ClearBit(InternalIndex);
	ReplicatedObjectStateBuffers[InternalIndex] = nullptr;
	Data.ReceiveStateBuffer = nullptr;
	Data.bShouldPropagateChangedStates = 1U;

	++ActiveObjectCount;

	// Add map entry so that we can map from NetRefHandle to InternalIndex
	RefHandleToInternalIndex.Add(NetRefHandle, InternalIndex);
		
	// Add mapping from global handle to InternalIndex to speed up lookups for ReplicationSystem public API
	if (GlobalHandle.IsValid())
	{
		NetHandleToInternalIndex.Add(GlobalHandle, InternalIndex);
	}

	// Mark Handle index as assigned and scopable for now
	AssignedInternalIndices.SetBit(InternalIndex);
	GlobalScopableInternalIndices.SetBit(InternalIndex);

	// When a handle is first created, it is not set to be a subobject
	SubObjectInternalIndices.ClearBit(InternalIndex);

	// Need a full copy if set, normally only needed for new objects.
	Data.bNeedsFullCopyAndQuantize = 1U;

	// Make sure we do a full poll of all properties the first time the object gets polled.
	Data.bWantsFullPoll = 1U;

	ReplicatedObjectRefCount[InternalIndex] = 0;

	return InternalIndex;
}

void FNetRefHandleManager::AttachInstanceProtocol(FInternalNetRefIndex InternalIndex, const FReplicationInstanceProtocol* InstanceProtocol, UObject* Instance)
{
	if (ensure((InternalIndex != InvalidInternalIndex) && InstanceProtocol))
	{
		FReplicatedObjectData& Data = ReplicatedObjectData[InternalIndex];
		Data.InstanceProtocol = InstanceProtocol;

		check(ReplicatedInstances[InternalIndex] == nullptr);
		ReplicatedInstances[InternalIndex] = Instance;

		ObjectsWithPreUpdate.SetBitValue(InternalIndex, EnumHasAnyFlags(InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::NeedsPreSendUpdate));
	}
}

const FReplicationInstanceProtocol* FNetRefHandleManager::DetachInstanceProtocol(FInternalNetRefIndex InternalIndex)
{
	if (ensure(InternalIndex != InvalidInternalIndex))
	{
		FReplicatedObjectData& Data = ReplicatedObjectData[InternalIndex];
		const FReplicationInstanceProtocol* InstanceProtocol = Data.InstanceProtocol;

		// Detect if someone unexpectedly tries to detach replicated instance
		const bool bDetachedInstancedIsPendingEndReplication = ReplicatedObjectData[InternalIndex].bPendingEndReplication == 1U;
		if (!bDetachedInstancedIsPendingEndReplication)
		{
			UE_LOG(LogIris, Warning, TEXT("FNetRefHandleManager::DetachInstanceProtocol - DetachInstanceProtocol %s %s (%s) from client"), *PrintObjectFromIndex(InternalIndex),*Data.RefHandle.ToString(), Data.Protocol->DebugName->Name);
			ensureMsgf(false, TEXT("DetachInstanceProtocol for %s %s (%s) from client"), *PrintObjectFromIndex(InternalIndex),*Data.RefHandle.ToString(), Data.Protocol->DebugName->Name);
		}
		
		Data.InstanceProtocol = nullptr;
		ReplicatedInstances[InternalIndex] = nullptr;

		ObjectsWithPreUpdate.ClearBit(InternalIndex);
		
		return InstanceProtocol;
	}

	return nullptr;
}

FNetRefHandle FNetRefHandleManager::AllocateNetRefHandle(bool bIsStatic)
{
	uint64& NextHandleId = bIsStatic ? NextStaticHandleIndex : NextDynamicHandleIndex;

	const uint64 NewHandleId = MakeNetRefHandleId(NextHandleId, bIsStatic);
	FNetRefHandle NewHandle = MakeNetRefHandle(NewHandleId, ReplicationSystemId);

	// Verify that the handle is free
	if (RefHandleToInternalIndex.Contains(NewHandle))
	{
		checkf(false, TEXT("FNetRefHandleManager::AllocateNetHandle - Handle %s already exists!"), *NewHandle.ToString());

		return FNetRefHandle();
	}

	// Bump NextHandleId
	NextHandleId = GetNextNetRefHandleId(NextHandleId);

	return NewHandle;
}

FNetRefHandle FNetRefHandleManager::CreateNetObject(FNetRefHandle WantedHandle, FNetHandle GlobalHandle, const FReplicationProtocol* ReplicationProtocol)
{
	FNetRefHandle NetRefHandle = WantedHandle;

	const FInternalNetRefIndex InternalIndex = InternalCreateNetObject(NetRefHandle, GlobalHandle, ReplicationProtocol);
	if (InternalIndex != InvalidInternalIndex)
	{
		FReplicatedObjectData& Data = ReplicatedObjectData[InternalIndex];
		
		// Allocate storage for outgoing data. We need a valid pointer even if the size is zero.
		uint8* StateBuffer = (uint8*)FMemory::MallocZeroed(FPlatformMath::Max(ReplicationProtocol->InternalTotalSize, 1U), ReplicationProtocol->InternalTotalAlignment);
		if (EnumHasAnyFlags(ReplicationProtocol->ProtocolTraits, EReplicationProtocolTraits::HasConditionalChangeMask))
		{
			// Enable all conditions by default. This is to be compatible with the implementation of FRepChangedPropertyTracker where we have hooks into ReplicationSystem.
			FNetBitArrayView ConditionalChangeMask(reinterpret_cast<uint32*>(StateBuffer + ReplicationProtocol->GetConditionalChangeMaskOffset()), ReplicationProtocol->ChangeMaskBitCount, FNetBitArrayView::NoResetNoValidate);
			ConditionalChangeMask.SetAllBits();
		}

		ReplicatedObjectStateBuffers[InternalIndex] = StateBuffer;

		// Bump protocol refcount
		ReplicationProtocol->AddRef();

		return NetRefHandle;
	}
	else
	{
		return FNetRefHandle();
	}
}

// Create NetRefHandle not owned by us
FNetRefHandle FNetRefHandleManager::CreateNetObjectFromRemote(FNetRefHandle WantedHandle, const FReplicationProtocol* ReplicationProtocol,  UE::Net::FNetObjectFactoryId FactoryId)
{
	if (!ensureMsgf(WantedHandle.IsValid() && !WantedHandle.IsCompleteHandle(), TEXT("FNetRefHandleManager::CreateNetObjectFromRemote Expected WantedHandle %s to be valid and incomplete"), *WantedHandle.ToString()))
	{
		return FNetRefHandle();
	}

	check(FactoryId != UE::Net::InvalidNetObjectFactoryId);

	FNetRefHandle NetRefHandle = MakeNetRefHandle(WantedHandle.GetId(), ReplicationSystemId);

	const FInternalNetRefIndex InternalIndex = InternalCreateNetObject(NetRefHandle, FNetHandle(), ReplicationProtocol);
	if (InternalIndex != InvalidInternalIndex)
	{
		FReplicatedObjectData& Data = ReplicatedObjectData[InternalIndex];

		Data.NetFactoryId = FactoryId;

		// Allocate storage for incoming data
		Data.ReceiveStateBuffer = (uint8*)FMemory::Malloc(ReplicationProtocol->InternalTotalSize, ReplicationProtocol->InternalTotalAlignment);

		// Since we currently do not have any default values, just initialize to zero
		// N.B. If this Memzero is optimized away for some reason it still needs to be done for protocols with dynamic state.
		FMemory::Memzero(Data.ReceiveStateBuffer, ReplicationProtocol->InternalTotalSize);

		// Note: We could initialize this from default but at the moment it is part of the contract for all serializers to write the value when we serialize and we will only apply dirty states

		// Don't bother initializing the conditional changemask if present since it's currently unused on the receiving end.

		// Bump protocol refcount
		ReplicationProtocol->AddRef();

		return NetRefHandle;
	}
	else
	{
		return FNetRefHandle();
	}
}

void FNetRefHandleManager::InternalDestroyNetObject(FInternalNetRefIndex InternalIndex)
{
	UE_LOG(LogIris, Verbose, TEXT("FNetRefHandleManager::InternalDestroyNetObject: %s"), *PrintObjectFromIndex(InternalIndex));

	FReplicatedObjectData& Data = ReplicatedObjectData[InternalIndex];

	uint8* StateBuffer = ReplicatedObjectStateBuffers[InternalIndex];
	// Free any allocated resources
	if (EnumHasAnyFlags(Data.Protocol->ProtocolTraits, EReplicationProtocolTraits::HasDynamicState))
	{
		FNetSerializationContext FreeContext;
		FInternalNetSerializationContext InternalContext;
		FreeContext.SetInternalContext(&InternalContext);
		if (StateBuffer != nullptr)
		{
			FReplicationProtocolOperationsInternal::FreeDynamicState(FreeContext, StateBuffer, Data.Protocol);
		}
		if (Data.ReceiveStateBuffer != nullptr)
		{
			FReplicationProtocolOperationsInternal::FreeDynamicState(FreeContext, Data.ReceiveStateBuffer, Data.Protocol);
		}
	}
	
	// If we have subobjects remove them from our list
	if (FNetDependencyData::FInternalNetRefIndexArray* SubObjectArray = SubObjects.GetInternalIndexArray<FNetDependencyData::EArrayType::SubObjects>(InternalIndex))
	{
		for (FInternalNetRefIndex SubObjectInternalIndex : *SubObjectArray)
		{
			InternalRemoveSubObject(InternalIndex, SubObjectInternalIndex, false);
		}
		SubObjectArray->Reset(0U);
	}
	// Clear ChildSubObjectArray
	if (FNetDependencyData::FInternalNetRefIndexArray* ChildSubObjectArray = SubObjects.GetInternalIndexArray<FNetDependencyData::EArrayType::ChildSubObjects>(InternalIndex))
	{
		ChildSubObjectArray->Reset(0U);
	}

	// If we are a subobject remove from owner and hierarchical parents
	if (Data.SubObjectRootIndex != InvalidInternalIndex)
	{
		InternalRemoveSubObject(Data.SubObjectRootIndex, InternalIndex);
	}

	// Remove from all dependent object relationships and clear data
	InternalRemoveDependentObject(InternalIndex);

	// Free all stored data for object
	SubObjects.FreeStoredDependencyDataForObject(InternalIndex);

	// Decrease protocol refcount
	Data.Protocol->Release();
	if (Data.Protocol->GetRefCount() == 0)
	{
		ReplicationProtocolManager.DestroyReplicationProtocol(Data.Protocol);
	}

	FMemory::Free(StateBuffer);
	FMemory::Free(Data.ReceiveStateBuffer);

	// Clear pointer to state buffer
	ReplicatedObjectStateBuffers[InternalIndex] = nullptr;

	UE_NET_TRACE_NETHANDLE_DESTROYED(Data.RefHandle);

	Data = FReplicatedObjectData();

	// tracking
	AssignedInternalIndices.ClearBit(InternalIndex);

	// Restore internal state
	ClearStateForFreedInternalIndex(InternalIndex);

	// Cleanup cross reference to destruction info
	if (DestroyedStartupObjectInternalIndices.GetBit(InternalIndex))
	{
		DestroyedStartupObjectInternalIndices.ClearBit(InternalIndex);
		uint32 OtherInternalIndex = 0U;
		if (DestroyedStartupObject.RemoveAndCopyValue(InternalIndex, OtherInternalIndex))
		{
			DestroyedStartupObject.Remove(OtherInternalIndex);
		}
	}

	--ActiveObjectCount;
}

void FNetRefHandleManager::ClearStateForFreedInternalIndex(FInternalNetRefIndex FreedInternalIndex)
{
	GlobalScopableInternalIndices.ClearBit(FreedInternalIndex);
	ObjectsWithPreUpdate.ClearBit(FreedInternalIndex);
	SubObjectInternalIndices.ClearBit(FreedInternalIndex);
	ObjectsWithDependentObjectsInternalIndices.ClearBit(FreedInternalIndex);
	WantToBeDormantInternalIndices.ClearBit(FreedInternalIndex);
	DormantObjectsPendingFlushNet.ClearBit(FreedInternalIndex);
}

FNetRefHandle FNetRefHandleManager::CreateHandleForDestructionInfo(FNetRefHandle Handle, const FReplicationProtocol* DestroyedObjectProtocol)
{
	// Create destruction info handle carrying destruction info
	constexpr bool bIsStaticHandle = false;
	FNetRefHandle AllocatedHandle = AllocateNetRefHandle(bIsStaticHandle);
	FNetRefHandle DestructionInfoHandle = CreateNetObject(AllocatedHandle, FNetHandle(), DestroyedObjectProtocol);

	if (DestructionInfoHandle.IsValid())
	{
		const uint32 InternalIndex = GetInternalIndex(DestructionInfoHandle);
		const uint32 DestroyedInternalIndex = GetInternalIndex(Handle);
		
		// mark the internal index
		DestroyedStartupObjectInternalIndices.SetBit(InternalIndex);
				
		// Is the object replicated, if so we must make sure that we do not add it to scope by accident
		if (DestroyedInternalIndex)
		{			
			DestroyedStartupObject.FindOrAdd(InternalIndex, DestroyedInternalIndex);
		
			// Mark the replicated index as destroyed
			DestroyedStartupObjectInternalIndices.SetBit(DestroyedInternalIndex);
			DestroyedStartupObject.FindOrAdd(DestroyedInternalIndex, InternalIndex);
		}
	}

	return DestructionInfoHandle;
}

void FNetRefHandleManager::RemoveFromScope(FInternalNetRefIndex InternalIndex)
{
	// Can only remove an object from scope if it is assignable
	if (ensure(AssignedInternalIndices.GetBit(InternalIndex)))
	{
		GlobalScopableInternalIndices.ClearBit(InternalIndex);
	}
}

void FNetRefHandleManager::DestroyNetObject(FNetRefHandle RefHandle)
{
	FInternalNetRefIndex InternalIndex = RefHandleToInternalIndex.FindAndRemoveChecked(RefHandle);
	if (ensure(AssignedInternalIndices.GetBit(InternalIndex)))
	{
		FReplicatedObjectData& Data = ReplicatedObjectData[InternalIndex];
		check(Data.RefHandle == RefHandle);

		// Remove mapping from global handle to internal index
		NetHandleToInternalIndex.Remove(Data.NetHandle);

		// Remove from scopable objects if not already done
		GlobalScopableInternalIndices.ClearBit(InternalIndex);

		// We always defer the actual destroy
		PendingDestroyInternalIndices.Add(InternalIndex);
	}
}

void FNetRefHandleManager::DestroyObjectsPendingDestroy()
{
	IRIS_PROFILER_SCOPE(FNetRefHandleManager_DestroyObjectsPendingDestroy);

	// Destroy Objects pending destroy
	for (int32 It = PendingDestroyInternalIndices.Num() - 1; It >= 0; --It)
	{
		const FInternalNetRefIndex InternalIndex = PendingDestroyInternalIndices[It];
		// If we have subobjects pending tear off and such then wait before destroying the parent.
		if (ReplicatedObjectRefCount[InternalIndex] == 0 && GetSubObjects(InternalIndex).Num() <= 0)
		{
			InternalDestroyNetObject(InternalIndex);
			PendingDestroyInternalIndices.RemoveAtSwap(It);
		}
	}
	CSV_CUSTOM_STAT(IrisCommon, PendingDestroyInternalIndicesCount, (float)PendingDestroyInternalIndices.Num(), ECsvCustomStatOp::Set);
}

bool FNetRefHandleManager::AddSubObject(FNetRefHandle OwnerHandle, FNetRefHandle SubObjectHandle, FNetRefHandle RelativeOtherSubObjectHandle, EAddSubObjectFlags Flags)
{
	check(OwnerHandle != SubObjectHandle);

	// validate objects
	const FInternalNetRefIndex OwnerInternalIndex = GetInternalIndex(OwnerHandle);
	const FInternalNetRefIndex SubObjectInternalIndex = GetInternalIndex(SubObjectHandle);

	const bool bIsValidOwner = ensure(OwnerInternalIndex != InvalidInternalIndex);
	const bool bIsValidSubObejct = ensure(SubObjectInternalIndex != InvalidInternalIndex);

	if (!(bIsValidOwner && bIsValidSubObejct))
	{
		return false;
	}

	const FInternalNetRefIndex RelativeOtherSubObjectInternalIndex = EnumHasAnyFlags(Flags, EAddSubObjectFlags::ReplicateWithSubObject) ? GetInternalIndex(RelativeOtherSubObjectHandle) : InvalidInternalIndex;
	return InternalAddSubObject(OwnerInternalIndex, SubObjectInternalIndex, RelativeOtherSubObjectInternalIndex, Flags);
}

bool FNetRefHandleManager::InternalAddSubObject(FInternalNetRefIndex OwnerInternalIndex, FInternalNetRefIndex SubObjectInternalIndex, FInternalNetRefIndex RelativeOtherSubObjectInternalIndex, EAddSubObjectFlags Flags)
{
	using namespace UE::Net::Private;

	FReplicatedObjectData& SubObjectData = GetReplicatedObjectDataNoCheck(SubObjectInternalIndex);
	if (!ensureMsgf(SubObjectData.SubObjectRootIndex == InvalidInternalIndex, TEXT("FNetRefHandleManager::AddSubObject %s is already marked as a subobject"), ToCStr(SubObjectData.RefHandle.ToString())))
	{
		return false;
	}

	FNetDependencyData::FInternalNetRefIndexArray& SubObjectArray = SubObjects.GetOrCreateInternalIndexArray<FNetDependencyData::SubObjects>(OwnerInternalIndex);
	SubObjectArray.Add(SubObjectInternalIndex);
	SubObjectData.SubObjectRootIndex = OwnerInternalIndex;
	SubObjectData.bDestroySubObjectWithOwner = EnumHasAnyFlags(Flags, EAddSubObjectFlags::DestroyWithOwner);
	// Mark the object as a subobject
	SetIsSubObject(SubObjectInternalIndex, true);

	if (RelativeOtherSubObjectInternalIndex != InvalidInternalIndex && ensureMsgf(SubObjectArray.Find(RelativeOtherSubObjectInternalIndex) != INDEX_NONE, TEXT("RelativeOtherSubObjectHandle %s Must be a Subobject of %s"), ToCStr(GetNetRefHandleFromInternalIndex(RelativeOtherSubObjectInternalIndex).ToString()), ToCStr(GetNetRefHandleFromInternalIndex(OwnerInternalIndex).ToString())))
	{
		// Add to child array of RelativeOtherSubObjectRelativeIndex for hierarchical replication order
		FNetDependencyData::FSubObjectConditionalsArray* SubObjectConditionalsArray = nullptr;
		FNetDependencyData::FInternalNetRefIndexArray& ChildSubObjectArray = SubObjects.GetOrCreateInternalChildSubObjectsArray(RelativeOtherSubObjectInternalIndex, SubObjectConditionalsArray);
		ChildSubObjectArray.Add(SubObjectInternalIndex);
		if (SubObjectConditionalsArray)
		{
			SubObjectConditionalsArray->Add(ELifetimeCondition::COND_None);
		}
		SubObjectData.SubObjectParentIndex = RelativeOtherSubObjectInternalIndex;
	}
	else
	{
		// Add to child array of root
		FNetDependencyData::FSubObjectConditionalsArray* SubObjectConditionalsArray = nullptr;
		FNetDependencyData::FInternalNetRefIndexArray& ChildSubObjectArray = SubObjects.GetOrCreateInternalChildSubObjectsArray(OwnerInternalIndex, SubObjectConditionalsArray);
		ChildSubObjectArray.Add(SubObjectInternalIndex);
		if (SubObjectConditionalsArray)
		{
			SubObjectConditionalsArray->Add(ELifetimeCondition::COND_None);
		}
		SubObjectData.SubObjectParentIndex = OwnerInternalIndex;
	}

	return true;
}

void FNetRefHandleManager::InternalRemoveSubObject(FInternalNetRefIndex OwnerInternalIndex, FInternalNetRefIndex SubObjectInternalIndex, bool bRemoveFromSubObjectArray)
{
	// both must be valid
	if (OwnerInternalIndex != InvalidInternalIndex && SubObjectInternalIndex != InvalidInternalIndex)
	{
		FReplicatedObjectData& SubObjectData = GetReplicatedObjectDataNoCheck(SubObjectInternalIndex);
		check(SubObjectData.SubObjectRootIndex == OwnerInternalIndex);
		
		if (bRemoveFromSubObjectArray)
		{
			// Remove from root parent
			if (FNetDependencyData::FInternalNetRefIndexArray* SubObjectArray = SubObjects.GetInternalIndexArray<FNetDependencyData::EArrayType::SubObjects>(OwnerInternalIndex))
			{
				SubObjectArray->Remove(SubObjectInternalIndex);
			}
			// Remove from parents ChildSubObjectArray
			if (SubObjectData.SubObjectParentIndex != InvalidInternalIndex)
			{
				FNetDependencyData::FInternalNetRefIndexArray* ChildSubObjectArray;
				FNetDependencyData::FSubObjectConditionalsArray* SubObjectConditionsArray;

				if (SubObjects.GetInternalChildSubObjectAndConditionalArrays(SubObjectData.SubObjectParentIndex, ChildSubObjectArray, SubObjectConditionsArray))
				{
					const int32 ArrayIndex = ChildSubObjectArray->Find(SubObjectInternalIndex);
					if (ensure(ArrayIndex != INDEX_NONE))
					{
						ChildSubObjectArray->RemoveAt(ArrayIndex);
						if (SubObjectConditionsArray)
						{
							SubObjectConditionsArray->RemoveAt(ArrayIndex);
							check(SubObjectConditionsArray->Num() == ChildSubObjectArray->Num());
						}
					}
				}
			}
		}

		SubObjectData.SubObjectRootIndex = InvalidInternalIndex;
		SubObjectData.SubObjectParentIndex = InvalidInternalIndex;
		SubObjectData.bDestroySubObjectWithOwner = false;

		SetIsSubObject(SubObjectInternalIndex, false);
	}
}

void FNetRefHandleManager::RemoveSubObject(FNetRefHandle Handle)
{
	FInternalNetRefIndex SubObjectInternalIndex = GetInternalIndex(Handle);
	checkSlow(SubObjectInternalIndex);

	if (SubObjectInternalIndex != InvalidInternalIndex)
	{
		const uint32 OwnerInternalIndex = ReplicatedObjectData[SubObjectInternalIndex].SubObjectRootIndex;
		if (OwnerInternalIndex != InvalidInternalIndex)
		{
			InternalRemoveSubObject(OwnerInternalIndex, SubObjectInternalIndex);
		}
	}
}

bool FNetRefHandleManager::SetSubObjectNetCondition(FInternalNetRefIndex SubObjectInternalIndex, FLifeTimeConditionStorage SubObjectCondition)
{
	if (ensure(SubObjectInternalIndex != InvalidInternalIndex))
	{
		const FInternalNetRefIndex SubObjectParentIndex = ReplicatedObjectData[SubObjectInternalIndex].SubObjectParentIndex;
		if (ensure(SubObjectParentIndex != InvalidInternalIndex))
		{
			FNetDependencyData::FInternalNetRefIndexArray* SubObjectsArray;
			FNetDependencyData::FSubObjectConditionalsArray* SubObjectConditionals;
			
			if (SubObjects.GetInternalChildSubObjectAndConditionalArrays(SubObjectParentIndex, SubObjectsArray, SubObjectConditionals))
			{
				const int32 SubObjectArrayIndex = SubObjectsArray->Find(SubObjectInternalIndex);
				if (ensure(SubObjectArrayIndex != INDEX_NONE))
				{
					if (!SubObjectConditionals)
					{
						SubObjectConditionals = &SubObjects.GetOrCreateSubObjectConditionalsArray(SubObjectParentIndex);
					}

					check(SubObjectConditionals->Num() == SubObjectsArray->Num());

					(*SubObjectConditionals)[SubObjectArrayIndex] = SubObjectCondition;
					return true;
				}
			}

		}
	}

	return false;
}

FNetRefHandle FNetRefHandleManager::GetRootObjectOfSubObject(FNetRefHandle SubObjectRefHandle) const
{
	const FInternalNetRefIndex SubObjectInternalIndex = GetInternalIndex(SubObjectRefHandle);
	const FInternalNetRefIndex OwnerInternalIndex = SubObjectInternalIndex != InvalidInternalIndex ? ReplicatedObjectData[SubObjectInternalIndex].SubObjectRootIndex : InvalidInternalIndex;

	return OwnerInternalIndex != InvalidInternalIndex ? ReplicatedObjectData[OwnerInternalIndex].RefHandle : FNetRefHandle();
}

FInternalNetRefIndex FNetRefHandleManager::GetRootObjectInternalIndexOfSubObject(FInternalNetRefIndex SubObjectIndex) const
{
	return SubObjectIndex != InvalidInternalIndex ? ReplicatedObjectData[SubObjectIndex].SubObjectRootIndex : InvalidInternalIndex;
}

bool FNetRefHandleManager::AddDependentObject(FNetRefHandle ParentRefHandle, FNetRefHandle DependentObjectRefHandle, EDependentObjectSchedulingHint SchedulingHint)
{
	check(ParentRefHandle != DependentObjectRefHandle);

	// validate objects
	FInternalNetRefIndex ParentInternalIndex = GetInternalIndex(ParentRefHandle);
	FInternalNetRefIndex DependentObjectInternalIndex = GetInternalIndex(DependentObjectRefHandle);

	const bool bIsValidOwner = ensure(ParentInternalIndex != InvalidInternalIndex);
	const bool bIsValidDependentObject = ensure(DependentObjectInternalIndex != InvalidInternalIndex);

	if (!(bIsValidOwner && bIsValidDependentObject))
	{
		return false;
	}

	FReplicatedObjectData& DependentObjectData = GetReplicatedObjectDataNoCheck(DependentObjectInternalIndex);
	FReplicatedObjectData& ParentObjectData = GetReplicatedObjectDataNoCheck(ParentInternalIndex);

	// SubObjects cannot have dependent objects or be a dependent object (for now)
	check(!DependentObjectData.IsSubObject() && !ParentObjectData.IsSubObject());
	check(!SubObjectInternalIndices.GetBit(DependentObjectInternalIndex));
	check(!SubObjectInternalIndices.GetBit(ParentInternalIndex));

	FNetDependencyData::FDependentObjectInfoArray& ParentDependentObjectsArray = SubObjects.GetOrCreateDependentObjectInfoArray(ParentInternalIndex);
	FNetDependencyData::FInternalNetRefIndexArray& DependentParentObjectArray = SubObjects.GetOrCreateInternalIndexArray<FNetDependencyData::DependentParentObjects>(DependentObjectInternalIndex);
	
	// Make sure parent didn't set the child as a dependent already
	{
		const FDependentObjectInfo* DependentInfo = ParentDependentObjectsArray.FindByPredicate([DependentObjectInternalIndex](const FDependentObjectInfo& Entry) { return Entry.NetRefIndex == DependentObjectInternalIndex;});
		if (DependentInfo)
		{
			// Make sure the children is also dependent to the Parent
			checkf(DependentParentObjectArray.Find(ParentInternalIndex) != INDEX_NONE, TEXT("FNetRefHandleManager::AddDependentObject: Parent: %s already has child: %s as dependent but not the inverse."), 
				*PrintObjectFromNetRefHandle(ParentRefHandle), *PrintObjectFromNetRefHandle(DependentObjectRefHandle));

			// If they were already dependent there is no side-effect, unless the scheduler hint would have been changed by the new call.
			UE_LOG(LogIris, Warning, TEXT("FNetRefHandleManager::AddDependentObject: Parent: %s already has child: %s as a dependent"), *PrintObjectFromNetRefHandle(ParentRefHandle), *PrintObjectFromNetRefHandle(DependentObjectRefHandle));
			ensureMsgf(DependentInfo->SchedulingHint == SchedulingHint, TEXT("FNetRefHandleManager::AddDependentObject: Conflicting scheduling hint between Child: %s and Parent: %s. Requested %s but was already set to %s"),
				*PrintObjectFromNetRefHandle(DependentObjectRefHandle), *PrintObjectFromNetRefHandle(ParentRefHandle), LexToString(SchedulingHint), LexToString(DependentInfo->SchedulingHint));
			return false;
		}
	}

	// If child was already set as dependent on the parent there is a logic error somewhere.
	checkf(DependentParentObjectArray.Find(ParentInternalIndex) == INDEX_NONE, TEXT("FNetRefHandleManager::AddDependentObject: Child: %s already dependent of Parent: %s but not the inverse."), *PrintObjectFromNetRefHandle(DependentObjectRefHandle), *PrintObjectFromNetRefHandle(ParentRefHandle));

	// Add dependent to parent's dependent object list
	FDependentObjectInfo DependentObjectInfo;
	DependentObjectInfo.NetRefIndex = DependentObjectInternalIndex;
	DependentObjectInfo.SchedulingHint = SchedulingHint;
	ParentDependentObjectsArray.Add(DependentObjectInfo);

	// Add parent to dependent's list
	DependentParentObjectArray.Add(ParentInternalIndex);

	// Update cached info to avoid to do map lookups to find out if we are a dependent object or have dependent objects
	DependentObjectData.bIsDependentObject = true;
	ParentObjectData.bHasDependentObjects = true;
	ObjectsWithDependentObjectsInternalIndices.SetBit(ParentInternalIndex);
	DependentObjectInternalIndices.SetBit(DependentObjectInternalIndex);

	return true;
}

void FNetRefHandleManager::RemoveDependentObject(FNetRefHandle DependentHandle)
{
	FInternalNetRefIndex DependentInternalIndex = GetInternalIndex(DependentHandle);

	if (DependentInternalIndex != InvalidInternalIndex)
	{
		InternalRemoveDependentObject(DependentInternalIndex);
	}
}

void FNetRefHandleManager::InternalRemoveDependentObject(FInternalNetRefIndex ParentInternalIndex, FInternalNetRefIndex DependentInternalIndex, ERemoveDependentObjectFlags Flags)
{
	if (EnumHasAnyFlags(Flags, ERemoveDependentObjectFlags::RemoveFromDependentParentObjects))
	{
		if (FNetDependencyData::FInternalNetRefIndexArray* ParentObjectArray = SubObjects.GetInternalIndexArray<FNetDependencyData::DependentParentObjects>(DependentInternalIndex))
		{
			ParentObjectArray->Remove(ParentInternalIndex);
			if (ParentObjectArray->Num() == 0)
			{
				FReplicatedObjectData& DependentObjectData = GetReplicatedObjectDataNoCheck(DependentInternalIndex);
				DependentObjectData.bIsDependentObject = false;
				DependentObjectInternalIndices.ClearBit(DependentInternalIndex);
			}
		}
	}

	if (EnumHasAnyFlags(Flags, ERemoveDependentObjectFlags::RemoveFromParentDependentObjects))
	{
		FReplicatedObjectData& ParentObjectData = GetReplicatedObjectDataNoCheck(ParentInternalIndex);
		FNetDependencyData::FDependentObjectInfoArray* ParentDependentObjectsArray = ParentObjectData.bHasDependentObjects ? SubObjects.GetDependentObjectInfoArray(ParentInternalIndex) : nullptr;
		if (ParentDependentObjectsArray)
		{
			const int32 ArrayIndex =  ParentDependentObjectsArray->FindLastByPredicate([DependentInternalIndex](const FDependentObjectInfo& Entry) { return Entry.NetRefIndex == DependentInternalIndex;});
			if (ArrayIndex != INDEX_NONE)
			{
				ParentDependentObjectsArray->RemoveAt(ArrayIndex);
			}

			if (ParentDependentObjectsArray->Num() == 0)
			{
				ParentObjectData.bHasDependentObjects = false;
				ObjectsWithDependentObjectsInternalIndices.ClearBit(ParentInternalIndex);
			}
		}
	}
}

void FNetRefHandleManager::InternalRemoveDependentObject(FInternalNetRefIndex DependentInternalIndex)
{
	// Remove from all parents
	if (FNetDependencyData::FInternalNetRefIndexArray* ParentObjectArray = SubObjects.GetInternalIndexArray<FNetDependencyData::DependentParentObjects>(DependentInternalIndex))
	{
		for (FInternalNetRefIndex ParentInternalIndex : *ParentObjectArray)
		{
			// Flag is set to only update data on the parent to avoid modifying the array we iterate over
			InternalRemoveDependentObject(ParentInternalIndex, DependentInternalIndex, ERemoveDependentObjectFlags::RemoveFromParentDependentObjects);
		}
		ParentObjectArray->Reset();
	}

	// Remove from our dependents
	if (FNetDependencyData::FDependentObjectInfoArray* DependentObjectArray = SubObjects.GetDependentObjectInfoArray(DependentInternalIndex))
	{
		for (const FDependentObjectInfo& ChildDependentObjectInfo : *DependentObjectArray)
		{
			// Flag is set to only update data on the childDependentObject to avoid modifying the array we iterate over
			InternalRemoveDependentObject(DependentInternalIndex, ChildDependentObjectInfo.NetRefIndex, ERemoveDependentObjectFlags::RemoveFromDependentParentObjects);
		}
		DependentObjectArray->Reset();		
	}

	FReplicatedObjectData& DependentObjectData = GetReplicatedObjectDataNoCheck(DependentInternalIndex);

	// Clear out flags on this object	
	DependentObjectData.bIsDependentObject = false;
	DependentObjectData.bHasDependentObjects = false;
	ObjectsWithDependentObjectsInternalIndices.ClearBit(DependentInternalIndex);
	DependentObjectInternalIndices.ClearBit(DependentInternalIndex);
}

void FNetRefHandleManager::RemoveDependentObject(FNetRefHandle ParentHandle, FNetRefHandle DependentHandle)
{
	// Validate objects
	FInternalNetRefIndex ParentInternalIndex = GetInternalIndex(ParentHandle);
	FInternalNetRefIndex DependentInternalIndex = GetInternalIndex(DependentHandle);

	if ((ParentInternalIndex == InvalidInternalIndex) || (DependentInternalIndex == InvalidInternalIndex))
	{
		return;
	}

	InternalRemoveDependentObject(ParentInternalIndex, DependentInternalIndex);
}

void FNetRefHandleManager::SetShouldPropagateChangedStates(FInternalNetRefIndex ObjectInternalIndex, bool bShouldPropagateChangedStates)
{
	if (ObjectInternalIndex != InvalidInternalIndex)
	{
		if (bShouldPropagateChangedStates)
		{
			// Currently we do not support re-enabling state propagation
			// $IRIS: $TODO: Implement method to force dirty all changes 
			// https://jira.it.epicgames.com/browse/UE-127368

			checkf(false, TEXT("Re-enabling state change propagation is currently Not implemented."));			
			return;
		}

		ReplicatedObjectData[ObjectInternalIndex].bShouldPropagateChangedStates = bShouldPropagateChangedStates ? 1U : 0U;
	}
}

void FNetRefHandleManager::SetShouldPropagateChangedStates(FNetRefHandle Handle, bool bShouldPropagateChangedStates)
{
	FInternalNetRefIndex ObjectInternalIndex = GetInternalIndex(Handle);
	return SetShouldPropagateChangedStates(ObjectInternalIndex, bShouldPropagateChangedStates);
}

void FNetRefHandleManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (TObjectPtr<UObject>& Object : ReplicatedInstances)
	{
		Collector.AddReferencedObject(Object);
	}
}

uint64 FNetRefHandleManager::MakeNetRefHandleId(uint64 Id, bool bIsStatic)
{
	return (Id << 1U) | (bIsStatic ? 1U : 0U);
}

FNetRefHandle FNetRefHandleManager::MakeNetRefHandle(uint64 Id, uint32 ReplicationSystemId)
{
	check((Id & FNetRefHandle::IdMask) == Id);
	check(ReplicationSystemId < FNetRefHandle::MaxReplicationSystemId);

	FNetRefHandle Handle;

	Handle.Id = Id;
	Handle.ReplicationSystemId = ReplicationSystemId + 1U;

	return Handle;
}

FNetRefHandle FNetRefHandleManager::MakeNetRefHandleFromId(uint64 Id)
{
	// This is called on the receiving end when deserializing replicated objects. We don't want to crash on bit stream errors leading to invalid handle IDs being read.
	ensure((Id & FNetRefHandle::IdMask) == Id);

	FNetRefHandle Handle;

	Handle.Id = Id;
	Handle.ReplicationSystemId = 0U;

	return Handle;
}

void FNetRefHandleManager::OnPreSendUpdate()
{
	// The current frame scope is based on all indexes assigned up to this point.
	ScopeFrameData.CurrentFrameScopableInternalIndices.Copy(GlobalScopableInternalIndices);

	// Allow the list to be read.
	ScopeFrameData.bIsValid = true;
}

void FNetRefHandleManager::OnPostSendUpdate()
{
	// Store the scope for the next frame.
	ScopeFrameData.PrevFrameScopableInternalIndices.Copy(ScopeFrameData.CurrentFrameScopableInternalIndices);

	// From here no-one should access the ScopeFrameData
	ScopeFrameData.bIsValid = false;

	CSV_CUSTOM_STAT(IrisCommon, ActiveReplicatedObjectCount, (float)ActiveObjectCount, ECsvCustomStatOp::Set);
}

FString FNetRefHandleManager::PrintObjectFromIndex(FInternalNetRefIndex ObjectIndex) const
{
	if (ObjectIndex != InvalidInternalIndex)
	{
		const FNetRefHandle NetRefHandle = GetNetRefHandleFromInternalIndex(ObjectIndex);
		const FReplicatedObjectData& ObjectData = GetReplicatedObjectDataNoCheck(ObjectIndex);

		if (ObjectData.SubObjectRootIndex == InvalidInternalIndex)
		{
			return FString::Printf(TEXT("RootObject %s (InternalIndex: %u) (%s)"), *GetNameSafe(ReplicatedInstances[ObjectIndex]), ObjectIndex, *NetRefHandle.ToString());
		}
		else
		{
			const FNetRefHandle RootNetRefHandle = GetNetRefHandleFromInternalIndex(ObjectData.SubObjectRootIndex);
			return FString::Printf(TEXT("SubObject %s (InternalIndex: %u) (%s) tied to RootObject %s (InternalIndex: %u) (%s)"), 
								*GetNameSafe(ReplicatedInstances[ObjectIndex]), ObjectIndex, *NetRefHandle.ToString(),
								*GetNameSafe(ReplicatedInstances[ObjectData.SubObjectRootIndex]), ObjectData.SubObjectRootIndex, *RootNetRefHandle.ToString());
		}
	}
	else
	{
		return FString(TEXT("InvalidObject (InternalIndex: Invalid)"));
	}
}

FString FNetRefHandleManager::PrintObjectFromNetRefHandle(FNetRefHandle ObjectHandle) const
{ 
	const FInternalNetRefIndex ObjectIndex = GetInternalIndex(ObjectHandle);
	if (ObjectIndex != InvalidInternalIndex)
	{
		return PrintObjectFromIndex(ObjectIndex);
	}
	else
	{
		return FString::Printf(TEXT("NetObject None (InternalIndex: None) (%s)"), *ObjectHandle.ToString());
	}
	
}

} // end namespace UE::Net::Private
