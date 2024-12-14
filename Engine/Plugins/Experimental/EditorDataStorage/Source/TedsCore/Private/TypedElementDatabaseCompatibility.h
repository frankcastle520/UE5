// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <atomic>
#include "Async/Mutex.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Commands/EditorDataStorageCommandBuffer.h"
#include "Commands/EditorDataStorageCompatibilityCommands.h"
#include "Compatibility/TypedElementObjectReinstancingManager.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "Engine/World.h"
#include "Misc/Change.h"
#include "Misc/Optional.h"
#include "UObject/ObjectMacros.h"
#include "UObject/WeakObjectPtr.h"

#include "TypedElementDatabaseCompatibility.generated.h"

class IEditorDataStorageProvider;
struct FMassActorManager;
class UTypedElementMementoSystem;

namespace UE::Editor::DataStorage
{
	class FEnvironment;
} // namespace UE::Editor::DataStorage

UCLASS()
class TEDSCORE_API UEditorDataStorageCompatibility
	: public UObject
	, public IEditorDataStorageCompatibilityProvider
{
	GENERATED_BODY()

	friend struct UE::Editor::DataStorage::FCommandProcessor;
	friend struct UE::Editor::DataStorage::FPatchData;
	friend struct UE::Editor::DataStorage::FPrepareCommands;
public:
	~UEditorDataStorageCompatibility() override = default;

	void Initialize(UEditorDataStorage* InStorage);
	void Deinitialize();

	void RegisterRegistrationFilter(ObjectRegistrationFilter Filter) override;
	void RegisterDealiaserCallback(ObjectToRowDealiaser Dealiaser) override;
	void RegisterTypeTableAssociation(TObjectPtr<UStruct> TypeInfo, UE::Editor::DataStorage::TableHandle Table) override;
	FDelegateHandle RegisterObjectAddedCallback(UE::Editor::DataStorage::ObjectAddedCallback&& OnObjectAdded);
	void UnregisterObjectAddedCallback(FDelegateHandle Handle);
	FDelegateHandle RegisterObjectRemovedCallback(UE::Editor::DataStorage::ObjectRemovedCallback&& OnObjectRemoved);
	void UnregisterObjectRemovedCallback(FDelegateHandle Handle);
	
	UE::Editor::DataStorage::RowHandle AddCompatibleObjectExplicit(UObject* Object) override;
	UE::Editor::DataStorage::RowHandle AddCompatibleObjectExplicit(void* Object, TWeakObjectPtr<UScriptStruct> TypeInfo) override;
	
	void RemoveCompatibleObjectExplicit(UObject* Object) override;
	void RemoveCompatibleObjectExplicit(void* Object) override;

	UE::Editor::DataStorage::RowHandle FindRowWithCompatibleObjectExplicit(const UObject* Object) const override;
	UE::Editor::DataStorage::RowHandle FindRowWithCompatibleObjectExplicit(const void* Object) const override;

	bool SupportsExtension(FName Extension) const override;
	void ListExtensions(TFunctionRef<void(FName)> Callback) const override;
	
private:
	// The below changes expect UEditorDataStorageCompatibility to be the object passed in to StoreUndo.
	// Note that we cannot pass in TargetObject to StoreUndo because doing so seems to stomp regular Modify()
	// changes for that object.
	class FRegistrationCommandChange final : public FCommandChange
	{
	public:
		FRegistrationCommandChange(UEditorDataStorageCompatibility* InOwner, UObject* InTargetObject);
		~FRegistrationCommandChange() override;

		void Apply(UObject* Object) override;
		void Revert(UObject* Object) override;
		FString ToString() const override;

	private:
		TWeakObjectPtr<UEditorDataStorageCompatibility> Owner;
		TWeakObjectPtr<UObject> TargetObject;
		UE::Editor::DataStorage::RowHandle MementoRow = UE::Editor::DataStorage::InvalidRowHandle;
	};
	class FDeregistrationCommandChange final : public FCommandChange
	{
	public:
		FDeregistrationCommandChange(UEditorDataStorageCompatibility* InOwner, UObject* InTargetObject);
		~FDeregistrationCommandChange() override;

		void Apply(UObject* Object) override;
		void Revert(UObject* Object) override;
		FString ToString() const override;

	private:
		TWeakObjectPtr<UEditorDataStorageCompatibility> Owner;
		TWeakObjectPtr<UObject> TargetObject;
		UE::Editor::DataStorage::RowHandle MementoRow = UE::Editor::DataStorage::InvalidRowHandle;
	};

	void Prepare();
	void Reset();
	void CreateStandardArchetypes();
	void RegisterTypeInformationQueries();
	
	bool ShouldAddObject(const UObject* Object) const;
	UE::Editor::DataStorage::TableHandle FindBestMatchingTable(const UStruct* TypeInfo) const;
	template<bool bEnableTransactions>
	UE::Editor::DataStorage::RowHandle AddCompatibleObjectExplicitTransactionable(UObject* Object);
	template<bool bEnableTransactions>
	void RemoveCompatibleObjectExplicitTransactionable(const UObject* Object);
	template<bool bEnableTransactions>
	void RemoveCompatibleObjectExplicitTransactionable(const UObject* Object, UE::Editor::DataStorage::RowHandle ObjectRow);
	UE::Editor::DataStorage::RowHandle DealiasObject(const UObject* Object) const;

	void Tick();
	void TickPendingCommands();
	void TickPendingUObjectRegistration();
	void TickPendingExternalObjectRegistration();
	void TickObjectSync();

	void OnPrePropertyChanged(UObject* Object, const FEditPropertyChain& PropertyChain);
	void OnPostEditChangeProperty(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
	void OnObjectModified(UObject* Object);
	void TriggerOnObjectAdded(const void* Object, UE::Editor::DataStorage::FObjectTypeInfo TypeInfo, UE::Editor::DataStorage::RowHandle Row) const;
	void TriggerOnPreObjectRemoved(const void* Object, UE::Editor::DataStorage::FObjectTypeInfo TypeInfo, UE::Editor::DataStorage::RowHandle Row) const;
	void OnObjectReinstanced(const FCoreUObjectDelegates::FReplacementObjectMap& ReplacedObjects);

	void OnPostGcUnreachableAnalysis();

	void OnPostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues);
	void OnPreWorldFinishDestroy(UWorld* World);

	void OnActorDestroyed(AActor* Actor);

	struct FPendingTypeInformationUpdate
	{
	public:
		FPendingTypeInformationUpdate();

		void AddTypeInformation(const TMap<UObject*, UObject*>& ReplacedObjects);
		void Process(UEditorDataStorageCompatibility& Compatibility);

	private:
		TOptional<TWeakObjectPtr<UObject>> ProcessResolveTypeRecursively(const TWeakObjectPtr<UObject>& Target);

		struct FTypeInfoEntryKeyFuncs : TDefaultMapHashableKeyFuncs<TWeakObjectPtr<UObject>, TWeakObjectPtr<UObject>, false>
		{
			static inline bool Matches(KeyInitType Lhs, KeyInitType Rhs) { return Lhs.HasSameIndexAndSerialNumber(Rhs); }
		};
		using PendingTypeInformationMap = TMap<TWeakObjectPtr<UObject>, TWeakObjectPtr<UObject>, FDefaultSetAllocator, FTypeInfoEntryKeyFuncs>;

		PendingTypeInformationMap PendingTypeInformationUpdates[2];
		PendingTypeInformationMap* PendingTypeInformationUpdatesActive;
		PendingTypeInformationMap* PendingTypeInformationUpdatesSwapped;
		TArray<TTuple<TWeakObjectPtr<UStruct>, UE::Editor::DataStorage::TableHandle>> UpdatedTypeInfoScratchBuffer;
		UE::FMutex Safeguard;
		std::atomic<bool> bHasPendingUpdate = false;
	};
	FPendingTypeInformationUpdate PendingTypeInformationUpdate;

	struct ExternalObjectRegistration
	{
		void* Object;
		TWeakObjectPtr<UScriptStruct> TypeInfo;
	};
	
	template<typename AddressType>
	struct PendingRegistration
	{
	private:
		struct FEntry
		{
			AddressType Address;
			UE::Editor::DataStorage::RowHandle Row;
			UE::Editor::DataStorage::TableHandle Table;
		};
		TArray<FEntry> Entries;

	public:
		void Add(UE::Editor::DataStorage::RowHandle ReservedRowHandle, AddressType Address);
		bool IsEmpty() const;
		int32 Num() const;
		
		void ForEachAddress(const TFunctionRef<void(AddressType&)>& Callback);
		void ProcessEntries(IEditorDataStorageProvider& Storage, UEditorDataStorageCompatibility& Compatibility,
			const TFunctionRef<void(UE::Editor::DataStorage::RowHandle, const AddressType&)>& SetupRowCallback);
		void Reset();
	};

	UE::Editor::DataStorage::CompatibilityCommandBuffer QueuedCommands;
	UE::Editor::DataStorage::CompatibilityCommandBuffer::FCollection PendingCommands;
	PendingRegistration<TWeakObjectPtr<UObject>> UObjectsPendingRegistration;
	PendingRegistration<ExternalObjectRegistration> ExternalObjectsPendingRegistration;
	TArray<UE::Editor::DataStorage::RowHandle> RowScratchBuffer;
	
	TArray<ObjectRegistrationFilter> ObjectRegistrationFilters;
	TArray<ObjectToRowDealiaser> ObjectToRowDialiasers;
	using TypeToTableMapType = TMap<TWeakObjectPtr<UStruct>, UE::Editor::DataStorage::TableHandle>;
	TypeToTableMapType TypeToTableMap;
	TArray<TPair<UE::Editor::DataStorage::ObjectAddedCallback, FDelegateHandle>> ObjectAddedCallbackList;
	TArray<TPair<UE::Editor::DataStorage::ObjectRemovedCallback, FDelegateHandle>> PreObjectRemovedCallbackList;

	UE::Editor::DataStorage::TableHandle StandardActorTable{ UE::Editor::DataStorage::InvalidTableHandle };
	UE::Editor::DataStorage::TableHandle StandardActorWithTransformTable{ UE::Editor::DataStorage::InvalidTableHandle };
	UE::Editor::DataStorage::TableHandle StandardUObjectTable{ UE::Editor::DataStorage::InvalidTableHandle };
	UE::Editor::DataStorage::TableHandle StandardExternalObjectTable{ UE::Editor::DataStorage::InvalidTableHandle };
	IEditorDataStorageProvider* Storage{ nullptr };

	/**
	 * Reference of objects (UObject and AActor) that need to be fully synced from the world to the database.
	 * Caution: Could point to objects that have been GC-ed
	 */
	struct FSyncTagInfo
	{
		TWeakObjectPtr<UScriptStruct> ColumnType;
		bool bAddColumn;

		bool operator==(const FSyncTagInfo& Rhs) const = default;
		bool operator!=(const FSyncTagInfo& Rhs) const = default;
	};
	friend SIZE_T GetTypeHash(const FSyncTagInfo& Column);
	static constexpr uint32 MaxExpectedTagsForObjectSync = 2;
	using ObjectsNeedingSyncTagsMapKey = TObjectKey<const UObject>;
	using ObjectsNeedingSyncTagsMapValue = TArray<FSyncTagInfo, TInlineAllocator<MaxExpectedTagsForObjectSync>>;
	using ObjectsNeedingSyncTagsMap = TMap<ObjectsNeedingSyncTagsMapKey, ObjectsNeedingSyncTagsMapValue>;
	ObjectsNeedingSyncTagsMap ObjectsNeedingSyncTags;

	TMap<UWorld*, FDelegateHandle> ActorDestroyedDelegateHandles;
	FDelegateHandle PreEditChangePropertyDelegateHandle;
	FDelegateHandle PostEditChangePropertyDelegateHandle;
	FDelegateHandle ObjectModifiedDelegateHandle;
	FDelegateHandle PostWorldInitializationDelegateHandle;
	FDelegateHandle PreWorldFinishDestroyDelegateHandle;
	FDelegateHandle ObjectReinstancedDelegateHandle;
	FDelegateHandle PostGcUnreachableAnalysisHandle;
	
	TSharedPtr<UE::Editor::DataStorage::FEnvironment> Environment;
	UE::Editor::DataStorage::QueryHandle ClassTypeInfoQuery;
	UE::Editor::DataStorage::QueryHandle ScriptStructTypeInfoQuery;
	UE::Editor::DataStorage::QueryHandle UObjectQuery;
};

SIZE_T GetTypeHash(const UEditorDataStorageCompatibility::FSyncTagInfo& Column);
