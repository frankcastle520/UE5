// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowInputOutput.h"

#include "ChaosLog.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowCoreNodes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowInputOutput)


FDataflowInput FDataflowInput::NoOpInput = FDataflowInput();
FDataflowOutput FDataflowOutput::NoOpOutput = FDataflowOutput();


FDataflowInput::FDataflowInput(const UE::Dataflow::FInputParameters& Param, FGuid InGuid)
	: FDataflowConnection(UE::Dataflow::FPin::EDirection::INPUT, Param)
	, Connection(nullptr)
{
	Guid = InGuid;
}

FDataflowInput::FDataflowInput(const UE::Dataflow::FInputParameters& Param)
	: FDataflowConnection(UE::Dataflow::FPin::EDirection::INPUT, Param)
	, Connection(nullptr)
{
}

bool FDataflowInput::AddConnection(FDataflowConnection* InOutput)
{
	if (Type == InOutput->GetType())
	{
		Connection = (FDataflowOutput*)InOutput;
		GetOwningNode()->Invalidate();
		return true;
	}
	return false;
}

bool FDataflowInput::RemoveConnection(FDataflowConnection* InOutput)
{
	if (ensure(Connection == (FDataflowOutput*)InOutput))
	{
		Connection = nullptr;
		GetOwningNode()->Invalidate();
		return true;
	}
	return false;
}

TArray< FDataflowOutput* > FDataflowInput::GetConnectedOutputs()
{
	TArray<FDataflowOutput* > RetList;
	if (FDataflowOutput* Conn = GetConnection())
	{
		RetList.Add(Conn);
	}
	return RetList;
}

const TArray< const FDataflowOutput* > FDataflowInput::GetConnectedOutputs() const
{
	TArray<const FDataflowOutput* > RetList;
	if (const FDataflowOutput* Conn = GetConnection())
	{
		RetList.Add(Conn);
	}
	return RetList;
}

void FDataflowInput::Invalidate(const UE::Dataflow::FTimestamp& ModifiedTimestamp)
{
	OwningNode->Invalidate(ModifiedTimestamp);
}

void FDataflowInput::PullValue(UE::Dataflow::FContext& Context) const
{
	if (GetConnectedOutputs().Num())
	{
		ensure(GetConnectedOutputs().Num() == 1);
		if (const FDataflowOutput* ConnectionOut = GetConnection())
		{
			ConnectionOut->Evaluate(Context);
		}
	}
}

void FDataflowInput::FixAndPropagateType(FName InType)
{
	check(InType.ToString().StartsWith(Type.ToString()));
	check(!FDataflowConnection::IsAnyType(InType));
	
	if (GetType() != InType)
	{
		Type = InType;
		bHasConcreteType = true;

		// if we have a reroute node propagate through to make sure each reroute segment is properly handled
		// IMPORTANT : this needs to be done before we propagate through the input connections
		if (const FDataflowReRouteNode* const ReRouteNode = OwningNode->AsType<FDataflowReRouteNode>())
		{
			for (FDataflowOutput* const ReRouteOutput : ReRouteNode->GetOutputs())
			{
				if (ReRouteOutput)
				{
					ReRouteOutput->FixAndPropagateType(InType);
				}
			}
		}

		OwningNode->NotifyConnectionTypeChanged(this);

		// Now propagate to the connected output
		if (FDataflowOutput* const Output = GetConnection())
		{
			Output->FixAndPropagateType(InType);
		}
	}
}

FDataflowArrayInput::FDataflowArrayInput(int32 InIndex, const UE::Dataflow::FArrayInputParameters& Param)
	: FDataflowInput(Param)
	, Index(InIndex)
	, ElementOffset(Param.InnerOffset)
	, ArrayProperty(Param.ArrayProperty)
{}

