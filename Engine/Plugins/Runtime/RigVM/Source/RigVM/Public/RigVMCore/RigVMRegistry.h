// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ChunkedArray.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "RigVMCore/RigVMTraits.h"
#include "RigVMDispatchFactory.h"
#include "RigVMFunction.h"
#include "RigVMTemplate.h"
#include "RigVMTypeIndex.h"
#include "Templates/EnableIf.h"
#include "Templates/IsEnum.h"
#include "Templates/Models.h"
#include "UObject/Class.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ReflectedTypeAccessors.h"
#include "UObject/GCObject.h"

class FProperty;
class IPlugin;
class UObject;
struct FRigVMDispatchFactory;

struct FRigVMRegistry_RWLock;
typedef FRigVMRegistry_RWLock FRigVMRegistry;

/**
 * The FRigVMRegistry is used to manage all known function pointers
 * for use in the RigVM. The Register method is called automatically
 * when the static struct is initially constructed for each USTRUCT
 * hosting a RIGVM_METHOD enabled virtual function.
 * 
 * Inheriting from FGCObject to ensure that all type objects cannot be GCed
 */
struct RIGVM_API FRigVMRegistry_NoLock : public FGCObject
{
public:

	enum ELockType : uint8
	{
		LockType_Read,
		LockType_Write,
		LockType_Invalid
	};

	// Returns the singleton registry
	static FRigVMRegistry_NoLock& Get(ELockType InLockType);
	static const FRigVMRegistry_NoLock& GetForRead() { return Get(LockType_Read); }
	static FRigVMRegistry_NoLock& GetForWrite() { return Get(LockType_Write); }

	DECLARE_MULTICAST_DELEGATE(FOnRigVMRegistryChanged);

	virtual ~FRigVMRegistry_NoLock() override;
	
	// FGCObject overrides
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	
	// Registers a function given its name.
	// The name will be the name of the struct and virtual method,
	// for example "FMyStruct::MyVirtualMethod"
	virtual void Register_NoLock(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct = nullptr, const TArray<FRigVMFunctionArgument>& InArguments = TArray<FRigVMFunctionArgument>());

	// Registers a dispatch factory given its struct.
	virtual const FRigVMDispatchFactory* RegisterFactory_NoLock(UScriptStruct* InFactoryStruct);

	// Register a predicate contained in the input struct
	virtual void RegisterPredicate_NoLock(UScriptStruct* InStruct, const TCHAR* InName, const TArray<FRigVMFunctionArgument>& InArguments);

	// How to register an object's class when passed to RegisterObjectTypes
	enum class ERegisterObjectOperation
	{
		Class,

		ClassAndParents,

		ClassAndChildren,
	};

	// Register a set of allowed object types
	virtual void RegisterObjectTypes_NoLock(TConstArrayView<TPair<UClass*, ERegisterObjectOperation>> InClasses);

	// Register a set of allowed struct types
	virtual void RegisterStructTypes_NoLock(TConstArrayView<UScriptStruct*> InStructs);

	// Refreshes the list and finds the function pointers
	// based on the names.
	virtual void RefreshEngineTypes_NoLock();

	// Refreshes the list and finds the function pointers
	// based on the names.
	virtual void RefreshEngineTypesIfRequired_NoLock();
	
	// Update the registry when types are renamed
	virtual void OnAssetRenamed_NoLock(const FAssetData& InAssetData, const FString& InOldObjectPath);
	
	// Update the registry when old types are removed
	virtual bool OnAssetRemoved_NoLock(const FAssetData& InAssetData);

	// Removes all types associated with a plugin that's being unloaded. 
	virtual bool OnPluginUnloaded_NoLock(IPlugin& InPlugin);
	
	// Update the registry when new types are added to the attribute system so that they can be selected
	// on Attribute Nodes
	virtual void OnAnimationAttributeTypesChanged_NoLock(const UScriptStruct* InStruct, bool bIsAdded);

	// Clear the registry
	virtual void Reset_NoLock();

	// Adds a type if it doesn't exist yet and returns its index.
	// This function is thread-safe
	virtual TRigVMTypeIndex FindOrAddType_NoLock(const FRigVMTemplateArgumentType& InType, bool bForce = false);

	// Removes a type from the registry, and updates all dependent templates
	// which also creates invalid permutations in templates that we should ignore
	virtual bool RemoveType_NoLock(const FSoftObjectPath& InObjectPath, const UClass* InObjectClass);

	// Returns the type index given a type
	virtual TRigVMTypeIndex GetTypeIndex_NoLock(const FRigVMTemplateArgumentType& InType) const;

	// Returns the type index given a cpp type and a type object
	virtual TRigVMTypeIndex GetTypeIndex_NoLock(const FName& InCPPType, UObject* InCPPTypeObject) const
	{
		return GetTypeIndex_NoLock(FRigVMTemplateArgumentType(InCPPType, InCPPTypeObject));
	}

	// Returns the type index given an enum
	template <
		typename T,
		typename TEnableIf<TIsEnum<T>::Value>::Type* = nullptr
	>
	TRigVMTypeIndex GetTypeIndex_NoLock(bool bAsArray = false) const
	{
		FRigVMTemplateArgumentType Type(StaticEnum<T>());
		if(bAsArray)
		{
			Type.ConvertToArray();
		}
		return GetTypeIndex_NoLock(Type);
	}

	// Returns the type index given a struct
	template <
		typename T,
		typename TEnableIf<TRigVMIsBaseStructure<T>::Value, T>::Type* = nullptr
	>
	TRigVMTypeIndex GetTypeIndex_NoLock(bool bAsArray = false) const
	{
		FRigVMTemplateArgumentType Type(TBaseStructure<T>::Get());
		if(bAsArray)
		{
			Type.ConvertToArray();
		}
		return GetTypeIndex_NoLock(Type);
	}

