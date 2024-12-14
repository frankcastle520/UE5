// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "MassArchetypeTypes.h"
#include "Misc/TVariant.h"
#include "Templates/SharedPointer.h"
#include "TypedElementDatabaseCommandBuffer.h"
#include "UObject/ObjectMacros.h"
#include "UObject/StrongObjectPtr.h"

#include "TypedElementDatabase.generated.h"

struct FMassEntityManager;
struct FMassProcessingPhaseManager;
class UEditorDataStorageFactory;
class FOutputDevice;
class UWorld;

namespace UE::Editor::DataStorage
{
	class FEnvironment;
} // namespace UE::Editor::DataStorage

UCLASS()
class TEDSCORE_API UEditorDataStorage
	: public UObject
	, public IEditorDataStorageProvider
{
	GENERATED_BODY()

	using RowCreationCallbackRef = UE::Editor::DataStorage::RowCreationCallbackRef;
	using ColumnCreationCallbackRef = UE::Editor::DataStorage::ColumnCreationCallbackRef;
	using ColumnListCallbackRef = UE::Editor::DataStorage::ColumnListCallbackRef;
	using ColumnListWithDataCallbackRef = UE::Editor::DataStorage::ColumnListWithDataCallbackRef;
	using ColumnCopyOrMoveCallback = UE::Editor::DataStorage::ColumnCopyOrMoveCallback;
	using RowHandle = UE::Editor::DataStorage::RowHandle;
	using TableHandle = UE::Editor::DataStorage::TableHandle;
	using QueryHandle = UE::Editor::DataStorage::QueryHandle;

public:
	template<typename FactoryType, typename DatabaseType>
	class TFactoryIterator
	{
	public:
		using ThisType = TFactoryIterator<FactoryType, DatabaseType>;
		using FactoryPtr = FactoryType*;
		using DatabasePtr = DatabaseType*;

		TFactoryIterator() = default;
		explicit TFactoryIterator(DatabasePtr InDatabase);

		FactoryPtr operator*() const;
		ThisType& operator++();
		operator bool() const;

	private:
		DatabasePtr Database = nullptr;
		int32 Index = 0;
	};

	using FactoryIterator = TFactoryIterator<UEditorDataStorageFactory, UEditorDataStorage>;
	using FactoryConstIterator = TFactoryIterator<const UEditorDataStorageFactory, const UEditorDataStorage>;

public:
	~UEditorDataStorage() override = default;
	
	void Initialize();
	
	void SetFactories(TConstArrayView<UClass*> InFactories);
	void ResetFactories();

	/** An iterator which allows traversal of factory instances. Ordered lowest->highest of GetOrder() */
	FactoryIterator CreateFactoryIterator();
	/** An iterator which allows traversal of factory instances. Ordered lowest->highest of GetOrder() */
	FactoryConstIterator CreateFactoryIterator() const;

	/** Returns factory instance given the type of factory */
	virtual const UEditorDataStorageFactory* FindFactory(const UClass* FactoryType) const override;
	/** Helper for FindFactory(const UClass*) */
	template<typename FactoryTypeT>
	const FactoryTypeT* FindFactory() const;
	
	void Deinitialize();

	/** Triggered at the start of the underlying Mass' tick cycle. */
	void OnPreMassTick(float DeltaTime);
	/** Triggered just before underlying Mass processing completes it's tick cycle. */
	void OnPostMassTick(float DeltaTime);

	TSharedPtr<FMassEntityManager> GetActiveMutableEditorEntityManager();
	TSharedPtr<const FMassEntityManager> GetActiveEditorEntityManager() const;

	virtual TableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList, const FName Name) override;
	virtual TableHandle RegisterTable(
		TableHandle SourceTable, TConstArrayView<const UScriptStruct*> ColumnList, const FName Name) override;
	virtual TableHandle FindTable(const FName Name) override;

	virtual RowHandle ReserveRow() override;
	virtual void BatchReserveRows(int32 Count, TFunctionRef<void(RowHandle)> ReservationCallback) override;
	virtual void BatchReserveRows(TArrayView<RowHandle> ReservedRows) override;
	virtual RowHandle AddRow(TableHandle Table, 
		RowCreationCallbackRef OnCreated) override;
	RowHandle AddRow(TableHandle Table) override;
	virtual bool AddRow(RowHandle ReservedRow, TableHandle Table) override;
	virtual bool AddRow(RowHandle ReservedRow, TableHandle Table,
		RowCreationCallbackRef OnCreated) override;
	virtual bool BatchAddRow(TableHandle Table, int32 Count,
		RowCreationCallbackRef OnCreated) override;
	virtual bool BatchAddRow(TableHandle Table, TConstArrayView<RowHandle> ReservedHandles,
		RowCreationCallbackRef OnCreated) override;
	virtual void RemoveRow(RowHandle Row) override;
	virtual bool IsRowAvailable(RowHandle Row) const override;
	virtual bool IsRowAssigned(RowHandle Row) const override;

	virtual void AddColumn(RowHandle Row, const UScriptStruct* ColumnType) override;
	virtual void AddColumn(RowHandle Row, const UE::Editor::DataStorage::FValueTag& Tag, const FName& InValue) override;
	virtual void AddColumnData(RowHandle Row, const UScriptStruct* ColumnType,
		const ColumnCreationCallbackRef& Initializer,
		ColumnCopyOrMoveCallback Relocator) override;
	virtual void RemoveColumn(RowHandle Row, const UScriptStruct* ColumnType) override;
	virtual void RemoveColumn(RowHandle Row, const UE::Editor::DataStorage::FValueTag& Tag) override;
	virtual void* GetColumnData(RowHandle Row, const UScriptStruct* ColumnType) override;
	virtual const void* GetColumnData(RowHandle Row, const UScriptStruct* ColumnType) const override;
	virtual void AddColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> Columns) override;
	virtual void RemoveColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> Columns) override;
	virtual void AddRemoveColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> ColumnsToAdd,
		TConstArrayView<const UScriptStruct*> ColumnsToRemove) override;
	virtual void BatchAddRemoveColumns(TConstArrayView<RowHandle> Rows,TConstArrayView<const UScriptStruct*> ColumnsToAdd,
		TConstArrayView<const UScriptStruct*> ColumnsToRemove) override;
	virtual bool HasColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes) const override;
	virtual bool HasColumns(RowHandle Row, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes) const override;
	virtual void ListColumns(RowHandle Row, ColumnListCallbackRef Callback) const;
	virtual void ListColumns(RowHandle Row, ColumnListWithDataCallbackRef Callback);
	virtual bool MatchesColumns(RowHandle Row, const UE::Editor::DataStorage::Queries::FConditions& Conditions) const override;

	const UScriptStruct* FindDynamicColumn(const UE::Editor::DataStorage::FDynamicColumnDescription& Description) const override;
	const UScriptStruct* GenerateDynamicColumn(const UE::Editor::DataStorage::FDynamicColumnDescription& Description) override;

	void RegisterTickGroup(FName GroupName, EQueryTickPhase Phase, FName BeforeGroup, FName AfterGroup, UE::Editor::DataStorage::EExecutionMode ExecutionMode);
	void UnregisterTickGroup(FName GroupName, EQueryTickPhase Phase);

	QueryHandle RegisterQuery(FQueryDescription&& Query) override;
	virtual void UnregisterQuery(QueryHandle Query) override;
	virtual const FQueryDescription& GetQueryDescription(QueryHandle Query) const override;
	virtual FName GetQueryTickGroupName(EQueryTickGroups Group) const override;
	virtual FQueryResult RunQuery(QueryHandle Query) override;
	virtual FQueryResult RunQuery(QueryHandle Query, DirectQueryCallbackRef Callback) override;
	virtual FQueryResult RunQuery(QueryHandle Query, UE::Editor::DataStorage::EDirectQueryExecutionFlags Flags,
		DirectQueryCallbackRef Callback) override;
	virtual void ActivateQueries(FName ActivationName) override;

	virtual RowHandle FindIndexedRow(UE::Editor::DataStorage::IndexHash Index) const override;
	virtual void IndexRow(UE::Editor::DataStorage::IndexHash Index, RowHandle Row) override;
	virtual void BatchIndexRows(
		TConstArrayView<TPair<UE::Editor::DataStorage::IndexHash, RowHandle>> IndexRowPairs) override;
	virtual void ReindexRow(
		UE::Editor::DataStorage::IndexHash OriginalIndex, 
		UE::Editor::DataStorage::IndexHash NewIndex, 
		RowHandle Row) override;
	virtual void RemoveIndex(UE::Editor::DataStorage::IndexHash Index) override;

	virtual FTypedElementOnDataStorageUpdate& OnUpdate() override;
	virtual FTypedElementOnDataStorageUpdate& OnUpdateCompleted() override;
	virtual bool IsAvailable() const override;
	virtual void* GetExternalSystemAddress(UClass* Target) override;

	virtual bool SupportsExtension(FName Extension) const override;
	virtual void ListExtensions(TFunctionRef<void(FName)> Callback) const override;
	
	TSharedPtr<UE::Editor::DataStorage::FEnvironment> GetEnvironment();
	TSharedPtr<const UE::Editor::DataStorage::FEnvironment> GetEnvironment() const;

	FMassArchetypeHandle LookupArchetype(TableHandle InTableHandle) const;

	void DebugPrintQueryCallbacks(FOutputDevice& Output) override;

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	void PreparePhase(EQueryTickPhase Phase, float DeltaTime);
	void FinalizePhase(EQueryTickPhase Phase, float DeltaTime);
	void Reset();

	int32 GetTableChunkSize(FName TableName) const;
	
	struct FFactoryTypePair
	{
		// Used to find the factory by type without needing to dereference each one
		TObjectPtr<UClass> Type;
		
		TObjectPtr<UEditorDataStorageFactory> Instance;
	};
	
	static const FName TickGroupName_Default;
	static const FName TickGroupName_PreUpdate;
	static const FName TickGroupName_Update;
	static const FName TickGroupName_PostUpdate;
	static const FName TickGroupName_SyncWidget;
	static const FName TickGroupName_SyncExternalToDataStorage;
	static const FName TickGroupName_SyncDataStorageToExternal;
	
	TArray<FMassArchetypeHandle> Tables;
	TMap<FName, TableHandle> TableNameLookup;

	// Ordered array of factories by the return value of GetOrder()
	TArray<FFactoryTypePair> Factories;

	TSharedPtr<UE::Editor::DataStorage::FEnvironment> Environment;
	
	FTypedElementOnDataStorageUpdate OnUpdateDelegate;
	FTypedElementOnDataStorageUpdate OnUpdateCompletedDelegate;
	FDelegateHandle OnPreMassTickHandle;
	FDelegateHandle OnPostMassTickHandle;

	TSharedPtr<FMassEntityManager> ActiveEditorEntityManager;
	TSharedPtr<FMassProcessingPhaseManager> ActiveEditorPhaseManager;
};

