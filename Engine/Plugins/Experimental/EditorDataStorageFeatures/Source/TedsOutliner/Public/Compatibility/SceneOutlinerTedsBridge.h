// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneOutlinerStandaloneTypes.h"
#include "Containers/ContainersFwd.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"

#include "SceneOutlinerTedsBridge.generated.h"

class ISceneOutliner;
class IEditorDataStorageProvider;
class IEditorDataStorageUiProvider;
class IEditorDataStorageCompatibilityProvider;

DECLARE_DELEGATE_RetVal_OneParam(FSceneOutlinerTreeItemID, FTreeItemIDDealiaser, UE::Editor::DataStorage::RowHandle);

class FSceneOutlinerTedsBridge;

/**
 * Utility class to bind Typed Elements Data Storage queries to a Scene Outliner. The provided query is expected to be a select query
 * and will be used to populate the Scene Outliner in addition to already existing data.
 */
class TEDSOUTLINER_API FSceneOutlinerTedsQueryBinder
{
public:
	static const FName CellWidgetTableName;
	static const FName HeaderWidgetPurpose;
	static const FName DefaultHeaderWidgetPurpose;
	static const FName CellWidgetPurpose;
	static const FName DefaultCellWidgetPurpose;
	static const FName ItemLabelCellWidgetPurpose;
	static const FName DefaultItemLabelCellWidgetPurpose;


	static FSceneOutlinerTedsQueryBinder& GetInstance();

	void AssignQuery(UE::Editor::DataStorage::QueryHandle Query, const TSharedPtr<ISceneOutliner>& Widget, TConstArrayView<FName> InCellWidgetPurposes);

	// Register a dealiaser for a specific TEDS-Outliner to convert a row handle to an FSceneOutlinerTreeItemID
	void RegisterTreeItemIDDealiaser(const TSharedPtr<ISceneOutliner>& Widget, const FTreeItemIDDealiaser& InDealiaser);

	// Get the dealiaser for a specific outliner instance
	FTreeItemIDDealiaser GetTreeItemIDDealiaser(const TSharedPtr<ISceneOutliner>& Widget);

	// Get the name of the Outliner column corresponding to the given TEDS column (if any)
	FName FindOutlinerColumnFromTEDSColumns(TConstArrayView<TWeakObjectPtr<const UScriptStruct>> TEDSColumns) const;

private:
	FSceneOutlinerTedsQueryBinder();
	void SetupDefaultColumnMapping();
	void CleanupStaleOutliners();
	
	TSharedPtr<FSceneOutlinerTedsBridge>* FindOrAddQueryMapping(const TSharedPtr<ISceneOutliner>& Widget);
	TSharedPtr<FSceneOutlinerTedsBridge>* FindQueryMapping(const TSharedPtr<ISceneOutliner>& Widget);

	
	TMap<TWeakPtr<ISceneOutliner>, TSharedPtr<FSceneOutlinerTedsBridge>> SceneOutliners;

	IEditorDataStorageProvider* Storage{ nullptr };
	IEditorDataStorageUiProvider* StorageUi{ nullptr };
	IEditorDataStorageCompatibilityProvider* StorageCompatibility{ nullptr };

	TMap<TWeakObjectPtr<const UScriptStruct>, FName> TEDSToOutlinerDefaultColumnMapping;
};

UCLASS()
class USceneOutlinerTedsBridgeFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~USceneOutlinerTedsBridgeFactory() override = default;

	void RegisterWidgetPurposes(IEditorDataStorageUiProvider& DataStorageUi) const override;
};