	// Returns the type index given a struct
	template <
		typename T,
		typename TEnableIf<TModels_V<CRigVMUStruct, T>>::Type * = nullptr
	>
	TRigVMTypeIndex GetTypeIndex_NoLock(bool bAsArray = false) const
	{
		FRigVMTemplateArgumentType Type(T::StaticStruct());
		if(bAsArray)
		{
			Type.ConvertToArray();
		}
		return GetTypeIndex_NoLock(Type);
	}

	// Returns the type index given an object
	template <
		typename T,
		typename TEnableIf<TModels_V<CRigVMUClass, T>>::Type * = nullptr
	>
	TRigVMTypeIndex GetTypeIndex_NoLock(bool bAsArray = false) const
	{
		FRigVMTemplateArgumentType Type(T::StaticClass(), RigVMTypeUtils::EClassArgType::AsObject);
		if(bAsArray)
		{
			Type.ConvertToArray();
		}
		return GetTypeIndex_NoLock(Type);
	}

	// Returns the type given its index
	virtual const FRigVMTemplateArgumentType& GetType_NoLock(TRigVMTypeIndex InTypeIndex) const;

	// Returns the number of types
	virtual int32 NumTypes_NoLock() const { return Types.Num(); }

	// Returns the type given only its cpp type
	virtual const FRigVMTemplateArgumentType& FindTypeFromCPPType_NoLock(const FString& InCPPType) const;

	// Returns the type index given only its cpp type
	virtual TRigVMTypeIndex GetTypeIndexFromCPPType_NoLock(const FString& InCPPType) const;

	// Returns true if the type is an array
	virtual bool IsArrayType_NoLock(TRigVMTypeIndex InTypeIndex) const;

	// Returns true if the type is an execute type
	virtual bool IsExecuteType_NoLock(TRigVMTypeIndex InTypeIndex) const;

	// Converts the given execute context type to the base execute context type
	virtual bool ConvertExecuteContextToBaseType_NoLock(TRigVMTypeIndex& InOutTypeIndex) const;

	// Returns the dimensions of the array 
	virtual int32 GetArrayDimensionsForType_NoLock(TRigVMTypeIndex InTypeIndex) const;

	// Returns true if the type is a wildcard type
	virtual bool IsWildCardType_NoLock(TRigVMTypeIndex InTypeIndex) const;

	// Returns true if the types can be matched.
	virtual bool CanMatchTypes_NoLock(TRigVMTypeIndex InTypeIndexA, TRigVMTypeIndex InTypeIndexB, bool bAllowFloatingPointCasts) const;

	// Returns the list of compatible types for a given type
	virtual const TArray<TRigVMTypeIndex>& GetCompatibleTypes_NoLock(TRigVMTypeIndex InTypeIndex) const;

	// Returns all compatible types given a category
	virtual const TArray<TRigVMTypeIndex>& GetTypesForCategory_NoLock(FRigVMTemplateArgument::ETypeCategory InCategory) const;

	// Returns the type index of the array matching the given element type index
	virtual TRigVMTypeIndex GetArrayTypeFromBaseTypeIndex_NoLock(TRigVMTypeIndex InTypeIndex) const;

	// Returns the type index of the element matching the given array type index
	virtual TRigVMTypeIndex GetBaseTypeFromArrayTypeIndex_NoLock(TRigVMTypeIndex InTypeIndex) const;

	// Returns the function given its name (or nullptr)
	virtual const FRigVMFunction* FindFunction_NoLock(const TCHAR* InName, const FRigVMUserDefinedTypeResolver& InTypeResolver = FRigVMUserDefinedTypeResolver()) const;

	// Returns the function given its backing up struct and method name
	virtual const FRigVMFunction* FindFunction_NoLock(UScriptStruct* InStruct, const TCHAR* InName, const FRigVMUserDefinedTypeResolver& InResolvalInfo = FRigVMUserDefinedTypeResolver()) const;

	// Returns all current RigVM functions
	virtual const TChunkedArray<FRigVMFunction>& GetFunctions_NoLock() const;

	// Returns a template pointer given its notation (or nullptr)
	virtual const FRigVMTemplate* FindTemplate_NoLock(const FName& InNotation, bool bIncludeDeprecated = false) const;

	// Returns all current RigVM functions
	virtual const TChunkedArray<FRigVMTemplate>& GetTemplates_NoLock() const;

	// Defines and retrieves a template given its arguments
	virtual const FRigVMTemplate* GetOrAddTemplateFromArguments_NoLock(
		const FName& InName,
		const TArray<FRigVMTemplateArgumentInfo>& InInfos,
		const FRigVMTemplateDelegates& InDelegates);

	// Adds a new template given its arguments
	virtual const FRigVMTemplate* AddTemplateFromArguments_NoLock(
		const FName& InName,
		const TArray<FRigVMTemplateArgumentInfo>& InInfos,
		const FRigVMTemplateDelegates& InDelegates);

	// Returns a dispatch factory given its name (or nullptr)
	virtual FRigVMDispatchFactory* FindDispatchFactory_NoLock(const FName& InFactoryName) const;

	// Returns a dispatch factory given its static struct (or nullptr)
	virtual FRigVMDispatchFactory* FindOrAddDispatchFactory_NoLock(UScriptStruct* InFactoryStruct);

	// Returns a dispatch factory given its static struct (or nullptr)
	template <
		typename T,
		typename TEnableIf<TModels_V<CRigVMUStruct, T>>::Type * = nullptr
	>
	FRigVMDispatchFactory* FindOrAddDispatchFactory_NoLock()
	{
		return FindOrAddDispatchFactory_NoLock(T::StaticStruct());
	}

	// Returns a dispatch factory's singleton function name if that exists
	virtual FString FindOrAddSingletonDispatchFunction_NoLock(UScriptStruct* InFactoryStruct);

