// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/UObjectGlobals.h"

#include "TedsTypedElementActorHandleFactory.generated.h"

class UTypedElementRegistry;

/**
 * This class is responsible for acquiring and registering Actor Typed Element Handles
 * with TEDS/
 */
UCLASS(Transient)
class UTypedElementActorHandleDataStorageFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()
public:

	virtual void PreRegister(IEditorDataStorageProvider& DataStorage) override;
	virtual void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;
	virtual void PreShutdown(IEditorDataStorageProvider& DataStorage) override;
private:
	virtual void RegisterQuery_ActorHandlePopulate(IEditorDataStorageProvider& DataStorage);
	void HandleBridgeEnabled(bool bEnabled);

	FDelegateHandle BridgeEnableDelegateHandle;
	UE::Editor::DataStorage::QueryHandle ActorHandlePopulateQuery = UE::Editor::DataStorage::InvalidQueryHandle;
	UE::Editor::DataStorage::QueryHandle GetAllActorsQuery = UE::Editor::DataStorage::InvalidQueryHandle;
};


