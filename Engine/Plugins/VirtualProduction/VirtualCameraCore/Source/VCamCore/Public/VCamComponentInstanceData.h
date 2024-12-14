// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/SceneComponent.h"
#include "LiveLinkTypes.h"
#include "Modifier/ModifierStackEntry.h"
#include "VCamComponentInstanceData.generated.h"

class UInputMappingContext;
class UVCamComponent;
class UVCamOutputProviderBase;

/** Saves internal UVCamComponent state for Blueprint created components. */
USTRUCT()
struct VCAMCORE_API FVCamComponentInstanceData : public FSceneComponentInstanceData
{
	GENERATED_BODY()
	
	FVCamComponentInstanceData() = default;
	FVCamComponentInstanceData(const UVCamComponent* SourceComponent);
	
	virtual bool ContainsData() const override;
	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	/**
	 * These output providers are renamed and re-outered to the component they're applied to.
	 * The owning UVCamComponent makes sure to not reference the old instances in OnComponentDestroyed.
	 */
	TArray<TObjectPtr<UVCamOutputProviderBase>> StolenOutputProviders;
	/**
	 * These output providers are renamed and re-outered to the component they're applied to.
	 * The owning UVCamComponent makes sure to not reference the old instances in OnComponentDestroyed.
	 */
	TArray<FModifierStackEntry> StolenModifiers;

	/** Simple copy for carrying over player remappings */
	TArray<TObjectPtr<UInputMappingContext>> AppliedInputContexts;
	
	/** The subject name would be lost when the component is reconstructed so keep track of it. */
	FLiveLinkSubjectName LiveLinkSubject;

	/** The old value of bIsInitialized */
	bool bWasInitialized = false;
	/** The old value of bHasInitedModifiers */
	bool bWereModifiersInitialized = false;
	/** The old value of bHasInitedOutputProviders */
	bool bWereOutputProvidersInitialized = false;
	
};