	// Returns a dispatch factory's singleton function name if that exists
	template <
		typename T,
		typename TEnableIf<TModels_V<CRigVMUStruct, T>>::Type * = nullptr
	>
	FString FindOrAddSingletonDispatchFunction_NoLock()
	{
		return FindOrAddSingletonDispatchFunction_NoLock(T::StaticStruct());
	}

	// Returns all dispatch factories
	virtual const TArray<FRigVMDispatchFactory*>& GetFactories_NoLock() const;

	// Given a struct name, return the predicates
	virtual const TArray<FRigVMFunction>* GetPredicatesForStruct_NoLock(const FName& InStructName) const;

	static const TArray<UScriptStruct*>& GetMathTypes();


	// Returns a unique hash per type index
	virtual uint32 GetHashForType_NoLock(TRigVMTypeIndex InTypeIndex) const;
	virtual uint32 GetHashForScriptStruct_NoLock(const UScriptStruct* InScriptStruct, bool bCheckTypeIndex = true) const;
	virtual uint32 GetHashForStruct_NoLock(const UStruct* InStruct) const;
	virtual uint32 GetHashForEnum_NoLock(const UEnum* InEnum, bool bCheckTypeIndex = true) const;
	virtual uint32 GetHashForProperty_NoLock(const FProperty* InProperty) const;

	virtual void RebuildRegistry_NoLock(); 

	static inline const FLazyName TemplateNameMetaName = FLazyName(TEXT("TemplateName"));

protected:


	FRigVMRegistry_NoLock();

	// disable copy constructor
	FRigVMRegistry_NoLock(const FRigVMRegistry_NoLock&) = delete;
	// disable assignment operator
	FRigVMRegistry_NoLock& operator= (const FRigVMRegistry_NoLock &InOther) = delete;

	struct FTypeInfo
	{
		FTypeInfo()
			: Type()
			, BaseTypeIndex(INDEX_NONE)
			, ArrayTypeIndex(INDEX_NONE)
			, bIsArray(false)
			, bIsExecute(false)
			, Hash(UINT32_MAX)
		{}
		
		FRigVMTemplateArgumentType Type;
		TRigVMTypeIndex BaseTypeIndex;
		TRigVMTypeIndex ArrayTypeIndex;
		bool bIsArray;
		bool bIsExecute;
		uint32 Hash;
	};

	// Initialize the base types
	void Initialize_NoLock();
	virtual void Initialize(bool bLockRegistry) = 0;

	static EObjectFlags DisallowedFlags()
	{
		return RF_BeginDestroyed | RF_FinishDestroyed;
	}

	static EObjectFlags NeededFlags()
	{
		return RF_Public;
	}

	bool IsAllowedType_NoLock(const FProperty* InProperty) const;
	bool IsAllowedType_NoLock(const UEnum* InEnum) const;
	bool IsAllowedType_NoLock(const UStruct* InStruct) const;
	bool IsAllowedType_NoLock(const UClass* InClass) const;
	static bool IsTypeOfByName(const UObject* InObject, const FName& InName);

	void RegisterTypeInCategory_NoLock(const FRigVMTemplateArgument::ETypeCategory InCategory, const TRigVMTypeIndex InTypeIndex);
	void PropagateTypeAddedToCategory_NoLock(const FRigVMTemplateArgument::ETypeCategory InCategory, const TRigVMTypeIndex InTypeIndex);
	void RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory InCategory, TRigVMTypeIndex InTypeIndex);

	// memory for all (known) types
	TArray<FTypeInfo> Types;
	TMap<FRigVMTemplateArgumentType, TRigVMTypeIndex> TypeToIndex;

	// memory for all functions
	// We use TChunkedArray because we need the memory locations to be stable, since we only ever add and never remove.
	TChunkedArray<FRigVMFunction> Functions;

	// memory for all non-deprecated templates
	TChunkedArray<FRigVMTemplate> Templates;

	// memory for all deprecated templates
	TChunkedArray<FRigVMTemplate> DeprecatedTemplates;

	// memory for all dispatch factories
	TArray<FRigVMDispatchFactory*> Factories;

	// name lookup for functions
	TMap<FName, int32> FunctionNameToIndex;

	// lookup all the predicate functions of this struct
	TMap<FName, TArray<FRigVMFunction>> StructNameToPredicates;

	// name lookup for non-deprecated templates
	TMap<FName, int32> TemplateNotationToIndex;

	// name lookup for deprecated templates
	TMap<FName, int32> DeprecatedTemplateNotationToIndex;

	// Maps storing the default types per type category
	TMap<FRigVMTemplateArgument::ETypeCategory, TArray<TRigVMTypeIndex>> TypesPerCategory;

	// Lookup per type category to know which template to keep in sync
	TMap<FRigVMTemplateArgument::ETypeCategory, TArray<int32>> TemplatesPerCategory;

	// Name loop up for user defined types since they can be deleted.
	// When that happens, it won't be safe to reload deleted assets so only type names are reliable
	TMap<FSoftObjectPath, TRigVMTypeIndex> UserDefinedTypeToIndex;
	
	// All allowed classes
	TSet<TObjectPtr<const UClass>> AllowedClasses;

	// All allowed structs
	TSet<TObjectPtr<const UScriptStruct>> AllowedStructs;
	


	// If this is true the registry is currently refreshing all types
	bool bIsRefreshingEngineTypes;

	// This is true if the engine has ever refreshed the engine types
	bool bEverRefreshedEngineTypes;

	friend struct FRigVMStruct;
	friend struct FRigVMTemplate;
	friend struct FRigVMTemplateArgument;
	friend struct FRigVMDispatchFactory;
};

/**
 * The FRigVMRegistry is used to manage all known function pointers
 * for use in the RigVM. The Register method is called automatically
 * when the static struct is initially constructed for each USTRUCT
 * hosting a RIGVM_METHOD enabled virtual function.
 * 
 * Inheriting from FGCObject to ensure that all type objects cannot be GCed
 */