void* FDataflowArrayInput::RealAddress() const
{
	if (void* ContainerRealAddress = Super::RealAddress())
	{
		if (ArrayProperty)
		{
			if (const void* const AddressAtIndex = ArrayProperty->GetValueAddressAtIndex_Direct(ArrayProperty->Inner, ContainerRealAddress, Index))
			{
				return (void*)((size_t)AddressAtIndex + (size_t)ElementOffset);
			}
		}
	}
	return nullptr;
}

//
//
//  Output
//
//
//


FDataflowOutput::FDataflowOutput(const UE::Dataflow::FOutputParameters& Param, FGuid InGuid)
	: FDataflowConnection(UE::Dataflow::FPin::EDirection::OUTPUT, Param)
{
	Guid = InGuid;
	OutputLock = MakeShared<FCriticalSection>();
}

FDataflowOutput::FDataflowOutput(const UE::Dataflow::FOutputParameters& Param)
	: FDataflowConnection(UE::Dataflow::FPin::EDirection::OUTPUT, Param)
{
	OutputLock = MakeShared<FCriticalSection>();
}

const TArray<FDataflowInput*>& FDataflowOutput::GetConnections() const { return Connections; }
TArray<FDataflowInput*>& FDataflowOutput::GetConnections() { return Connections; }

const TArray< const FDataflowInput*> FDataflowOutput::GetConnectedInputs() const
{
	TArray<const FDataflowInput*> RetList;
	RetList.Reserve(Connections.Num());
	for (FDataflowInput* Ptr : Connections) 
	{ 
		RetList.Add(Ptr); 
	}
	return RetList;
}

TArray< FDataflowInput*> FDataflowOutput::GetConnectedInputs()
{
	TArray<FDataflowInput*> RetList;
	RetList.Reserve(Connections.Num());
	for (FDataflowInput* Ptr : Connections) 
	{ 
		RetList.Add(Ptr); 
	}
	return RetList;
}

bool FDataflowOutput::AddConnection(FDataflowConnection* InOutput)
{
	if (Type == InOutput->GetType())
	{
		Connections.Add((FDataflowInput*)InOutput);
		return true;
	}
	return false;
}

bool FDataflowOutput::RemoveConnection(FDataflowConnection* InInput)
{
	Connections.RemoveSwap((FDataflowInput*)InInput); return true;
}

FDataflowOutput& FDataflowOutput::SetPassthroughInput(const UE::Dataflow::FConnectionReference& Reference)
{
	check(OwningNode);
	const FDataflowInput* const PassthroughInput = OwningNode->FindInput(Reference);
	check(PassthroughInput);
	PassthroughKey = PassthroughInput->GetConnectionKey();
	return *this;
}

FDataflowOutput& FDataflowOutput::SetPassthroughInput(const UE::Dataflow::FConnectionKey& Key)
{
	check(Key == UE::Dataflow::FConnectionKey::Invalid || !OwningNode || OwningNode->FindInput(Key));
	PassthroughKey = Key;
	return *this;
}

const FDataflowInput* FDataflowOutput::GetPassthroughInput() const
{
	return OwningNode ? OwningNode->FindInput(PassthroughKey) : nullptr;
}

void FDataflowOutput::Invalidate(const UE::Dataflow::FTimestamp& ModifiedTimestamp)
{
	for (FDataflowConnection* Con : GetConnections())
	{
		Con->Invalidate(ModifiedTimestamp);
	}
}

bool FDataflowOutput::Evaluate(UE::Dataflow::FContext& Context) const
{
	check(OwningNode);

	if (IsOwningNodeEnabled())
	{
		return Context.Evaluate(*this);
	}
	else if (const FDataflowInput* const PassthroughInput = GetPassthroughInput())
	{
		ForwardInput(PassthroughInput, Context);
		return true;
	}

	return false;
}

