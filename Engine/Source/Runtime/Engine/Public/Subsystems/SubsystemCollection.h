// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Casts.h"
#include "Templates/SubclassOf.h"
#include "UObject/GCObject.h"

class USubsystem;
class UDynamicSubsystem;

DECLARE_LOG_CATEGORY_EXTERN(LogSubsystemCollection, Log, All);

class FSubsystemCollectionBase
{
public:
	/** Initialize the collection of systems, systems will be created and initialized */
	ENGINE_API void Initialize(UObject* NewOuter);

	/* Clears the collection, while deinitializing the systems */
	ENGINE_API void Deinitialize();

	/** Returns true if collection was already initialized */
	bool IsInitialized() const { return Outer != nullptr; }

	/** Get the collection BaseType */
	const UClass* GetBaseType() const { return BaseType; }

	/** 
	 * Only call from Initialize() of Systems to ensure initialization order
	 * Note: Dependencies only work within a collection
	 */
	ENGINE_API USubsystem* InitializeDependency(TSubclassOf<USubsystem> SubsystemClass);

	/**
	 * Only call from Initialize() of Systems to ensure initialization order
	 * Note: Dependencies only work within a collection
	 */
	template <typename TSubsystemClass>
	TSubsystemClass* InitializeDependency()
	{
		return Cast<TSubsystemClass>(InitializeDependency(TSubsystemClass::StaticClass()));
	}

	/** Registers and adds instances of the specified Subsystem class to all existing SubsystemCollections of the correct type.
	 *  Should be used by specific subsystems in plug ins when plugin is activated.
	 */
	static ENGINE_API void ActivateExternalSubsystem(UClass* SubsystemClass);

	/** Unregisters and removed instances of the specified Subsystem class from all existing SubsystemCollections of the correct type.
	 *  Should be used by specific subsystems in plug ins when plugin is deactivated.
	 */
	static ENGINE_API void DeactivateExternalSubsystem(UClass* SubsystemClass);

	/** Collect references held by this collection */
	ENGINE_API void AddReferencedObjects(UObject* Referencer, FReferenceCollector& Collector);
protected:
	struct FSubsystemArray
	{
		TArray<USubsystem*> Subsystems;
		mutable bool bIsIterating = false; // Safety check to avoid removals during iteration but allow index-based iteration
	};

	/** protected constructor - for use by the template only(FSubsystemCollection<TBaseType>) */
	ENGINE_API FSubsystemCollectionBase(UClass* InBaseType);

	/** protected constructor - Use the FSubsystemCollection<TBaseType> class */
	ENGINE_API FSubsystemCollectionBase();
	
	/** destructor will be called from virtual ~FGCObject in GC cleanup **/
	ENGINE_API virtual ~FSubsystemCollectionBase();

	/** Get a Subsystem by type */
	ENGINE_API USubsystem* GetSubsystemInternal(UClass* SubsystemClass) const;

	UE_DEPRECATED(5.4, "This function is unsafe for re-entrancy and has been deprecated. Use ForEachSubsystemOfClass or GetSubsystemArrayCopy instead")
	/** Get a list of Subsystems by type */
	ENGINE_API const TArray<USubsystem*>& GetSubsystemArrayInternal(UClass* SubsystemClass) const;

	// Fetch a list of subsystems that derive from the given class, populating the cache if necessary
	ENGINE_API FSubsystemArray& FindAndPopulateSubsystemArrayInternal(UClass* SubsystemClass) const;

	/** Get a list of Subsystems by type */
	ENGINE_API TArray<USubsystem*> GetSubsystemArrayCopy(UClass* SubsystemClass) const;

	/** 
	 *  Run the given operation on each registered subsystem.
	 *  Any new subsystems registered during this operation will also be visited.
	 *  It is not permitted to remove subsystems (e.g. by calling DeactivateExternalSubsystem) during this operation.
	 */
	ENGINE_API void ForEachSubsystem(TFunctionRef<void(USubsystem*)> Operation) const;