struct RIGVM_API FRigVMRegistry_RWLock final : public FRigVMRegistry_NoLock
{
	typedef FRigVMRegistry_NoLock Super;
	
protected:

	class RIGVM_API FConditionalScopeLock
	{
	public:
		UE_NODISCARD_CTOR explicit FConditionalScopeLock(const FRigVMRegistry_RWLock& InRegistry, ELockType InLockType, bool bInLockEnabled = true)
		: Registry(const_cast<FRigVMRegistry_RWLock*>(&InRegistry))
		, DesiredLockType(InLockType)
		, bLockEnabled(bInLockEnabled)
		{
			if(bLockEnabled)
			{
				int32 CurrentLockCount = 0;
				if(DesiredLockType == LockType_Read)
				{
					Registry->Lock.ReadLock();

					// fetch_add returns the value preceding the modification
					// so we have to add one manually to get the current value
					CurrentLockCount = Registry->LockCount.fetch_add(1) + 1;
				}
				else if(DesiredLockType == LockType_Write)
				{
					Registry->Lock.WriteLock();

					// fetch_add returns the value preceding the modification
					// so we have to add one manually to get the current value
					CurrentLockCount = Registry->LockCount.fetch_add(1) + 1;
					ensure(CurrentLockCount == 1);
				}

				if(CurrentLockCount == 1)
				{
					Registry->LockType.store(DesiredLockType);
				}
			}
		}

		~FConditionalScopeLock()
		{
			if(bLockEnabled)
			{
				if(DesiredLockType == LockType_Read)
				{
					// fetch_sub returns the value preceding the modification
					// so we have to subtract one manually to get the current value
					const int32 CurrentLockCount = Registry->LockCount.fetch_sub(1) - 1;
					ensure(CurrentLockCount >= 0);
					if(CurrentLockCount == 0)
					{
						Registry->LockType.store(LockType_Invalid);
					}
      				Registry->Lock.ReadUnlock();
				}
				else if(DesiredLockType == LockType_Write)
				{
					// fetch_sub returns the value preceding the modification
					// so we have to subtract one manually to get the current value
					const int32 CurrentLockCount = Registry->LockCount.fetch_sub(1) - 1;
					ensure(CurrentLockCount == 0);
					Registry->LockType.store(LockType_Invalid);
					Registry->Lock.WriteUnlock();
				}
			}
		}

		FRigVMRegistry_NoLock& GetRegistry()
		{
			return *Registry;
		}

		const FRigVMRegistry_NoLock& GetRegistry() const
		{
			return *Registry;
		}

	private:
		FRigVMRegistry_RWLock* Registry;
		ELockType DesiredLockType;
		const bool bLockEnabled;

		UE_NONCOPYABLE(FConditionalScopeLock);
	};

public:

	class RIGVM_API FConditionalReadScopeLock : public FConditionalScopeLock 
	{
	public:
		UE_NODISCARD_CTOR explicit FConditionalReadScopeLock(const FRigVMRegistry_RWLock& InRegistry, bool bInLockEnabled = true)
		: FConditionalScopeLock(InRegistry, LockType_Read, bInLockEnabled)
		{
		}

		UE_NODISCARD_CTOR explicit FConditionalReadScopeLock(bool bInLockEnabled = true)
		: FConditionalReadScopeLock(FRigVMRegistry_RWLock::Get(), bInLockEnabled)
		{
		}
	};

	class RIGVM_API FConditionalWriteScopeLock : public FConditionalScopeLock
	{
	public:
		UE_NODISCARD_CTOR explicit FConditionalWriteScopeLock(const FRigVMRegistry_RWLock& InRegistry, bool bInLockEnabled = true)
		: FConditionalScopeLock(InRegistry, LockType_Write, bInLockEnabled)
		{
		}

		UE_NODISCARD_CTOR explicit FConditionalWriteScopeLock(bool bInLockEnabled = true)
		: FConditionalWriteScopeLock(FRigVMRegistry_RWLock::Get(), bInLockEnabled)
		{
		}
	};

	// Returns the singleton registry
	static FRigVMRegistry_RWLock& Get();

	static FRigVMRegistry_NoLock& Get(ELockType InLockType) = delete;
	static const FRigVMRegistry_NoLock& GetForRead() = delete;
	static FRigVMRegistry_NoLock& GetForWrite() = delete;

	// Registers a function given its name.
	// The name will be the name of the struct and virtual method,
	// for example "FMyStruct::MyVirtualMethod"
	void Register(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct = nullptr, const TArray<FRigVMFunctionArgument>& InArguments = TArray<FRigVMFunctionArgument>(), bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		Super::Register_NoLock(InName, InFunctionPtr, InStruct, InArguments);
	}

	// Registers a dispatch factory given its struct.
	const FRigVMDispatchFactory* RegisterFactory(UScriptStruct* InFactoryStruct, bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::RegisterFactory_NoLock(InFactoryStruct);
	}

	// Register a predicate contained in the input struct
	void RegisterPredicate(UScriptStruct* InStruct, const TCHAR* InName, const TArray<FRigVMFunctionArgument>& InArguments, bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::RegisterPredicate_NoLock(InStruct, InName, InArguments);
	}

	// Register a set of allowed object types
	void RegisterObjectTypes(TConstArrayView<TPair<UClass*, ERegisterObjectOperation>> InClasses, bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		Super::RegisterObjectTypes_NoLock(InClasses);
	}

	// Register a set of allowed struct types
	void RegisterStructTypes(TConstArrayView<UScriptStruct*> InStructs, bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		Super::RegisterStructTypes_NoLock(InStructs);
	}

	// Refreshes the list and finds the function pointers
	// based on the names.
	void RefreshEngineTypes()
	{
		FConditionalWriteScopeLock _(*this);
		Super::RefreshEngineTypes_NoLock();
	}

	// Refreshes the list and finds the function pointers
	// based on the names.
	void RefreshEngineTypesIfRequired(bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		Super::RefreshEngineTypesIfRequired_NoLock();
	}

