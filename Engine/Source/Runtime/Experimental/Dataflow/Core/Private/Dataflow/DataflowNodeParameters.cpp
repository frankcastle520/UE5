// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNodeParameters.h"

#include "Dataflow/DataflowArchive.h"
#include "Dataflow/DataflowContextCachingFactory.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowNode.h"
#include "Serialization/Archive.h"
#include "GeometryCollection/ManagedArrayCollection.h"

namespace UE::Dataflow
{
	uint64 FTimestamp::Invalid = 0;
	uint64 FTimestamp::Current() { return FPlatformTime::Cycles64(); }

	FTimestamp FContext::GetTimestamp(FContextCacheKey Key) const
	{
		if (const TUniquePtr<FContextCacheElementBase>* const Cache = const_cast<FContext*>(this)->GetDataImpl(Key))
		{
			return (*Cache)->GetTimestamp();
		}
		return FTimestamp::Invalid;
	}

	void FContext::PushToCallstack(const FDataflowConnection* Connection)
	{
#if DATAFLOW_EDITOR_EVALUATION
		Callstack.Push(Connection);
#endif
	}

	void FContext::PopFromCallstack(const FDataflowConnection* Connection)
	{
#if DATAFLOW_EDITOR_EVALUATION
		ensure(Connection == Callstack.Top());
		Callstack.Pop();
#endif
	}

	bool FContext::IsInCallstack(const FDataflowConnection* Connection) const
	{
#if DATAFLOW_EDITOR_EVALUATION
		return Callstack.Contains(Connection);
#else
		return false;
#endif
	}

