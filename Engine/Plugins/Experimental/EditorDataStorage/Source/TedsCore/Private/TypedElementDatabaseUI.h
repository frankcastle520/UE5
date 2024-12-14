// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <variant>
#include "Containers/Map.h"
#include "Elements/Common/TypedElementQueryConditions.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Logging/LogMacros.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementDatabaseUI.generated.h"

class IEditorDataStorageProvider;
class IEditorDataStorageCompatibilityProvider;

TEDSCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogEditorDataStorageUI, Log, All);

UCLASS()
class TEDSCORE_API UEditorDataStorageUi final
	: public UObject
	, public IEditorDataStorageUiProvider
{
	GENERATED_BODY()

public:
	~UEditorDataStorageUi() override = default;

	void Initialize(
		IEditorDataStorageProvider* StorageInterface, 
		IEditorDataStorageCompatibilityProvider* StorageCompatibilityInterface);
	void Deinitialize();

	void RegisterWidgetPurpose(FName Purpose, EPurposeType Type, FText Description) override;

	bool RegisterWidgetFactory(FName Purpose, const UScriptStruct* Constructor) override;
	bool RegisterWidgetFactory(FName Purpose, const UScriptStruct* Constructor,
		UE::Editor::DataStorage::Queries::FConditions Columns) override;
	bool RegisterWidgetFactory(FName Purpose, TUniquePtr<FTypedElementWidgetConstructor>&& Constructor) override;
	bool RegisterWidgetFactory(FName Purpose, TUniquePtr<FTypedElementWidgetConstructor>&& Constructor,
		UE::Editor::DataStorage::Queries::FConditions Columns) override;

	void CreateWidgetConstructors(FName Purpose,
		const UE::Editor::DataStorage::FMetaDataView& Arguments, const WidgetConstructorCallback& Callback) override;
	void CreateWidgetConstructors(FName Purpose, EMatchApproach MatchApproach, TArray<TWeakObjectPtr<const UScriptStruct>>& Columns,
		const UE::Editor::DataStorage::FMetaDataView& Arguments, const WidgetConstructorCallback& Callback) override;

	void ConstructWidgets(FName Purpose, const UE::Editor::DataStorage::FMetaDataView& Arguments,
		const WidgetCreatedCallback& ConstructionCallback) override;
	TSharedPtr<SWidget> ConstructWidget(UE::Editor::DataStorage::RowHandle Row, FTypedElementWidgetConstructor& Constructor,
		const UE::Editor::DataStorage::FMetaDataView& Arguments) override;

	void ListWidgetPurposes(const WidgetPurposeCallback& Callback) const override;

	bool SupportsExtension(FName Extension) const override;
	void ListExtensions(TFunctionRef<void(FName)> Callback) const override;

private:
	struct FWidgetFactory
	{
	public:
		using ConstructorType = std::variant<const UScriptStruct*, TUniquePtr<FTypedElementWidgetConstructor>>;
		ConstructorType Constructor;

		FWidgetFactory() = default;
		explicit FWidgetFactory(const UScriptStruct* InConstructor);
		explicit FWidgetFactory(TUniquePtr<FTypedElementWidgetConstructor>&& InConstructor);
		FWidgetFactory(const UScriptStruct* InConstructor, UE::Editor::DataStorage::Queries::FConditions&& InColumns);
		FWidgetFactory(TUniquePtr<FTypedElementWidgetConstructor>&& InConstructor, UE::Editor::DataStorage::Queries::FConditions&& InColumns);
		const UE::Editor::DataStorage::Queries::FConditions& GetConditions(IEditorDataStorageProvider* DataStorage) const;

	private:
		// Private and mutable so we can control access to it via GetConditions() to compile the conditions on demand
		mutable UE::Editor::DataStorage::Queries::FConditions Columns;
	};

	struct FPurposeInfo
	{	
		TArray<FWidgetFactory> Factories;
		FText Description;
		EPurposeType Type;
		bool bIsSorted{ false }; //< Whether or not the array of factories needs to be sorted. The factories themselves are already sorted.
	};

	void CreateStandardArchetypes();

	bool CreateSingleWidgetConstructor(
		const FWidgetFactory::ConstructorType& Constructor,
		const UE::Editor::DataStorage::FMetaDataView& Arguments,
		TArray<TWeakObjectPtr<const UScriptStruct>> MatchedColumnTypes,
		const UE::Editor::DataStorage::Queries::FConditions& QueryConditions,
		const WidgetConstructorCallback& Callback);

	void CreateWidgetInstance(
		FTypedElementWidgetConstructor& Constructor,
		const UE::Editor::DataStorage::FMetaDataView& Arguments,
		const WidgetCreatedCallback& ConstructionCallback);

	void CreateWidgetConstructors_LongestMatch(
		const TArray<FWidgetFactory>& WidgetFactories, 
		TArray<TWeakObjectPtr<const UScriptStruct>>& Columns, 
		const UE::Editor::DataStorage::FMetaDataView& Arguments,
		const WidgetConstructorCallback& Callback);
	void CreateWidgetConstructors_ExactMatch(
		const TArray<FWidgetFactory>& WidgetFactories,
		TArray<TWeakObjectPtr<const UScriptStruct>>& Columns,
		const UE::Editor::DataStorage::FMetaDataView& Arguments,
		const WidgetConstructorCallback& Callback);
	void CreateWidgetConstructors_SingleMatch(
		const TArray<FWidgetFactory>& WidgetFactories,
		TArray<TWeakObjectPtr<const UScriptStruct>>& Columns,
		const UE::Editor::DataStorage::FMetaDataView& Arguments,
		const WidgetConstructorCallback& Callback);

	UE::Editor::DataStorage::TableHandle WidgetTable{ UE::Editor::DataStorage::InvalidTableHandle };
	
	TMap<FName, FPurposeInfo> WidgetPurposes;
	
	IEditorDataStorageProvider* Storage{ nullptr };
	IEditorDataStorageCompatibilityProvider* StorageCompatibility{ nullptr };
};