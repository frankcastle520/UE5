// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/Iris/ReplicationSystem/NetEngineFactories.h"

#if UE_WITH_IRIS
#include "Net/Iris/ReplicationSystem/NetActorFactory.h"
#include "Net/Iris/ReplicationSystem/NetSubObjectFactory.h"

#include "Iris/ReplicationSystem/NetObjectFactoryRegistry.h"
#endif 

#if UE_WITH_IRIS

namespace UE::Net
{

void InitEngineNetObjectFactories()
{
	FNetObjectFactoryRegistry::RegisterFactory(UNetActorFactory::StaticClass(), UNetActorFactory::GetFactoryName());
	FNetObjectFactoryRegistry::RegisterFactory(UNetSubObjectFactory::StaticClass(), UNetSubObjectFactory::GetFactoryName());
}

void ShutdownEngineNetObjectFactories()
{
	FNetObjectFactoryRegistry::UnregisterFactory(UNetActorFactory::GetFactoryName());
	FNetObjectFactoryRegistry::UnregisterFactory(UNetSubObjectFactory::GetFactoryName());
}

} // end namespace UE::Net

#endif // UE_WITH_IRIS