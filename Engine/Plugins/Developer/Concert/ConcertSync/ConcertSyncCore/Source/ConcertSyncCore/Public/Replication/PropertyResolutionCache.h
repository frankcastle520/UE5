// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Data/ConcertPropertySelection.h"

class UStruct;

namespace UE::ConcertSyncCore::PropertyChain
{
	/**
	 * FConcertPropertyChain::ResolveProperty is expensive because it has to iterate the property hierarchy.
	 * 
	 * This caches look-up results of native classes.
	 * By default, properties in Blueprint classes are not cached because they can change at runtime and leave dangling FProperty pointers.
	 */
	class CONCERTSYNCCORE_API FPropertyResolutionCache
	{
	public:

		/** Resolves the property chain and caches the result; */
		FProperty* ResolveAndCache(const UStruct& Struct, const FConcertPropertyChain& Chain);

		/** Removes the class from the cache.*/
		void Invalidate(const UStruct& Struct) { CachedProperties.Remove(&Struct); }
		/** Removes everything from the cache. */
		void Clear(int32 ExpectedNumElements = 0) { CachedProperties.Empty(ExpectedNumElements); }

	private:

		struct FClassCache
		{
			TMap<FConcertPropertyChain, FProperty*> Cache;
		};

		TMap<const UStruct*, FClassCache> CachedProperties;
	};
}
