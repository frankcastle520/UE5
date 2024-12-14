// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "RenderResource.h"
#include "TextureCollection.generated.h"

class FTextureResource;
class UTexture;
class UTextureCollection;

struct FTextureCollectionResource : public FRenderResource
{
	FTextureCollectionResource(UTextureCollection* InParent);

	// FRenderResource Interface
	virtual void InitRHI(FRHICommandListBase& RHICmdList) final;
	virtual void ReleaseRHI() final;
	// ~FRenderResource

	FRHIResourceCollection* GetRHI() const { return ResourceCollectionRHI.GetReference(); }

private:
	TArray<FTextureResource*>       InputTextureResources;
	TArray<FTextureReferenceRHIRef> InputTextures;
	FRHIResourceCollectionRef       ResourceCollectionRHI;
};

UCLASS(MinimalAPI)
class UTextureCollection : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category=TextureCollection)
	TArray<TObjectPtr<UTexture>> Textures;

	//~ Begin UObject Interface.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	virtual void PostLoad() override;
	//~ End UObject Interface.

	ENGINE_API void SetResource(FTextureCollectionResource* InResource);
	ENGINE_API FTextureCollectionResource* GetResource() const;
	ENGINE_API FTextureCollectionResource* GetResource();

	ENGINE_API FTextureCollectionResource* CreateResource();
	ENGINE_API void ReleaseResource();
	ENGINE_API void UpdateResource();

protected:
#if WITH_EDITOR
	void NotifyMaterials();
#endif

	FTextureCollectionResource* PrivateResource = nullptr;
	FTextureCollectionResource* PrivateResourceRenderThread = nullptr;
};
