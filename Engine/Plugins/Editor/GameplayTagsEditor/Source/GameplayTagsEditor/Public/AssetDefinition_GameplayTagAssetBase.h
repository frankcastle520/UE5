// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDefinitionDefault.h"

#include "AssetDefinition_GameplayTagAssetBase.generated.h"

struct FToolMenuSection;
struct FGameplayTagContainer;

/** Base asset type actions for any classes with gameplay tagging */
UCLASS(Abstract)
class GAMEPLAYTAGSEDITOR_API UAssetDefinition_GameplayTagAssetBase : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	UAssetDefinition_GameplayTagAssetBase() {};

	/** Traditionally these are implemented in a MenuExtension namespace. However, UAssetDefinition_GameplayTagAssetBase is an abstract class,
	* and the derived classes need to invoke this in their static MenuExtension functions**/
	static void AddGameplayTagsEditMenuExtension(FToolMenuSection& InSection, TArray<UObject*> InObjects, const FName& OwnedGameplayTagPropertyName);

	// UAssetDefinition Begin
	/** Overridden to specify misc category */
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override
	{
		static const auto Categories = { EAssetCategoryPaths::Misc };
		return Categories;
	}
	// UAssetDefinition End

private:
	/**
	 * Open the gameplay tag editor
	 *
	 * @param TagAssets	Assets to open the editor with
	 */
	static void OpenGameplayTagEditor(TArray<UObject*> Objects, TArray<FGameplayTagContainer> Containers, const FName& OwnedGameplayTagPropertyName);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
