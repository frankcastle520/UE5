// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/Object.h"
#include "CoreTypes.h"
#include "Math/MathFwd.h"
#include "ClothAssetInteractor.generated.h"

namespace Chaos::Softs
{
class FCollectionPropertyFacade;
}

UCLASS(BlueprintType, MinimalAPI)
class UChaosClothAssetInteractor : public UObject
{
	GENERATED_BODY()
public:

	/** Set properties this interactor references.*/
	CHAOSCLOTHASSETENGINE_API void SetProperties(const TArray<TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade>>& InCollectionPropertyFacades);

	/** Empty references to all properties.*/
	CHAOSCLOTHASSETENGINE_API void ResetProperties();

	/** Generate a list of all properties held by this interactor. Properties for all LODs will be returned if LODIndex = -1.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API TArray<FString> GetAllProperties(int32 LODIndex = -1) const;

	/** Get the value for a property cast to float. DefaultValue will be returned if the property is not found.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API float GetFloatValue(const FString& PropertyName, int32 LODIndex = 0, float DefaultValue = 0.f) const;

	/** Get the low value for a weighted property value (same as GetFloatValue). DefaultValue will be returned if the property is not found.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API float GetLowFloatValue(const FString& PropertyName, int32 LODIndex = 0, float DefaultValue = 0.f) const;

	/** Get the high value for a weighted property value. DefaultValue will be returned if the property is not found.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API float GetHighFloatValue(const FString& PropertyName, int32 LODIndex = 0, float DefaultValue = 0.f) const;

	/** Get the low and high values for a weighted property value. DefaultValue will be returned if the property is not found.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API FVector2D GetWeightedFloatValue(const FString& PropertyName, int32 LODIndex = 0, FVector2D DefaultValue = FVector2D(0., 0.)) const;

	/** Get the value for a property cast to int. DefaultValue will be returned if the property is not found.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API int32 GetIntValue(const FString& PropertyName, int32 LODIndex = 0, int32 DefaultValue = 0) const;

	/** Get the value for a property cast to vector. DefaultValue will be returned if the property is not found.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API FVector GetVectorValue(const FString& PropertyName, int32 LODIndex = 0, FVector DefaultValue = FVector(0.)) const;

	/** Get the string value for a property (typically the associated map name for weighted values). DefaultValue will be returned if the property is not found.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API FString GetStringValue(const FString& PropertyName, int32 LODIndex = 0, const FString& DefaultValue = "") const;

	/**Set the value for a property (if it exists). This sets the Low and High values for weighted values. All LODs will be set when LODIndex = -1.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API void SetFloatValue(const FString& PropertyName, int32 LODIndex = -1, float Value = 0.f);

	/**Set the low value for a weighted property (if it exists). All LODs will be set when LODIndex = -1.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API void SetLowFloatValue(const FString& PropertyName, int32 LODIndex = -1, float Value = 0.f);

	/**Set the high value for a weighted property (if it exists). All LODs will be set when LODIndex = -1.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API void SetHighFloatValue(const FString& PropertyName, int32 LODIndex = -1, float Value = 0.f);

	/**Set the low and high values for a weighted property (if it exists). All LODs will be set when LODIndex = -1.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API void SetWeightedFloatValue(const FString& PropertyName, int32 LODIndex = -1, FVector2D Value = FVector2D(0., 0.));

	/**Set the value for a property (if it exists). All LODs will be set when LODIndex = -1.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API void SetIntValue(const FString& PropertyName, int32 LODIndex = -1, int32 Value = 0);

	/**Set the value for a property (if it exists). All LODs will be set when LODIndex = -1.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API void SetVectorValue(const FString& PropertyName, int32 LODIndex = -1, FVector Value = FVector(0.));

	/**Set the string value for a property (if it exists). This is typically the map name associated with a property. All LODs will be set when LODIndex = -1.*/
	UFUNCTION(BlueprintCallable, Category = ClothProperty)
	CHAOSCLOTHASSETENGINE_API void SetStringValue(const FString& PropertyName, int32 LODIndex = -1, const FString& Value = "");

private:
	TArray<TWeakPtr<::Chaos::Softs::FCollectionPropertyFacade>> CollectionPropertyFacades;
};