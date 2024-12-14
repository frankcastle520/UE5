// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "VerseVM/VVMVerseEffectSet.h"
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMClass.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMShape.h"
#endif
#include "VVMVerseClass.generated.h"

class UClassCookedMetaData;

enum EVerseClassFlags
{
	VCLASS_None = 0x00000000u,
	VCLASS_NativeBound = 0x00000001u,
	VCLASS_UniversallyAccessible = 0x00000002u, // The class is accessible from any Verse path, and is in a package with a public scope.
	VCLASS_Concrete = 0x00000004u,              // The class can be instantiated without explicitly setting any properties
	VCLASS_Module = 0x00000008u,                // This class represents a Verse module

	// @TODO: this should be a per-function flag; a class flag is not granular enough
	VCLASS_Err_Inoperable = 0x40000000u, // One or more of the class's functions contain mis-linked (malformed) bytecode

	VCLASS_Err_Incomplete = 0x80000000u, // The class layout is malformed (missing super, illformed data-member, etc.)

	VCLASS_Err = (VCLASS_Err_Incomplete | VCLASS_Err_Inoperable)
};

USTRUCT()
struct COREUOBJECT_API FVersePersistentVar
{
	GENERATED_BODY()

	FVersePersistentVar(FString Path, TFieldPath<FMapProperty> Property)
		: Path(::MoveTemp(Path))
		, Property(::MoveTemp(Property))
	{
	}

	FVersePersistentVar() = default;

	UPROPERTY()
	FString Path;
	UPROPERTY()
	TFieldPath<FMapProperty> Property;
};

USTRUCT()
struct COREUOBJECT_API FVerseSessionVar
{
	GENERATED_BODY()

	explicit FVerseSessionVar(TFieldPath<FMapProperty> Property)
		: Property(::MoveTemp(Property))
	{
	}

	FVerseSessionVar() = default;

	UPROPERTY()
	TFieldPath<FMapProperty> Property;
};

USTRUCT()
struct COREUOBJECT_API FVerseClassVarAccessor
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UFunction> Func{};

	UPROPERTY()
	bool bIsInstanceMember{false};

	UPROPERTY()
	bool bIsFallible{false};
};

USTRUCT()
struct COREUOBJECT_API FVerseClassVarAccessors
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<int, FVerseClassVarAccessor> Getters;

	UPROPERTY()
	TMap<int, FVerseClassVarAccessor> Setters;
};

struct FVerseFunctionDescriptor
{
	UObject* Owner = nullptr;
	UFunction* Function = nullptr; // May be nullptr even when valid
	FName DisplayName = NAME_None;
	FName UEName = NAME_None;

	FVerseFunctionDescriptor() = default;

	FVerseFunctionDescriptor(
		UObject* InOwner,
		UFunction* InFunction,
		FName InDisplayName,
		FName InUEName)
		: Owner(InOwner)
		, Function(InFunction)
		, DisplayName(InDisplayName)
		, UEName(InUEName)
	{
	}

	operator bool() const
	{
		return Owner != nullptr;
	}
};

UCLASS(MinimalAPI, within = Package, Config = Engine)
class UVerseClass : public UClass
{
	GENERATED_BODY()

public:
	//~ Begin UObjectBaseUtility interface
	COREUOBJECT_API virtual UE::Core::FVersePath GetVersePath() const override;
	//~ End UObjectBaseUtility interface

private:
	//~ Begin UObject interface
	COREUOBJECT_API virtual bool IsAsset() const override { return true; }
	COREUOBJECT_API virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;
	COREUOBJECT_API virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
	COREUOBJECT_API virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	//~ End UObject interface

	//~ Begin UStruct interface
	COREUOBJECT_API virtual void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	COREUOBJECT_API virtual void PreloadChildren(FArchive& Ar) override;
	COREUOBJECT_API virtual FProperty* CustomFindProperty(const FName InName) const override;
	COREUOBJECT_API virtual FString GetAuthoredNameForField(const FField* Field) const override;
	//~ End UStruct interface

	//~ Begin UClass interface
	COREUOBJECT_API virtual void PostInitInstance(UObject* InObj, FObjectInstancingGraph* InstanceGraph) override;
	COREUOBJECT_API virtual void PostLoadInstance(UObject* InObj) override;
	COREUOBJECT_API virtual bool CanCreateAssetOfClass() const override
	{
		return false;
	}
#if WITH_EDITORONLY_DATA
	COREUOBJECT_API virtual bool CanCreateInstanceDataObject() const override;
#endif
#if WITH_EDITOR
	COREUOBJECT_API virtual FTopLevelAssetPath GetReinstancedClassPathName_Impl() const;
#endif
	//~ End UClass interface

	// UField interface.
	COREUOBJECT_API virtual const TCHAR* GetPrefixCPP() const override;
	// End of UField interface.

public:
	UPROPERTY()
	uint32 SolClassFlags;

	// All coroutine task classes belonging to this class (one for each coroutine in this class)
	UPROPERTY()
	TArray<TObjectPtr<UVerseClass>> TaskClasses;

	/** Initialization function */
	UPROPERTY()
	TObjectPtr<UFunction> InitInstanceFunction;

