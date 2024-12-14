// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dataflow/DataflowNodeParameters.h"
#include "GeometryCollection/ManagedArrayCollection.h"

class UObject;
class FArchive;

namespace UE::Dataflow
{
	template<class Base = FContextSingle>
	class TEngineContext : public Base
	{
	public:
		DATAFLOW_CONTEXT_INTERNAL(Base, TEngineContext);

		explicit TEngineContext(const TObjectPtr<UObject>& InOwner)
			: Base()
			, Owner(InOwner)
		{}

		TObjectPtr<UObject> Owner = nullptr;

		virtual ~TEngineContext() {}

		int32 GetKeys(TSet<FContextCacheKey>& InKeys) const { return Base::GetKeys(InKeys); }

		TUniquePtr<FContextCacheElementBase>* GetBaseData(FContextCacheKey Key) { return Base::GetDataImpl(Key); }

		virtual void Serialize(FArchive& Ar) { Base::Serialize(Ar); }

	};

	typedef TEngineContext<FContextSingle> FEngineContext;
	typedef TEngineContext<FContextThreaded> FEngineContextThreaded;
}

