// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationSystem/NetObjectFactoryRegistry.h"

#include "Iris/ReplicationSystem/NetObjectFactory.h"
#include "Iris/Core/IrisLog.h"

namespace UE::Net::Private
{
	bool bIsFactoryRegistrationAllowed = true;
} // end namespace UE::Net::Private

namespace UE::Net 
{

TArray<FNetObjectFactoryRegistry::FFactoryData, TFixedAllocator<FNetObjectFactoryRegistry::MaxFactories>>  FNetObjectFactoryRegistry::NetFactories;

void FNetObjectFactoryRegistry::SetFactoryRegistrationAllowed(bool bAllowed)
{
	UE::Net::Private::bIsFactoryRegistrationAllowed = bAllowed;
}

void FNetObjectFactoryRegistry::RegisterFactory(UClass* FactoryClass, FName FactoryName)
{
	using namespace UE::Net::Private;

#if UE_WITH_IRIS

	// Factories cannot be modified while Iris NetDrivers exist
	if (!bIsFactoryRegistrationAllowed)
	{
		UE_LOG(LogIris, Error, TEXT("FNetObjectFactoryRegistry::RegisterFactory cannot register factory: %s name: %s because it was called while Iris replication was already started"), *GetNameSafe(FactoryClass), *FactoryName.ToString());
		check(bIsFactoryRegistrationAllowed);
		return;
	}

	check(FactoryClass);

	if (FactoryName.IsNone())
	{
		checkf(!FactoryName.IsNone(), TEXT("FNetObjectFactoryRegistry::RegisterFactory cannot register %s due to invalid name"), *GetNameSafe(FactoryClass));
		return;
	}

	if (NetFactories.Num() >= MaxFactories)
	{
		UE_LOG(LogIris, Error, TEXT("FNetObjectFactoryRegistry::RegisterFactory already has %u factories registered. Cannot register factory: %s with name: %s"), MaxFactories, *GetNameSafe(FactoryClass), *FactoryName.ToString());
		ensure(false);
		return;
	}

	// Make sure the name is unique
	for (const FFactoryData& FactoryData : NetFactories)
	{
		if (FactoryData.Name == FactoryName)
		{
			UE_LOG(LogIris, Error, TEXT("FNetObjectFactoryRegistry::RegisterFactory cannot register factory: %s with name: %s. This name is already used by factory: %s id: %u"), *GetNameSafe(FactoryClass), *FactoryName.ToString(), *GetNameSafe(FactoryData.NetFactoryClass), FactoryData.Id);
			ensure(false);
			return;
		}
	}

	// Make sure the class is of the correct type
	if (!FactoryClass->IsChildOf<UNetObjectFactory>())
	{
		checkf(FactoryClass->IsChildOf<UNetObjectFactory>(), TEXT("FNetObjectFactoryRegistry::RegisterFactory factory: %s name: %s is not derived from UNetObjectFactory."), *GetNameSafe(FactoryClass), *FactoryName.ToString());
		return;
	}
	
	const int32 Index = NetFactories.Emplace( 
		FFactoryData 
		{
			.Name = FactoryName,
			.NetFactoryClass = FactoryClass
		});
	NetFactories[Index].Id = IntCastChecked<FNetObjectFactoryId>(Index);

	UE_LOG(LogIris, Log, TEXT("FNetObjectFactoryRegistry::UnregisterFactory is unregistering factory: %s name: %s id: %u"), *GetNameSafe(NetFactories[Index].NetFactoryClass), *NetFactories[Index].Name.ToString(), NetFactories[Index].Id);
#endif
}

void FNetObjectFactoryRegistry::UnregisterFactory(FName FactoryName)
{
	using namespace UE::Net::Private;
#if UE_WITH_IRIS
	
	// Factories cannot be modified while Iris NetDrivers exist
	if (!bIsFactoryRegistrationAllowed)
	{
		UE_LOG(LogIris, Error, TEXT("FNetObjectFactoryRegistry::UnregisterFactory cannot unregister factory name: %s because it was called while Iris replication was already started"), *FactoryName.ToString());
		check(bIsFactoryRegistrationAllowed);
		return;
	}

	for (int32 Index=0; Index < NetFactories.Num(); ++Index)
	{
		const FFactoryData& Data = NetFactories[Index];
		if (Data.Name == FactoryName)
		{
			UE_LOG(LogIris, Log, TEXT("FNetObjectFactoryRegistry::UnregisterFactory is unregistering factory: %s name: %s id: %u"), *GetNameSafe(Data.NetFactoryClass), *Data.Name.ToString(), Data.Id);
			NetFactories.RemoveAt(Index, EAllowShrinking::No);
			return;
		}
	}

	UE_LOG(LogIris, Error, TEXT("FNetObjectFactoryRegistry::UnregisterFactory could not find any factories using name: %s"), *FactoryName.ToString());
	ensure(false);
#endif
}
	  
FNetObjectFactoryId FNetObjectFactoryRegistry::GetFactoryIdFromName(FName FactoryName)
{
#if UE_WITH_IRIS
	for (const FFactoryData& Data : NetFactories)
	{
		if (Data.Name == FactoryName)
		{
			return Data.Id;
		}
	}
#endif

	return InvalidNetObjectFactoryId;
}

} // end namespace UE::Net