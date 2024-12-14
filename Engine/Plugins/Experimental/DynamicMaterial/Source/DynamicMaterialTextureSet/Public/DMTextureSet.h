// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "Containers/EnumAsByte.h"
#include "Containers/Map.h"
#include "DMMaterialTexture.h"

#include "DMTextureSet.generated.h"

enum class EDMTextureSetMaterialProperty : uint8;

UCLASS(BlueprintType, ClassGroup = "Material Designer")
class DYNAMICMATERIALTEXTURESET_API UDMTextureSet : public UObject
{
	GENERATED_BODY()

public:
	UDMTextureSet();

	virtual ~UDMTextureSet() override = default;

	/**
	 * Checks whether a given Material Property exists in the Texture Map. Does not check whether
	 * that a Texture is assigned to it.
	 * @param InMaterialProperty The Material Property to check.
	 * @return True if the property exists in the Texture Map.
	 */
	UFUNCTION(BlueprintPure, Category = "Material Designer")
	bool HasMaterialProperty(EDMTextureSetMaterialProperty InMaterialProperty) const;

	/**
	 * @return Gets the entire Texture Map.
	 */
	const TMap<EDMTextureSetMaterialProperty, FDMMaterialTexture>& GetTextures() const;

	/**
	 * Checks whether a given Material Property has a Texture assigned to it.
	 * @param InMaterialProperty The Material Property to check.
	 * @return True if the Material Property has an assigned Texture.
	 */
	UFUNCTION(BlueprintPure, Category = "Material Designer")
	bool HasMaterialTexture(EDMTextureSetMaterialProperty InMaterialProperty) const;

	/**
	 * Gets the Material Texture associated with a Material Property. Does not check whether a Texture
	 * is assigned to it.
	 * @param InMaterialProperty The Material Property to check.
	 * @param OutMaterialTexture The found Material Texture.
	 * @return True if the Material Property exists within the Texture Map.
	 */
	UFUNCTION(BlueprintPure, Category = "Material Designer")
	bool GetMaterialTexture(EDMTextureSetMaterialProperty InMaterialProperty, FDMMaterialTexture& OutMaterialTexture) const;

	/**
	 * Gets the Material Texture associated with a Material Property. Does not check whether a Texture
	 * is assigned to it.
	 * @param InMaterialProperty The Material Property to check.
	 * @return A pointer to the found Material Texture, if it exists, or nullptr.
	 */
	const FDMMaterialTexture* GetMaterialTexture(EDMTextureSetMaterialProperty InMaterialProperty) const;

	/**
	 * Sets the Material Texture for a given Material Property. Can be used to unset Textures.
	 * @param InMaterialProperty The Material Property to check.
	 * @param InMaterialTexture The Material Texture to set on the given Material Property.
	 */
	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	void SetMaterialTexture(EDMTextureSetMaterialProperty InMaterialProperty, const FDMMaterialTexture& InMaterialTexture);

	/**
	 * Checks the Texture Map to see if a given Texture exists within it.
	 * @param InTexture The Texture to search for.
	 * @return True if the Texture exits in the Texture Map.
	 */
	UFUNCTION(BlueprintPure, Category = "Material Designer")
	bool ContainsTexture(UTexture* InTexture) const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Designer", EditFixedSize, meta = (ReadOnlyKeys, AllowPrivateAccess))
	TMap<EDMTextureSetMaterialProperty, FDMMaterialTexture> Textures;
};
