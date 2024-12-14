// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "UObject/Object.h"

#include "AssetDefinitionRegistry.generated.h"

class UAssetDefinition;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssetDefinitionRegistryVersionChange, UAssetDefinitionRegistry*);

UCLASS(config=Editor)
class ASSETDEFINITION_API UAssetDefinitionRegistry : public UObject
{
	GENERATED_BODY()

public:	
	static UAssetDefinitionRegistry* Get();

	UAssetDefinitionRegistry();
	
	virtual void BeginDestroy() override;

	const UAssetDefinition* GetAssetDefinitionForAsset(const FAssetData& Asset) const;
	const UAssetDefinition* GetAssetDefinitionForClass(const UClass* Class) const;

	// Gets the current version of the AssetDefinitions.   Version is updated whenever an AssetDefinition is Registered/Unregistered
	uint64 GetAssetDefinitionVersion() const;
	TArray<TObjectPtr<UAssetDefinition>> GetAllAssetDefinitions() const;
	TArray<TSoftClassPtr<UObject>> GetAllRegisteredAssetClasses() const;

	/**
	 * Normally UAssetDefinitionRegistry are registered automatically by their CDO.  The only reason you need to do this is if
	 * you're forced to dynamically create the UAssetDefinition at runtime.  The original reason for this function was
	 * to be able to create wrappers for the to be replaced IAssetTypeActions, that you can access AssetDefinition
	 * versions of any IAssetType making the upgrade easier.
	 */
	void RegisterAssetDefinition(UAssetDefinition* AssetDefinition);

	void UnregisterAssetDefinition(UAssetDefinition* AssetDefinition);

	/**
	 * Called when the AssetDefinitionRegistry's version has changed.
	 */
	FOnAssetDefinitionRegistryVersionChange& OnAssetDefinitionRegistryVersionChange();
	
private:
	void RegisterTickerForVersionNotification();
	bool TickVersionNotification(float);
	
	static UAssetDefinitionRegistry* Singleton;
	static bool bHasShutDown;

	UPROPERTY()
	TMap<TSoftClassPtr<UObject>, TObjectPtr<UAssetDefinition>> AssetDefinitions;

	uint64 Version;

	FTickerDelegate TickerDelegate;
	FTSTicker::FDelegateHandle TickerDelegateHandle;
	FOnAssetDefinitionRegistryVersionChange OnAssetDefinitionRegistryVersionChangeDelegate;
};