	// Update the registry when types are renamed
	void OnAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath)
	{
		FConditionalWriteScopeLock _(*this);
		Super::OnAssetRenamed_NoLock(InAssetData, InOldObjectPath);
	}
	
	// Update the registry when old types are removed
    void OnAssetRemoved(const FAssetData& InAssetData);

	// Removes all types associated with a plugin that's being unloaded. 
	void OnPluginUnloaded(IPlugin& InPlugin);

	// Update the registry when new types are added to the attribute system so that they can be selected
	// on Attribute Nodes
	void OnAnimationAttributeTypesChanged(const UScriptStruct* InStruct, bool bIsAdded);

	// Notifies other system that types have been added/removed, and template permutations have been updated
	FOnRigVMRegistryChanged& OnRigVMRegistryChanged() { return OnRigVMRegistryChangedDelegate; }
	
	// Clear the registry
	void Reset(bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		Super::Reset_NoLock();
	}

	// Adds a type if it doesn't exist yet and returns its index.
	// This function is thread-safe
	TRigVMTypeIndex FindOrAddType(const FRigVMTemplateArgumentType& InType, bool bForce = false, bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::FindOrAddType_NoLock(InType, bForce);
	}

	// Removes a type from the registry, and updates all dependent templates
	// which also creates invalid permutations in templates that we should ignore
	bool RemoveType(const FSoftObjectPath& InObjectPath, const UClass* InObjectClass, bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::RemoveType_NoLock(InObjectPath, InObjectClass);
	}

	// Returns the type index given a type
	TRigVMTypeIndex GetTypeIndex(const FRigVMTemplateArgumentType& InType, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetTypeIndex_NoLock(InType);
	}

	// Returns the type index given a cpp type and a type object
	TRigVMTypeIndex GetTypeIndex(const FName& InCPPType, UObject* InCPPTypeObject, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return GetTypeIndex(FRigVMTemplateArgumentType(InCPPType, InCPPTypeObject));
	}

	// Returns the type index given an enum, struct, or object
	template <typename T>
	TRigVMTypeIndex GetTypeIndex(bool bAsArray = false, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetTypeIndex_NoLock<T>(bAsArray);
	}

	// Returns the type given its index
	const FRigVMTemplateArgumentType& GetType(TRigVMTypeIndex InTypeIndex, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetType_NoLock(InTypeIndex);
	}

	// Returns the number of types
	int32 NumTypes(bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::NumTypes_NoLock();
	}

	// Returns the type given only its cpp type
	const FRigVMTemplateArgumentType& FindTypeFromCPPType(const FString& InCPPType, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::FindTypeFromCPPType_NoLock(InCPPType);
	}

	// Returns the type index given only its cpp type
	TRigVMTypeIndex GetTypeIndexFromCPPType(const FString& InCPPType, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetTypeIndexFromCPPType_NoLock(InCPPType);
	}

	// Returns true if the type is an array
	bool IsArrayType(TRigVMTypeIndex InTypeIndex, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::IsArrayType_NoLock(InTypeIndex);
	}

	// Returns true if the type is an execute type
	bool IsExecuteType(TRigVMTypeIndex InTypeIndex, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::IsExecuteType_NoLock(InTypeIndex);
	} 

	// Converts the given execute context type to the base execute context type
	bool ConvertExecuteContextToBaseType(TRigVMTypeIndex& InOutTypeIndex, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::ConvertExecuteContextToBaseType_NoLock(InOutTypeIndex);
	} 

	// Returns the dimensions of the array 
	int32 GetArrayDimensionsForType(TRigVMTypeIndex InTypeIndex, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetArrayDimensionsForType_NoLock(InTypeIndex);
	} 

	// Returns true if the type is a wildcard type
	bool IsWildCardType(TRigVMTypeIndex InTypeIndex) const
	{
		// no lock required
		return Super::IsWildCardType_NoLock(InTypeIndex);
	} 

	// Returns true if the types can be matched.
	bool CanMatchTypes(TRigVMTypeIndex InTypeIndexA, TRigVMTypeIndex InTypeIndexB, bool bAllowFloatingPointCasts, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::CanMatchTypes_NoLock(InTypeIndexA, InTypeIndexB, bAllowFloatingPointCasts);
	}

	// Returns the list of compatible types for a given type
	const TArray<TRigVMTypeIndex>& GetCompatibleTypes(TRigVMTypeIndex InTypeIndex) const
	{
		// no lock required
		return Super::GetCompatibleTypes_NoLock(InTypeIndex);
	}

	// Returns all compatible types given a category
	const TArray<TRigVMTypeIndex>& GetTypesForCategory(FRigVMTemplateArgument::ETypeCategory InCategory, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetTypesForCategory_NoLock(InCategory);
	}

	// Returns the type index of the array matching the given element type index
	TRigVMTypeIndex GetArrayTypeFromBaseTypeIndex(TRigVMTypeIndex InTypeIndex, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetArrayTypeFromBaseTypeIndex_NoLock(InTypeIndex);
	}

	// Returns the type index of the element matching the given array type index
	TRigVMTypeIndex GetBaseTypeFromArrayTypeIndex(TRigVMTypeIndex InTypeIndex, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetBaseTypeFromArrayTypeIndex_NoLock(InTypeIndex);
	}

	// Returns the function given its name (or nullptr)
	const FRigVMFunction* FindFunction(const TCHAR* InName, const FRigVMUserDefinedTypeResolver& InTypeResolver = FRigVMUserDefinedTypeResolver(), bool bLockRegistry = true) const
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::FindFunction_NoLock(InName, InTypeResolver);
	}

	// Returns the function given its backing up struct and method name
	const FRigVMFunction* FindFunction(UScriptStruct* InStruct, const TCHAR* InName, const FRigVMUserDefinedTypeResolver& InResolvalInfo = FRigVMUserDefinedTypeResolver(), bool bLockRegistry = true) const
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::FindFunction_NoLock(InStruct, InName, InResolvalInfo);
	}

	// Returns all current RigVM functions
	const TChunkedArray<FRigVMFunction>& GetFunctions(bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetFunctions_NoLock();
	}

	// Returns a template pointer given its notation (or nullptr)
	const FRigVMTemplate* FindTemplate(const FName& InNotation, bool bIncludeDeprecated = false, bool bLockRegistry = true) const
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::FindTemplate_NoLock(InNotation, bIncludeDeprecated);
	}

	// Returns all current RigVM functions
	const TChunkedArray<FRigVMTemplate>& GetTemplates(bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetTemplates_NoLock();
	}

	// Defines and retrieves a template given its arguments
	const FRigVMTemplate* GetOrAddTemplateFromArguments(
		const FName& InName,
		const TArray<FRigVMTemplateArgumentInfo>& InInfos,
		const FRigVMTemplateDelegates& InDelegates,
		bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::GetOrAddTemplateFromArguments_NoLock(InName, InInfos, InDelegates);
	}

	// Adds a new template given its arguments
	const FRigVMTemplate* AddTemplateFromArguments(
		const FName& InName,
		const TArray<FRigVMTemplateArgumentInfo>& InInfos,
		const FRigVMTemplateDelegates& InDelegates,
		bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::AddTemplateFromArguments_NoLock(InName, InInfos, InDelegates);
	}

	// Returns a dispatch factory given its name (or nullptr)
	FRigVMDispatchFactory* FindDispatchFactory(const FName& InFactoryName, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::FindDispatchFactory_NoLock(InFactoryName);
	}

	// Returns a dispatch factory given its static struct (or nullptr)
	FRigVMDispatchFactory* FindOrAddDispatchFactory(UScriptStruct* InFactoryStruct, bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::FindOrAddDispatchFactory_NoLock(InFactoryStruct);
	}

	// Returns a dispatch factory given its static struct (or nullptr)
	template <
		typename T,
		typename TEnableIf<TModels_V<CRigVMUStruct, T>>::Type * = nullptr
	>
	FRigVMDispatchFactory* FindOrAddDispatchFactory(bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::FindOrAddDispatchFactory_NoLock<T>();
	}

	// Returns a dispatch factory's singleton function name if that exists
	FString FindOrAddSingletonDispatchFunction(UScriptStruct* InFactoryStruct, bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::FindOrAddSingletonDispatchFunction_NoLock(InFactoryStruct);
	}

	// Returns a dispatch factory's singleton function name if that exists
	template <
		typename T,
		typename TEnableIf<TModels_V<CRigVMUStruct, T>>::Type * = nullptr
	>
	FString FindOrAddSingletonDispatchFunction(bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		return Super::FindOrAddSingletonDispatchFunction_NoLock<T>();
	}

	// Returns all dispatch factories
	const TArray<FRigVMDispatchFactory*>& GetFactories(bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetFactories_NoLock();
	}

	// Given a struct name, return the predicates
	const TArray<FRigVMFunction>* GetPredicatesForStruct(const FName& InStructName, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetPredicatesForStruct_NoLock(InStructName);
	}

	// Returns a unique hash per type index
	uint32 GetHashForType(TRigVMTypeIndex InTypeIndex, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetHashForType_NoLock(InTypeIndex);
	}
	 
	uint32 GetHashForScriptStruct(const UScriptStruct* InScriptStruct, bool bCheckTypeIndex = true, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetHashForScriptStruct_NoLock(InScriptStruct, bCheckTypeIndex);
	}
	 
	uint32 GetHashForStruct(const UStruct* InStruct, bool bLockRegistry = true) const
	{
		FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetHashForStruct_NoLock(InStruct);
	}
	 
	uint32 GetHashForEnum(const UEnum* InEnum, bool bCheckTypeIndex = true, bool bLockRegistry = true) const
	{
		const FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetHashForEnum_NoLock(InEnum, bCheckTypeIndex);
	}
	 
	uint32 GetHashForProperty(const FProperty* InProperty, bool bLockRegistry = true) const
	{
		const FConditionalReadScopeLock _(*this, bLockRegistry);
		return Super::GetHashForProperty_NoLock(InProperty);
	}

	void RebuildRegistry(bool bLockRegistry = true)
	{
		FConditionalWriteScopeLock _(*this, bLockRegistry);
		Super::RebuildRegistry_NoLock();
	}