	bool FContext::IsCacheEntryAfterTimestamp(FContextCacheKey InKey, const FTimestamp InTimestamp)
	{
		if (HasData(InKey))
		{
			if (TUniquePtr<FContextCacheElementBase>* CacheEntry = GetDataImpl(InKey))
			{
				if (*CacheEntry)
				{
					if ((*CacheEntry)->GetTimestamp() >= InTimestamp)
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	FContextScopedCallstack::FContextScopedCallstack(FContext& InContext, const FDataflowConnection* InConnection)
		: Context(InContext)
		, Connection(InConnection)
	{
		bLoopDetected = Context.IsInCallstack(Connection);
		Context.PushToCallstack(Connection);
	}

	FContextScopedCallstack::~FContextScopedCallstack()
	{
		Context.PopFromCallstack(Connection);
	}

	void BeginContextEvaluation(FContext& Context, const FDataflowNode* Node, const FDataflowOutput* Output)
	{
		if (Output)
		{
			Context.Evaluate(*Output);
		}
		else if (Node)
		{
			if (Node->NumOutputs())
			{
				for (const FDataflowOutput* const NodeOutput : Node->GetOutputs())
				{
					Context.Evaluate(*NodeOutput);
				}
			}
			// Note: If the node is deactivated and has an output (like above), then the output might still need to be forwarded.
			//       Therefore the Evaluate method has to be called for whichever value of bActive.
			//       However if the node is deactivated and has no outputs (like below), now is the time to check its bActive state.
			else if (Node->bActive)
			{
				// TODO: When no outputs are specified, this call to Evaluate should really be removed.
				//       The purpose of the node evaluation function is to evaluate outputs.
				//       Therefore if a node has no outputs, then it shouldn't need any evaluation.
				UE_LOG(LogChaosDataflow, Verbose, TEXT("FDataflowNode::Evaluate(): Node [%s], Output [nullptr], NodeTimestamp [%lu]"), *Node->GetName().ToString(), Node->GetTimestamp().Value);
				Node->Evaluate(Context, nullptr);
			}
		}
		else
		{
			ensureMsgf(false, TEXT("Invalid arguments, either Node or Output needs to be non null."));
		}
	}

	void FContextSingle::Evaluate(const FDataflowNode* Node, const FDataflowOutput* Output)
	{
		BeginContextEvaluation(*this, Node, Output);
	}

	bool FContextSingle::Evaluate(const FDataflowOutput& Connection)
	{
		UE_LOG(LogChaosDataflow, VeryVerbose, TEXT("FContextSingle::Evaluate(): Node [%s], Output [%s]"), *Connection.GetOwningNode()->GetName().ToString(), *Connection.GetName().ToString());
		return Connection.EvaluateImpl(*this);
	}



	void FContextThreaded::Evaluate(const FDataflowNode* Node, const FDataflowOutput* Output)
	{
		BeginContextEvaluation(*this, Node, Output);
	}

	bool FContextThreaded::Evaluate(const FDataflowOutput& Connection)
	{
		UE_LOG(LogChaosDataflow, VeryVerbose, TEXT("FContextThreaded::Evaluate(): Node [%s], Output [%s]"), *Connection.GetOwningNode()->GetName().ToString(), *Connection.GetName().ToString());
		Connection.OutputLock->Lock(); ON_SCOPE_EXIT{ Connection.OutputLock->Unlock(); };
		return Connection.EvaluateImpl(*this);
	}


	void FContextCache::Serialize(FArchive& Ar)
	{

		if (Ar.IsSaving())
		{

			const int64 NumElementsSavedPosition = Ar.Tell();
			int64 NumElementsWritten = 0;
			Ar << NumElementsWritten;

			for (TPair<FContextCacheKey, TUniquePtr<FContextCacheElementBase>>& Elem : Pairs)
			{
				// note : we only serialize typed cache element and ignore the reference ones ( since they don't hold data per say )
				// Also UObject pointers aren't serialized, as there are no ways to differentiate the objects owned by the cache 
				// from the ones own by any other owners for now.
				if (Elem.Value && Elem.Value->Property && Elem.Value->Type == FContextCacheElementBase::EType::CacheElementTyped)
				{
					FProperty* Property = (FProperty*)Elem.Value->Property;
					FString ExtendedType;
					const FString CPPType = Property->GetCPPType(&ExtendedType);
					FName TypeName(CPPType + ExtendedType);
					FGuid NodeGuid = Elem.Value->NodeGuid;
					uint32 NodeHash = Elem.Value->NodeHash;

					if (FContextCachingFactory::GetInstance()->Contains(TypeName))
					{
						Ar << TypeName << Elem.Key << NodeGuid << NodeHash << Elem.Value->Timestamp;

						DATAFLOW_OPTIONAL_BLOCK_WRITE_BEGIN()
						{
							FContextCachingFactory::GetInstance()->Serialize(Ar, {TypeName, NodeGuid, Elem.Value.Get(), NodeHash, Elem.Value->Timestamp});
						}
						DATAFLOW_OPTIONAL_BLOCK_WRITE_END();

						NumElementsWritten++;
					}
				}
			}


			if (NumElementsWritten)
			{
				const int64 FinalPosition = Ar.Tell();
				Ar.Seek(NumElementsSavedPosition);
				Ar << NumElementsWritten;
				Ar.Seek(FinalPosition);
			}
		}
		else if (Ar.IsLoading())
		{
			int64 NumElementsWritten = 0;
			Ar << NumElementsWritten;
			for (int i = NumElementsWritten; i > 0; i--)
			{
				FName TypeName;
				FGuid NodeGuid;
				uint32 NodeHash;
				FContextCacheKey InKey;
				FTimestamp Timestamp = FTimestamp::Invalid;

				Ar << TypeName << InKey << NodeGuid << NodeHash << Timestamp;

				DATAFLOW_OPTIONAL_BLOCK_READ_BEGIN(FContextCachingFactory::GetInstance()->Contains(TypeName))
				{
					FContextCacheElementBase* NewElement = FContextCachingFactory::GetInstance()->Serialize(Ar, { TypeName, NodeGuid, nullptr, NodeHash, Timestamp });
					check(NewElement);
					NewElement->NodeGuid = NodeGuid;
					NewElement->NodeHash = NodeHash;
					NewElement->Timestamp = Timestamp;
					this->Add(InKey, TUniquePtr<FContextCacheElementBase>(NewElement));
				}
				DATAFLOW_OPTIONAL_BLOCK_READ_ELSE()
				{
				}
				DATAFLOW_OPTIONAL_BLOCK_READ_END();
			}
		}
	}
};

