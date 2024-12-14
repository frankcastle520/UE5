// Copyright Epic Games, Inc. All Rights Reserved.

// NetTokenExports is still an experimental feature that depends on Iris code.
#if UE_WITH_IRIS

#include "GameplayTagTokenStore.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"

namespace UE::Net
{

 FGameplayTagTokenStore::FGameplayTagTokenStore(FNetTokenStore& TokenStore) : FNameTokenStore(TokenStore)
 {
 }

FNetToken FGameplayTagTokenStore::GetOrCreateToken(FGameplayTag Tag)
{ 
	return FNameTokenStore::GetOrCreateToken(Tag.GetTagName());
}

FGameplayTag FGameplayTagTokenStore::ResolveToken(FNetToken Token, const FNetTokenStoreState* RemoteTokenStoreState) const
{
	const FName TagName = FNameTokenStore::ResolveToken(Token, RemoteTokenStoreState);
	return TagName.IsNone() ? FGameplayTag() : UGameplayTagsManager::Get().RequestGameplayTag(TagName);
}

}

#endif // UE_WITH_IRIS