template <typename FactoryType, typename DatabaseType>
UEditorDataStorage::TFactoryIterator<FactoryType, DatabaseType>::TFactoryIterator(DatabasePtr InDatabase): Database(InDatabase)
{}

template <typename FactoryType, typename DatabaseType>
typename UEditorDataStorage::TFactoryIterator<FactoryType, DatabaseType>::FactoryPtr UEditorDataStorage::TFactoryIterator<FactoryType, DatabaseType>::operator*() const
{
	return Database->Factories[Index].Instance;
}

template <typename FactoryType, typename DatabaseType>
typename UEditorDataStorage::TFactoryIterator<FactoryType, DatabaseType>::ThisType& UEditorDataStorage::TFactoryIterator<FactoryType, DatabaseType>::operator++()
{
	if (Database != nullptr && Index < Database->Factories.Num())
	{
		++Index;
	}
	return *this;
}

template <typename FactoryType, typename DatabaseType>
UEditorDataStorage::TFactoryIterator<FactoryType, DatabaseType>::operator bool() const
{
	return Database != nullptr && Index < Database->Factories.Num();
}

template <typename FactoryTypeT>
const FactoryTypeT* UEditorDataStorage::FindFactory() const
{
	return static_cast<const FactoryTypeT*>(FindFactory(FactoryTypeT::StaticClass()));
}
