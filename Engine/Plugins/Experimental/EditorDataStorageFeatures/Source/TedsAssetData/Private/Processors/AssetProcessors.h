// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"

#include "AssetProcessors.generated.h"

class IEditorDataStorageProvider;

UCLASS()
class UTedsAssetDataFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UTedsAssetDataFactory() override = default;

	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;
	virtual void PreRegister(IEditorDataStorageProvider& DataStorage) override;
	virtual void PreShutdown(IEditorDataStorageProvider& DataStorage) override;

protected:

	void OnSetFolderColor(const FString& Path, IEditorDataStorageProvider* DataStorage);
};