bool FDataflowOutput::EvaluateImpl(UE::Dataflow::FContext& Context) const
{
	UE_LOG(LogChaosDataflow, VeryVerbose, TEXT("FDataflowOutput::EvaluateImpl(): Node [%s], Output [%s]"), *GetOwningNode()->GetName().ToString(), *GetName().ToString());

	UE::Dataflow::FContextScopedCallstack Callstack(Context, this);
	if (Callstack.IsLoopDetected())
	{ 
		ensureMsgf(false, TEXT("Connection %s is already in the callstack, this is certainly because of a loop in the graph"), *GetName().ToString());
		return false;
	}

	// check if the cache has a valid version
	if(Context.HasData(CacheKey(), OwningNode->GetTimestamp()))
	{
		UE_LOG(LogChaosDataflow, VeryVerbose, TEXT("FDataflowOutput::EvaluateImpl(): Context has data, NodeTimestamp [%ul], CacheTimestamp [%lu]"), OwningNode->GetTimestamp().Value, Context.GetTimestamp(CacheKey()).Value);
		return true;
	}
	// if not, evaluate
	UE_LOG(LogChaosDataflow, Verbose, TEXT("FDataflowNode::Evaluate(): Node [%s], Output [%s], NodeTimestamp [%lu]"), *GetOwningNode()->GetName().ToString(), *GetName().ToString(), OwningNode->GetTimestamp().Value);
	OwningNode->Evaluate(Context, this);
	// Validation
	if (!Context.HasData(CacheKey()))
	{
		ensureMsgf(false, TEXT("Failed to evaluate output (%s:%s)"), *OwningNode->GetName().ToString(), *GetName().ToString());
		return false;
	}

	return true;
}

TFuture<bool> FDataflowOutput::EvaluateParallel(UE::Dataflow::FContext& Context) const
{
	return Async(EAsyncExecution::TaskGraph, [&]() -> bool { return this->Evaluate(Context); });
}


void FDataflowOutput::ForwardInput(const UE::Dataflow::FConnectionReference& InputReference, UE::Dataflow::FContext& Context) const
{
	if (Property && OwningNode)
	{
		const FDataflowInput* InputToForward = OwningNode->FindInput(InputReference);
		ForwardInput(InputToForward, Context);
	}
}

void FDataflowOutput::ForwardInput(const FDataflowInput* Input, UE::Dataflow::FContext& Context) const
{
	if (Property && OwningNode)
	{
		if (Input->GetConnectedOutputs().Num())
		{
			ensure(Input->GetType() == GetType());
			ensure(Input->GetConnectedOutputs().Num() == 1);
			if (const FDataflowOutput* ConnectionOut = Input->GetConnection())
			{
				Input->PullValue(Context);
				Context.SetDataReference(CacheKey(), Property, ConnectionOut->CacheKey());
			}
		}
		else
		{
			// if there's no connection we make a invalid reference 
			// so when the input is going to pull the cached value , it will return a default value instead
			Context.SetNullData(CacheKey(), Property, GetOwningNodeGuid(), GetOwningNodeValueHash(), GetOwningNodeTimestamp());
		}
	}
}

void FDataflowOutput::FixAndPropagateType(FName InType)
{
	check(InType.ToString().StartsWith(Type.ToString()));
	check(!FDataflowConnection::IsAnyType(InType));

	if (Type != InType)
	{
		Type = InType;
		bHasConcreteType = true;

		// if we have a reroute node propagate through to make sure each reroute segment is properly handled
		// IMPORTANT : this needs to be done before we propagate through the output connections
		if (const FDataflowReRouteNode* const ReRouteNode = OwningNode->AsType<FDataflowReRouteNode>())
		{
			for (FDataflowInput* const RerouteInput : ReRouteNode->GetInputs())
			{
				if (RerouteInput)
				{
					RerouteInput->FixAndPropagateType(InType);
				}
			}
		}

		OwningNode->NotifyConnectionTypeChanged(this);

		// Now propagate through the connected inputs
		for (FDataflowInput* const Input : Connections)
		{
			if (Input)
			{
				Input->FixAndPropagateType(InType);
			}
		}
	}
}