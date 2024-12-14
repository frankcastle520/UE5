// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Iris/ReplicationSystem/NetTokenStore.h"

namespace UE::Net
{

class FNameTokenStore : public FNetTokenDataStore
{
	UE_NONCOPYABLE(FNameTokenStore);
public:
	IRISCORE_API explicit FNameTokenStore(FNetTokenStore& TokenStore);

	// Create a NetToken for the provided name
	IRISCORE_API FNetToken GetOrCreateToken(FName Name);

	// Resolve NetToken, to resolve remote tokens RemoteTokenStoreState must be valid
	IRISCORE_API FName ResolveToken(FNetToken Token, const FNetTokenStoreState* RemoteTokenStoreState = nullptr) const;	

	static FName GetTokenStoreName()
	{ 
		return NameTokenStoreName;
	}

protected:
	// Serialize data for a token, note there is not validation in this function
	IRISCORE_API virtual void WriteTokenData(FNetSerializationContext& Context, FNetTokenStoreKey TokenStoreKey) const override;
	IRISCORE_API virtual void WriteTokenData(FArchive& Archive, FNetTokenStoreKey TokenStoreKey) const override;

	// Read data for a token, returns a valid StoreKey if successful read
	IRISCORE_API virtual FNetTokenStoreKey ReadTokenData(FNetSerializationContext& Context, const FNetToken& NetToken) override;
	IRISCORE_API virtual FNetTokenStoreKey ReadTokenData(FArchive& Archive, const FNetToken& NetToken) override;

	// Create and store data for Token
	FNetTokenStoreKey GetOrCreateTokenStoreKey(FName Name);

private:

	inline static FName NameTokenStoreName = TEXT("NameTokenStore");

	TMap<FName, FNetTokenStoreKey> FNameToKey;
	TArray<FName> StoredFNames;
};

}
