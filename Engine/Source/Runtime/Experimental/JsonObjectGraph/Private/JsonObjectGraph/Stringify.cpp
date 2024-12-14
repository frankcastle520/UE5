// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonObjectGraph/Stringify.h"
#include "JsonStringifyImpl.h"

FUtf8String UE::JsonObjectGraph::Stringify(TConstArrayView<const UObject*> RootObjects, const FJsonStringifyOptions& Options)
{
	UE::Private::FJsonStringifyImpl Impl(RootObjects, Options);
	return Impl.ToJson();
}