protected:

	// Registers a function given its name.
	// The name will be the name of the struct and virtual method,
	// for example "FMyStruct::MyVirtualMethod"
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual void Register_NoLock(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct = nullptr, const TArray<FRigVMFunctionArgument>& InArguments = TArray<FRigVMFunctionArgument>()) override
	{
		Super::Register_NoLock(InName, InFunctionPtr, InStruct, InArguments);
	}

	// Registers a dispatch factory given its struct.
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const FRigVMDispatchFactory* RegisterFactory_NoLock(UScriptStruct* InFactoryStruct) override
	{
		return Super::RegisterFactory_NoLock(InFactoryStruct);
	}

	// Register a predicate contained in the input struct
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual void RegisterPredicate_NoLock(UScriptStruct* InStruct, const TCHAR* InName, const TArray<FRigVMFunctionArgument>& InArguments) override
	{
		return Super::RegisterPredicate_NoLock(InStruct, InName, InArguments);
	}

	// Register a set of allowed object types
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual void RegisterObjectTypes_NoLock(TConstArrayView<TPair<UClass*, ERegisterObjectOperation>> InClasses) override
	{
		Super::RegisterObjectTypes_NoLock(InClasses);
	}

	// Refreshes the list and finds the function pointers
	// based on the names.
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual void RefreshEngineTypes_NoLock() override
	{
		Super::RefreshEngineTypes_NoLock();
	}

	// Refreshes the list and finds the function pointers
	// based on the names.
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual void RefreshEngineTypesIfRequired_NoLock() override
	{
		Super::RefreshEngineTypesIfRequired_NoLock();
	}

	// Update the registry when types are renamed
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual void OnAssetRenamed_NoLock(const FAssetData& InAssetData, const FString& InOldObjectPath) override
	{
		Super::OnAssetRenamed_NoLock(InAssetData, InOldObjectPath);
	}
	
	// Update the registry when old types are removed
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual bool OnAssetRemoved_NoLock(const FAssetData& InAssetData) override
	{
		return Super::OnAssetRemoved_NoLock(InAssetData);
	}

	// Removes all types associated with a plugin that's being unloaded. 
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual bool OnPluginUnloaded_NoLock(IPlugin& InPlugin) override
	{
		return Super::OnPluginUnloaded_NoLock(InPlugin);
	}
	
	// Update the registry when new types are added to the attribute system so that they can be selected
	// on Attribute Nodes
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual void OnAnimationAttributeTypesChanged_NoLock(const UScriptStruct* InStruct, bool bIsAdded) override
	{
		Super::OnAnimationAttributeTypesChanged_NoLock(InStruct, bIsAdded);
	}
	
	// Clear the registry
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual void Reset_NoLock() override
	{
		Super::Reset_NoLock();
	}

	// Adds a type if it doesn't exist yet and returns its index.
	// This function is thread-safe
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual TRigVMTypeIndex FindOrAddType_NoLock(const FRigVMTemplateArgumentType& InType, bool bForce = false) override
	{
		return Super::FindOrAddType_NoLock(InType, bForce);
	}

	// Removes a type from the registry, and updates all dependent templates
	// which also creates invalid permutations in templates that we should ignore
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual bool RemoveType_NoLock(const FSoftObjectPath& InObjectPath, const UClass* InObjectClass) override
	{
		return Super::RemoveType_NoLock(InObjectPath, InObjectClass);
	}

	// Returns the type index given a type
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual TRigVMTypeIndex GetTypeIndex_NoLock(const FRigVMTemplateArgumentType& InType) const override
	{
		return Super::GetTypeIndex_NoLock(InType);
	}

	// Returns the type index given a cpp type and a type object
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual TRigVMTypeIndex GetTypeIndex_NoLock(const FName& InCPPType, UObject* InCPPTypeObject) const override
	{
		return Super::GetTypeIndex_NoLock(FRigVMTemplateArgumentType(InCPPType, InCPPTypeObject));
	}

	// Returns the type given its index
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const FRigVMTemplateArgumentType& GetType_NoLock(TRigVMTypeIndex InTypeIndex) const override
	{
		return Super::GetType_NoLock(InTypeIndex);
	}

	// Returns the number of types
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual int32 NumTypes_NoLock() const override
	{
		return Super::NumTypes_NoLock();
	}

	// Returns the type given only its cpp type
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const FRigVMTemplateArgumentType& FindTypeFromCPPType_NoLock(const FString& InCPPType) const override
	{
		return Super::FindTypeFromCPPType_NoLock(InCPPType);
	}

	// Returns the type index given only its cpp type
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual TRigVMTypeIndex GetTypeIndexFromCPPType_NoLock(const FString& InCPPType) const override
	{
		return Super::GetTypeIndexFromCPPType_NoLock(InCPPType);
	}

	// Returns true if the type is an array
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual bool IsArrayType_NoLock(TRigVMTypeIndex InTypeIndex) const override
	{
		return Super::IsArrayType_NoLock(InTypeIndex);
	}

	// Returns true if the type is an execute type
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual bool IsExecuteType_NoLock(TRigVMTypeIndex InTypeIndex) const override
	{
		return Super::IsExecuteType_NoLock(InTypeIndex);
	} 

	// Converts the given execute context type to the base execute context type
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual bool ConvertExecuteContextToBaseType_NoLock(TRigVMTypeIndex& InOutTypeIndex) const override
	{
		return Super::ConvertExecuteContextToBaseType_NoLock(InOutTypeIndex);
	} 

	// Returns the dimensions of the array 
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual int32 GetArrayDimensionsForType_NoLock(TRigVMTypeIndex InTypeIndex) const override
	{
		return Super::GetArrayDimensionsForType_NoLock(InTypeIndex);
	} 

	// Returns true if the type is a wildcard type
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual bool IsWildCardType_NoLock(TRigVMTypeIndex InTypeIndex) const override
	{
		return Super::IsWildCardType_NoLock(InTypeIndex);
	} 

	// Returns true if the types can be matched.
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual bool CanMatchTypes_NoLock(TRigVMTypeIndex InTypeIndexA, TRigVMTypeIndex InTypeIndexB, bool bAllowFloatingPointCasts) const override
	{
		return Super::CanMatchTypes_NoLock(InTypeIndexA, InTypeIndexB, bAllowFloatingPointCasts);
	}

	// Returns the list of compatible types for a given type
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const TArray<TRigVMTypeIndex>& GetCompatibleTypes_NoLock(TRigVMTypeIndex InTypeIndex) const override
	{
		return Super::GetCompatibleTypes_NoLock(InTypeIndex);
	}

	// Returns all compatible types given a category
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const TArray<TRigVMTypeIndex>& GetTypesForCategory_NoLock(FRigVMTemplateArgument::ETypeCategory InCategory) const override
	{
		return Super::GetTypesForCategory_NoLock(InCategory);
	}

	// Returns the type index of the array matching the given element type index
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual TRigVMTypeIndex GetArrayTypeFromBaseTypeIndex_NoLock(TRigVMTypeIndex InTypeIndex) const override
	{
		return Super::GetArrayTypeFromBaseTypeIndex_NoLock(InTypeIndex);
	}

	// Returns the type index of the element matching the given array type index
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual TRigVMTypeIndex GetBaseTypeFromArrayTypeIndex_NoLock(TRigVMTypeIndex InTypeIndex) const override
	{
		return Super::GetBaseTypeFromArrayTypeIndex_NoLock(InTypeIndex);
	}

	// Returns the function given its name (or nullptr)
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const FRigVMFunction* FindFunction_NoLock(const TCHAR* InName, const FRigVMUserDefinedTypeResolver& InTypeResolver = FRigVMUserDefinedTypeResolver()) const override
	{
		return Super::FindFunction_NoLock(InName, InTypeResolver);
	}

	// Returns the function given its backing up struct and method name
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const FRigVMFunction* FindFunction_NoLock(UScriptStruct* InStruct, const TCHAR* InName, const FRigVMUserDefinedTypeResolver& InResolvalInfo = FRigVMUserDefinedTypeResolver()) const override
	{
		return Super::FindFunction_NoLock(InStruct, InName, InResolvalInfo);
	}

	// Returns all current RigVM functions
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const TChunkedArray<FRigVMFunction>& GetFunctions_NoLock() const override
	{
		return Super::GetFunctions_NoLock();
	}

	// Returns a template pointer given its notation (or nullptr)
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const FRigVMTemplate* FindTemplate_NoLock(const FName& InNotation, bool bIncludeDeprecated = false) const override
	{
		return Super::FindTemplate_NoLock(InNotation, bIncludeDeprecated);
	}

	// Returns all current RigVM functions
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const TChunkedArray<FRigVMTemplate>& GetTemplates_NoLock() const override
	{
		return Super::GetTemplates_NoLock();
	}

	// Defines and retrieves a template given its arguments
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const FRigVMTemplate* GetOrAddTemplateFromArguments_NoLock(
		const FName& InName,
		const TArray<FRigVMTemplateArgumentInfo>& InInfos,
		const FRigVMTemplateDelegates& InDelegates) override
	{
		return Super::GetOrAddTemplateFromArguments_NoLock(InName, InInfos, InDelegates);
	}

	// Adds a new template given its arguments
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const FRigVMTemplate* AddTemplateFromArguments_NoLock(
		const FName& InName,
		const TArray<FRigVMTemplateArgumentInfo>& InInfos,
		const FRigVMTemplateDelegates& InDelegates) override
	{
		return Super::AddTemplateFromArguments_NoLock(InName, InInfos, InDelegates);
	}

	// Returns a dispatch factory given its name (or nullptr)
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual FRigVMDispatchFactory* FindDispatchFactory_NoLock(const FName& InFactoryName) const override
	{
		return Super::FindDispatchFactory_NoLock(InFactoryName);
	}

	// Returns a dispatch factory given its static struct (or nullptr)
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual FRigVMDispatchFactory* FindOrAddDispatchFactory_NoLock(UScriptStruct* InFactoryStruct) override
	{
		return Super::FindOrAddDispatchFactory_NoLock(InFactoryStruct);
	}

	// Returns a dispatch factory's singleton function name if that exists
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual FString FindOrAddSingletonDispatchFunction_NoLock(UScriptStruct* InFactoryStruct) override
	{
		return Super::FindOrAddSingletonDispatchFunction_NoLock(InFactoryStruct);
	}

	// Returns all dispatch factories
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const TArray<FRigVMDispatchFactory*>& GetFactories_NoLock() const override
	{
		return Super::GetFactories_NoLock();
	}

	// Given a struct name, return the predicates
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual const TArray<FRigVMFunction>* GetPredicatesForStruct_NoLock(const FName& InStructName) const override
	{
		return Super::GetPredicatesForStruct_NoLock(InStructName);
	}

	// Returns a unique hash per type index
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual uint32 GetHashForType_NoLock(TRigVMTypeIndex InTypeIndex) const override
	{
		return Super::GetHashForType_NoLock(InTypeIndex);
	}
	 
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual uint32 GetHashForScriptStruct_NoLock(const UScriptStruct* InScriptStruct, bool bCheckTypeIndex = true) const override
	{
		return Super::GetHashForScriptStruct_NoLock(InScriptStruct, bCheckTypeIndex);
	}
	 
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual uint32 GetHashForStruct_NoLock(const UStruct* InStruct) const override
	{
		return Super::GetHashForStruct_NoLock(InStruct);
	}
	 
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual uint32 GetHashForEnum_NoLock(const UEnum* InEnum, bool bCheckTypeIndex = true) const override
	{
		return Super::GetHashForEnum_NoLock(InEnum, bCheckTypeIndex);
	}
	 
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual uint32 GetHashForProperty_NoLock(const FProperty* InProperty) const override
	{
		return Super::GetHashForProperty_NoLock(InProperty);
	}
	 
	// Note: Only call NoLock methods on the FRigVMRegistry_NoLock
	virtual void RebuildRegistry_NoLock() override
	{
		Super::RebuildRegistry_NoLock();
	}

private:

	FRigVMRegistry_RWLock();

	// Initialize the base types
	virtual void Initialize(bool bLockRegistry) override;

	static void EnsureLocked(ELockType InLockType);

	mutable FRWLock Lock;
	mutable std::atomic<ELockType> LockType;
	mutable std::atomic<int32> LockCount;

	// Notifies other system that types have been added/removed, and template permutations have been updated
	FOnRigVMRegistryChanged OnRigVMRegistryChangedDelegate;
	
	friend struct FRigVMStruct;
	friend struct FRigVMTemplate;
	friend struct FRigVMFunction;
	friend struct FRigVMTemplateArgument;
	friend struct FRigVMDispatchFactory;
	friend struct FRigVMRegistry_NoLock;
};

typedef FRigVMRegistry_RWLock::FConditionalReadScopeLock FRigVMRegistryReadLock;
typedef FRigVMRegistry_RWLock::FConditionalWriteScopeLock FRigVMRegistryWriteLock;
