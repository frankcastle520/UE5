// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/InterchangeAnimationPayloadInterface.h"
#include "InterchangeTranslatorBase.h"
#include "Mesh/InterchangeMeshPayloadInterface.h"
#include "Texture/InterchangeBlockedTexturePayloadInterface.h"
#include "Texture/InterchangeTexturePayloadInterface.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "USDStageOptions.h"

#include "InterchangeUsdTranslator.generated.h"

enum class EUsdInterpolationType : uint8;
namespace UE::InterchangeUsdTranslator::Private
{
	class UInterchangeUSDTranslatorImpl;
}

UCLASS(BlueprintType, editinlinenew, MinimalAPI)
class UInterchangeUsdTranslatorSettings : public UInterchangeTranslatorSettings
{
	GENERATED_BODY()

public:

	/**
	 * TODO: Most (if not all) of these settings should probably be specific to the pipeline, and not the
	 * translator, and will be moved there whenever we do make a USD pipeline.
	 *
	 * For example: Instead of filtering prims based on purpose on translation, we should emit all of the translated
	 * nodes and filter only later on the pipeline, as translating the scene should be fast either way. That way users
	 * can even customize/disable that behavior if they want to, in order to use their own pipelines.
	 *
	 * It's not clear what to do about StageOptions or RenderContext though: Maybe these should be here, as they
	 * actively affect how we translate the scene? (e.g. the generated Interchange Material node for a Material
	 * prim will be very different whether we use "universal", "unreal" or "mtlx" render contexts...)
	 */

	/** Only import geometry prims with these specific purposes from the USD file */
	UPROPERTY(EditAnywhere, Category = "USD Translator", meta = (Bitmask, BitmaskEnum = "/Script/UnrealUSDWrapper.EUsdPurpose"))
	int32 GeometryPurpose;

	/** Specifies which set of shaders to use when parsing USD materials, in addition to the universal render context. */
	UPROPERTY(EditAnywhere, Category = "USD Translator")
	FName RenderContext;

	/** Specifies which material purpose to use when parsing USD material bindings, in addition to the "allPurpose" fallback */
	UPROPERTY(EditAnywhere, Category = "USD Translator")
	FName MaterialPurpose;

	/** Describes how to interpolate between a timeSample value and the next */
	UPROPERTY(EditAnywhere, Category = "USD Translator")
	EUsdInterpolationType InterpolationType;

	/** Whether to use the specified StageOptions instead of the stage's own settings */
	UPROPERTY(EditAnywhere, Category = "USD Translator")
	bool bOverrideStageOptions;

	/** Custom StageOptions to use for the stage */
	UPROPERTY(EditAnywhere, Category = "USD Translator", meta = (EditCondition = bOverrideStageOptions))
	FUsdStageOptions StageOptions;

public:
	UInterchangeUsdTranslatorSettings();
};

/* For now, USD Interchange (FBX parity) translator supports textures, materials and static meshes */
UCLASS(BlueprintType)
class UInterchangeUSDTranslator
	: public UInterchangeTranslatorBase
	, public IInterchangeMeshPayloadInterface
	, public IInterchangeTexturePayloadInterface
	, public IInterchangeBlockedTexturePayloadInterface
	, public IInterchangeAnimationPayloadInterface
{
	GENERATED_BODY()

public:
	UInterchangeUSDTranslator();

	/** Begin UInterchangeTranslatorBase API*/
	virtual EInterchangeTranslatorType GetTranslatorType() const override;
	virtual EInterchangeTranslatorAssetType GetSupportedAssetTypes() const override;
	virtual TArray<FString> GetSupportedFormats() const override;
	virtual bool Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const override;
	virtual void ReleaseSource() override;
	virtual UInterchangeTranslatorSettings* GetSettings() const override;
	virtual void SetSettings(const UInterchangeTranslatorSettings* InterchangeTranslatorSettings) override;
	/** End UInterchangeTranslatorBase API*/

	TFuture<TOptional<UE::Interchange::FAnimationPayloadData>> ResolveAnimationPayloadQuery(
		const UE::Interchange::FAnimationPayloadQuery& PayloadQuery
	) const;

	/** Begin Interchange payload interfaces */
	virtual TOptional<UE::Interchange::FMeshPayloadData> GetMeshPayloadData(
		const FInterchangeMeshPayLoadKey& PayLoadKey,
		const FTransform& MeshGlobalTransform
	) const override;

	virtual TOptional<UE::Interchange::FImportImage> GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& AlternateTexturePath)
		const override;

	virtual TOptional<UE::Interchange::FImportBlockedImage> GetBlockedTexturePayloadData(
		const FString& PayloadKey,
		TOptional<FString>& AlternateTexturePath
	) const override;

	virtual TArray<UE::Interchange::FAnimationPayloadData> GetAnimationPayloadData(
		const TArray<UE::Interchange::FAnimationPayloadQuery>& PayloadQueries
	) const override;
	/** End Interchange payload interfaces */

private:
	TUniquePtr<UE::InterchangeUsdTranslator::Private::UInterchangeUSDTranslatorImpl> Impl;

	UPROPERTY(Transient, DuplicateTransient)
	mutable TObjectPtr<UInterchangeUsdTranslatorSettings> TranslatorSettings = nullptr;
};