	/** Perform an operation on all subsystems that derive from the given class */
	ENGINE_API void ForEachSubsystemOfClass(UClass* SubsystemClass, TFunctionRef<void(USubsystem*)> Operation) const;

private:
	ENGINE_API USubsystem* AddAndInitializeSubsystem(UClass* SubsystemClass);

	ENGINE_API void RemoveAndDeinitializeSubsystem(USubsystem* Subsystem);

	TMap<TObjectPtr<UClass>, TObjectPtr<USubsystem>> SubsystemMap;

	mutable TMap<UClass*, TUniquePtr<FSubsystemArray>> SubsystemArrayMap;

	UClass* BaseType;

	UObject* Outer;

	bool bPopulating;
	mutable bool bIterating = false; // True if iterating over SubsystemMap

private:
	friend class FSubsystemModuleWatcher;

	/** Add Instances of the specified Subsystem class to all existing SubsystemCollections of the correct type */
	static ENGINE_API void AddAllInstances(UClass* SubsystemClass);

	/** Remove Instances of the specified Subsystem class from all existing SubsystemCollections of the correct type */
	static ENGINE_API void RemoveAllInstances(UClass* SubsystemClass);
};

template<typename TBaseType>
class FSubsystemCollection : public FSubsystemCollectionBase, public FGCObject
{
public:
	/** Get a Subsystem by type */
	template <typename TSubsystemClass>
	TSubsystemClass* GetSubsystem(const TSubclassOf<TSubsystemClass>& SubsystemClass) const
	{
		static_assert(TIsDerivedFrom<TSubsystemClass, TBaseType>::IsDerived, "TSubsystemClass must be derived from TBaseType");

		// A static cast is safe here because we know SubsystemClass derives from TSubsystemClass if it is not null
		return static_cast<TSubsystemClass*>(GetSubsystemInternal(SubsystemClass));
	}

	/** Get a list of Subsystems by type */
	template <typename TSubsystemClass>
	UE_DEPRECATED(5.4, "This function is unsafe for re-entrancy and has been deprecated. Use ForEachSubsystem or GetSubsystemArrayCopy instead")
	const TArray<TSubsystemClass*>& GetSubsystemArray(const TSubclassOf<TSubsystemClass>& SubsystemClass) const
	{
		// Force a compile time check that TSubsystemClass derives from TBaseType, the internal code only enforces it's a USubsystem
		TSubclassOf<TBaseType> SubsystemBaseClass = SubsystemClass;

		FSubsystemArray& Array = FindAndPopulateSubsystemArrayInternal(SubsystemBaseClass);
		const TArray<TSubsystemClass*>* SpecificArray = reinterpret_cast<const TArray<TSubsystemClass*>*>(&Array.Subsystems);
		return *SpecificArray;
	}

	/** Get a list of Subsystems by type */
	template <typename TSubsystemClass>
	TArray<TSubsystemClass*> GetSubsystemArrayCopy(const TSubclassOf<TSubsystemClass>& SubsystemClass) const
	{
		// Force a compile time check that TSubsystemClass derives from TBaseType, the internal code only enforces it's a USubsystem
		TSubclassOf<TBaseType> SubsystemBaseClass = SubsystemClass;

		FSubsystemArray& Array = FindAndPopulateSubsystemArrayInternal(SubsystemBaseClass);
		return TArray<TSubsystemClass*>(reinterpret_cast<TSubsystemClass**>(Array.Subsystems.GetData()), Array.Subsystems.Num());
	}

	/** Perform an operation on all subsystems of a given type in the collection */
	void ForEachSubsystem(TFunctionRef<void(TBaseType*)> Operation, const TSubclassOf<TBaseType>& SubsystemClass = {}) const
	{
		// Force a compile time check that TSubsystemClass derives from TBaseType, the internal code only enforces it's a USubsystem
		ForEachSubsystemOfClass(SubsystemClass , [Operation=MoveTemp(Operation)](USubsystem* Subsystem){
			Operation(CastChecked<TBaseType>(Subsystem));
		});
	}

