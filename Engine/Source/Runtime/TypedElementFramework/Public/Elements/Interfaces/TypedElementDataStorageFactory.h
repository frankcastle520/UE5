// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementDataStorageFactory.generated.h"

class IEditorDataStorageCompatibilityProvider;
class IEditorDataStorageProvider;
class IEditorDataStorageUiProvider;

/**
 * Base class that can be used to register various elements, such as queries and widgets, with
 * the Editor Data Storage.
 */
UCLASS(MinimalAPI)
class UEditorDataStorageFactory : public UObject
{
	GENERATED_BODY()

public:
	~UEditorDataStorageFactory() override = default;

	/** 
	 * Returns the order registration will be executed. Factories with a lower number will be executed
	 * before factories with a higher number.
	 */
	virtual uint8 GetOrder() const { return 127; }

	/**
	 * All factories will have this called before any Register functions on any factories are called
	 */
	virtual void PreRegister(IEditorDataStorageProvider& DataStorage) {}

	virtual void RegisterTables(IEditorDataStorageProvider& DataStorage) {}
	virtual void RegisterTables(IEditorDataStorageProvider& DataStorage, IEditorDataStorageCompatibilityProvider& DataStorageCompatibility) {}
	virtual void RegisterTickGroups(IEditorDataStorageProvider& DataStorage) const {}
	virtual void RegisterQueries(IEditorDataStorageProvider& DataStorage) {}

	virtual void RegisterRegistrationFilters(IEditorDataStorageCompatibilityProvider& DataStorageCompatibility) const {}
	virtual void RegisterDealiaser(IEditorDataStorageCompatibilityProvider& DataStorageCompatibility) const {}
	
	virtual void RegisterWidgetPurposes(IEditorDataStorageUiProvider& DataStorageUi) const {}
	virtual void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const {}

	/**
	 * Called in reverse order before the DataStorage object is shut down
	 */
	virtual void PreShutdown(IEditorDataStorageProvider& DataStorage) {}
};
