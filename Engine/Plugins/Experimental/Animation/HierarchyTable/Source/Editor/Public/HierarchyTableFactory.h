// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "HierarchyTableType.h"

#include "HierarchyTableFactory.generated.h"

class UScriptStruct;

UCLASS()
class UHierarchyTableFactory : public UFactory
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<class USkeleton>	Skeleton;

	const UScriptStruct* TableType;

public:
	UHierarchyTableFactory();

	// UFactory interface
	UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ConfigureProperties() override;

private:
	void OnSkeletonSelected(const FAssetData& SelectedAsset);

	TSharedPtr<class SWindow> PickerWindow;
};