	/* FGCObject Interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		FSubsystemCollectionBase::AddReferencedObjects(nullptr, Collector);
	}

	virtual FString GetReferencerName() const override
	{
		return TEXT("FSubsystemCollection");
	}
	
public:

	/** Construct a FSubsystemCollection, pass in the owning object almost certainly (this). */
	FSubsystemCollection()
		: FSubsystemCollectionBase(TBaseType::StaticClass())
	{
	}
};

/** Subsystem collection which delegates UObject references to its owning UObject (object needs to implement AddReferencedObjects and forward call to Collection */
template<typename TBaseType>
class FObjectSubsystemCollection : public FSubsystemCollectionBase
{
public:
	/** Get a Subsystem by type */
	template <typename TSubsystemClass>
	TSubsystemClass* GetSubsystem(const TSubclassOf<TSubsystemClass>& SubsystemClass) const
	{
		static_assert(TIsDerivedFrom<TSubsystemClass, TBaseType>::IsDerived, "TSubsystemClass must be derived from TBaseType");

		// A static cast is safe here because we know SubsystemClass derives from TSubsystemClass if it is not null
		return static_cast<TSubsystemClass*>(GetSubsystemInternal(SubsystemClass));
	}

	/** Get a list of Subsystems by type */
	template <typename TSubsystemClass>
	UE_DEPRECATED(5.4, "This function is unsafe for re-entrancy and has been deprecated. Use ForEachSubsystem or GetSubsystemArrayCopy instead")
	const TArray<TSubsystemClass*>& GetSubsystemArray(const TSubclassOf<TSubsystemClass>& SubsystemClass) const
	{
		// Force a compile time check that TSubsystemClass derives from TBaseType, the internal code only enforces it's a USubsystem
		TSubclassOf<TBaseType> SubsystemBaseClass = SubsystemClass;

		FSubsystemArray& Array = FindAndPopulateSubsystemArrayInternal(SubsystemBaseClass);
		return TArray<TSubsystemClass*>(reinterpret_cast<TSubsystemClass**>(Array.Subsystems.GetData()), Array.Subsystems.Num());
	}

	/** Get a list of Subsystems by type */
	template <typename TSubsystemClass>
	TArray<TSubsystemClass*> GetSubsystemArrayCopy(const TSubclassOf<TSubsystemClass>& SubsystemClass) const
	{
		// Force a compile time check that TSubsystemClass derives from TBaseType, the internal code only enforces it's a USubsystem
		TSubclassOf<TBaseType> SubsystemBaseClass = SubsystemClass;

		FSubsystemArray& Array = FindAndPopulateSubsystemArrayInternal(SubsystemBaseClass);
		return TArray<TSubsystemClass*>(reinterpret_cast<TSubsystemClass**>(Array.Subsystems.GetData()), Array.Subsystems.Num());
	}

	/** Perform an operation on all subsystems in the collection */
	void ForEachSubsystem(TFunctionRef<void(TBaseType*)> Operation, const TSubclassOf<TBaseType>& SubsystemClass = {}) const
	{
		ForEachSubsystemOfClass(SubsystemClass, [Operation=MoveTemp(Operation)](USubsystem* Subsystem){
			Operation(CastChecked<TBaseType>(Subsystem));
		});
	}

	template <typename TSubsystemInterface>
	void ForEachSubsystemWithInterface(TFunctionRef<void(TBaseType*)> Operation) const
	{
		UClass* SubsystemInterfaceClass = TSubsystemInterface::StaticClass();
		ForEachSubsystemOfClass(SubsystemInterfaceClass, [Operation = MoveTemp(Operation)](USubsystem* Subsystem)
		{
			Operation(CastChecked<TBaseType>(Subsystem));
		});
	}

public:

	/** Construct a FSubsystemCollection, pass in the owning object almost certainly (this). */
	FObjectSubsystemCollection()
		: FSubsystemCollectionBase(TBaseType::StaticClass())
	{
	}
};