	UPROPERTY()
	TArray<FVersePersistentVar> PersistentVars;

	UPROPERTY()
	TArray<FVerseSessionVar> SessionVars;

	UPROPERTY()
	TMap<FName, FVerseClassVarAccessors> VarAccessors;

	UPROPERTY()
	EVerseEffectSet ConstructorEffects;

	UPROPERTY()
	FName MangledPackageVersePath; // Storing as FName since it's shared between classes

	UPROPERTY()
	FString PackageRelativeVersePath;

	//~ This map is technically wrong since the FName is caseless...
	UPROPERTY()
	TMap<FName, FName> DisplayNameToUENameFunctionMap;

#if WITH_VERSE_COMPILER && WITH_EDITORONLY_DATA
	/** Path name this class had before it was marked as DEAD */
	FString PreviousPathName;
#endif // WITH_VERSE_COMPILER && WITH_EDITORONLY_DATA

	// Name of the CDO init function
	COREUOBJECT_API static const FName InitCDOFunctionName;
	COREUOBJECT_API static const FName StructPaddingDummyName;

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	static Verse::VValue LoadField(Verse::FAllocationContext Context, UObject* Object, Verse::VUniqueString& FieldName);
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	Verse::TWriteBarrier<Verse::VShape> Shape;
	Verse::TWriteBarrier<Verse::VClass> Class;
#endif

	/**
	 * Renames default subobjects on a CDO so that they're unique (named after properties they are assigned to)
	 * @param  InObject Object (usually a CDO) whose default sobjects are to be renamed
	 */
	COREUOBJECT_API static void RenameDefaultSubobjects(UObject* InObject);

	// Delegate for detecting unresolved properties during reinstancing
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPropertyRemoved, const UVerseClass* Class, FName PropertyName);
	COREUOBJECT_API static FOnPropertyRemoved OnPropertyRemoved;

	COREUOBJECT_API void SetNeedsSubobjectInstancingForLoadedInstances(bool bNeedsInstancing)
	{
		bNeedsSubobjectInstancingForLoadedInstances = bNeedsInstancing;
	}

	COREUOBJECT_API bool IsUniversallyAccessible() const { return (SolClassFlags & VCLASS_UniversallyAccessible) != VCLASS_None; }
	COREUOBJECT_API bool IsVerseModule() const { return (SolClassFlags & VCLASS_Module) != VCLASS_None; }
	COREUOBJECT_API bool IsConcrete() const { return (SolClassFlags & VCLASS_Concrete) != VCLASS_None; }

	COREUOBJECT_API const FVerseClassVarAccessors* FindAccessors(FName VarName) const
	{
		const UVerseClass* VerseClass = this;
		while (VerseClass)
		{
			if (const FVerseClassVarAccessors* Accessors = VerseClass->VarAccessors.Find(VarName))
			{
				return Accessors;
			}

			VerseClass = Cast<UVerseClass>(VerseClass->GetSuperClass());
		}
		return nullptr;
	}

	/**
	 * Iterates over Verse Function Properties on an object instance and executes a callback with VerseFunction value and its Verse name.
	 * @param Object Object instance to iterate Verse Functions for
	 * @param Operation callback for each of the found Verse Functions. When the callback returns false, iteration is stopped.
	 * @param IterationFlags Additional options used when iterating over Verse Function properties
	 */
	COREUOBJECT_API void ForEachVerseFunction(UObject* Object, TFunctionRef<bool(FVerseFunctionDescriptor)> Operation, EFieldIterationFlags IterationFlags = EFieldIterationFlags::None);

	/**
	 * Returns a VerseFunction value given its display name
	 * @param Object Object instance to iterate Verse Functions for
	 * @param VerseName Display name of the function
	 * @param SearchFlags Additional options used when iterating over Verse Function properties
	 * @return VerseFunction value acquired from the provided Object instance or invalid function value if none was found.
	 */
#if WITH_VERSE_BPVM
	COREUOBJECT_API FVerseFunctionDescriptor FindVerseFunctionByDisplayName(UObject* Object, const FString& DisplayName, EFieldIterationFlags SearchFlags = EFieldIterationFlags::None);
#endif // WITH_VERSE_BPVM

	/**
	 * Returns the number of parameters a verse function takes
	 */
	COREUOBJECT_API static int32 GetVerseFunctionParameterCount(UFunction* Func);

private:
	COREUOBJECT_API void CallInitInstanceFunctions(UObject* InObj, FObjectInstancingGraph* InstanceGraph);
	COREUOBJECT_API void CallPropertyInitInstanceFunctions(UObject* InObj, FObjectInstancingGraph* InstanceGraph);
	COREUOBJECT_API void InstanceNewSubobjects(UObject* InObj);

	COREUOBJECT_API void AddPersistentVars(UObject*);

	COREUOBJECT_API void AddSessionVars(UObject*);

	/** True if this class needs to run subobject instancing on loaded instances of classes (by default the engine does not run subobject instancing on instances that are being loaded) */
	bool bNeedsSubobjectInstancingForLoadedInstances = false;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UClassCookedMetaData> CachedCookedMetaDataPtr;
#endif // WITH_EDITORONLY_DATA
};
