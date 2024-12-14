// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StructUtils.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/StructUtilsTypes.h"

#include "SharedStruct.generated.h"

struct FInstancedStruct;
template<typename BaseStructT> struct TInstancedStruct;
template<typename BaseStructT> struct TStructView;
struct FConstStructView;
template<typename BaseStructT> struct TConstStructView;

///////////////////////////////////////////////////////////////// FStructSharedMemory /////////////////////////////////////////////////////////////////

/**
 * Holds the information and memory about a UStruct. Instances of these are shared using FConstSharedStruct and FSharedStruct.
 * 
 * The size of the allocation for this struct always includes both the size of the struct and also the size required to hold the
 * structure described by the ScriptStruct. This avoids two pointer referencing (cache misses). 
 * Look at Create() to understand more.
 * 
 * A 'const FStructSharedMemory' the memory is immutable. We restrict shallow copies of StructMemory where its not appropriate
 * in the owning types that compose this type. ie:
 * - FSharedStruct A; ConstSharedStruct B = A; is allowed
 * - ConstSharedStruct A; FSharedStruct B = A; is not allowed
 * 
 * This type is designed to be used in composition and should not be used outside of the types that compose it.
 */
struct COREUOBJECT_API FStructSharedMemory
{
	~FStructSharedMemory()
	{
		ScriptStruct->DestroyStruct(GetMutableMemory());
	}

	FStructSharedMemory(const FStructSharedMemory& Other) = delete;
	FStructSharedMemory(const FStructSharedMemory&& Other) = delete;
	FStructSharedMemory& operator=(const FStructSharedMemory& Other) = delete;
	FStructSharedMemory& operator=(const FStructSharedMemory&& Other) = delete;

	struct FStructSharedMemoryDeleter
	{
		FORCEINLINE void operator()(FStructSharedMemory* StructSharedMemory) const
		{
			FMemory::Free(StructSharedMemory);
		}
	};

	static TSharedPtr<FStructSharedMemory> Create(const UScriptStruct& InScriptStruct, const uint8* InStructMemory = nullptr)
	{
		FStructSharedMemory* const StructMemory = CreateImpl(InScriptStruct, InStructMemory);

		return MakeShareable(StructMemory, FStructSharedMemoryDeleter());
	}

	template<typename T, typename... TArgs>
	static TSharedPtr<FStructSharedMemory> CreateArgs(TArgs&&... InArgs)
	{
		UE::StructUtils::CheckStructType<T>();

		FStructSharedMemory* const StructMemory = CreateImpl(*TBaseStructure<T>::Get());

		new (StructMemory->GetMutableMemory()) T(Forward<TArgs>(InArgs)...);
		return MakeShareable(StructMemory, FStructSharedMemoryDeleter());
	}

	/** Returns pointer to aligned struct memory. */
	const uint8* GetMemory() const
	{
		return Align((uint8*)StructMemory, ScriptStruct->GetMinAlignment());
	}

	/** Returns mutable pointer to aligned struct memory. */
	uint8* GetMutableMemory()
	{
		return Align((uint8*)StructMemory, ScriptStruct->GetMinAlignment());
	}

	/** Returns struct type. */
	const UScriptStruct& GetScriptStruct() const
	{
		return *ObjectPtrDecay(ScriptStruct);
	}

	TObjectPtr<const UScriptStruct>& GetScriptStructPtr() 
	{
		return ScriptStruct;
	}

private:
	FStructSharedMemory(const UScriptStruct& InScriptStruct, const uint8* InStructMemory = nullptr)
		: ScriptStruct(InScriptStruct)
	{
		ScriptStruct->InitializeStruct(GetMutableMemory());
		
		if (InStructMemory)
		{
			ScriptStruct->CopyScriptStruct(GetMutableMemory(), InStructMemory);
		}
	}

	static FStructSharedMemory* CreateImpl(const UScriptStruct& InScriptStruct, const uint8* InStructMemory = nullptr)
	{
		// Align RequiredSize to InScriptStruct's alignment to effectively add padding in between ScriptStruct and
		// StructMemory. GetMemory will then round &StructMemory up past this 'padding' to the nearest aligned address.
		const int32 RequiredSize = static_cast<int32>(Align(sizeof(FStructSharedMemory), InScriptStruct.GetMinAlignment())) + InScriptStruct.GetStructureSize();
		// Code analysis is unable to understand correctly what we are doing here, so disabling the warning C6386: Buffer overrun while writing to...
		CA_SUPPRESS( 6386 )
		FStructSharedMemory* StructMemory = new(FMemory::Malloc(RequiredSize, InScriptStruct.GetMinAlignment())) FStructSharedMemory(InScriptStruct, InStructMemory);
		return StructMemory;
	}

	TObjectPtr<const UScriptStruct> ScriptStruct;

	/**
	 * Memory for the struct described by ScriptStruct will be allocated here using the 'Flexible array member' pattern.
	 * Access this using GetMutableMemory() / GetMemory() to account for memory alignment.
	 */ 
	uint8 StructMemory[0];
};

///////////////////////////////////////////////////////////////// FSharedStruct /////////////////////////////////////////////////////////////////
/**
 * FSharedStruct works similarly as a TSharedPtr<FInstancedStruct> but avoids the double pointer indirection.
 * (One pointer for the FInstancedStruct and one pointer for the struct memory it is wrapping).
 * Also note that because of its implementation, it is not possible for now to go from a struct reference or struct view back to a shared struct.
 *
 * This struct type is also convertible to a FStructView / FConstStructView and like FInstancedStruct it is the preferable way of passing it as a parameter.
 * If the calling code would like to keep a shared pointer to the struct, you may pass the FSharedStruct as a parameter but it is recommended to pass it as
 * a "const FSharedStruct&" to limit the unnecessary recounting.
 * 
 * A 'const FSharedStruct' can not be made to point at another instance of a struct, whilst a vanila FSharedStruct can. In either case the shared struct memory /data is
 * mutable.
 *
 */
USTRUCT()
struct COREUOBJECT_API FSharedStruct
{
	GENERATED_BODY();

	friend struct FConstSharedStruct;

	FSharedStruct() = default;

	UE_DEPRECATED(5.3, "Use Make() or InitializeAs() instead")
	explicit FSharedStruct(const struct FConstStructView& InOther); // Note passing view by reference on purpose to allow code to compile

	~FSharedStruct()
	{
	}

	/** Copy constructors */
	FSharedStruct(const FSharedStruct& InOther) = default;
	FSharedStruct(FSharedStruct&& InOther) = default;

	/** Assignment operators */
	FSharedStruct& operator=(const FSharedStruct& InOther) = default;
	FSharedStruct& operator=(FSharedStruct&& InOther) = default;

	UE_DEPRECATED(5.3, "Use Make() instead")
	FSharedStruct& operator=(const struct FConstStructView& InOther); // Note passing view by reference on purpose to allow code to compile

	/** For StructOpsTypeTraits */
	bool Identical(const FSharedStruct* Other, uint32 PortFlags) const;
	void AddStructReferencedObjects(class FReferenceCollector& Collector);

	/** Returns struct type. */
	const UScriptStruct* GetScriptStruct() const
	{
		return StructMemoryPtr ? &(StructMemoryPtr.Get()->GetScriptStruct()) : nullptr;
	}

	TObjectPtr<const UScriptStruct>* const GetScriptStructPtr() const
	{
		return StructMemoryPtr ? &StructMemoryPtr.Get()->GetScriptStructPtr() : nullptr;
	}

	/** Returns a mutable pointer to struct memory. */
	uint8* GetMemory() const
	{
		return StructMemoryPtr ? StructMemoryPtr.Get()->GetMutableMemory() : nullptr;
	}

	/** Reset to empty. */
	void Reset()
	{
		StructMemoryPtr.Reset();
	}

	/** Initializes from templated struct type. This will create a new instance of the shared struct memory. */
	template<typename T>
	void InitializeAs()
	{
		UE::StructUtils::CheckStructType<T>();

		InitializeAs(TBaseStructure<T>::Get(), nullptr);
	}

	/** Initializes from templated struct instance. This will create a new instance of the shared struct memory. */
	template <typename T, UE::StructUtils::EnableIfNotSharedInstancedOrViewStruct<T>* = nullptr>
	void InitializeAs(const T& Struct)
	{
		InitializeAs(TBaseStructure<T>::Get(), reinterpret_cast<const uint8*>(&Struct));
	}

	/** Initializes from other related struct types. This will create a new instance of the shared struct memory. */
	template <typename T, UE::StructUtils::EnableIfSharedInstancedOrViewStruct<T>* = nullptr>
	void InitializeAs(const T& Struct)
	{
		InitializeAs(Struct.GetScriptStruct(), Struct.GetMemory());
	}

	/** Initializes from struct type and optional data. This will create a new instance of the shared struct memory. */
	void InitializeAs(const UScriptStruct* InScriptStruct, const uint8* InStructMemory = nullptr)
	{
		if (InScriptStruct)
		{
			StructMemoryPtr = FStructSharedMemory::Create(*InScriptStruct, InStructMemory);
		}
		else
		{
			Reset();
		}
	}

	/** Initializes from struct type and emplace args. This will create a new instance of the shared struct memory.*/
	template<typename T, typename... TArgs>
	void InitializeAs(TArgs&&... InArgs)
	{
		UE::StructUtils::CheckStructType<T>();

		StructMemoryPtr = FStructSharedMemory::CreateArgs<T>(Forward<TArgs>(InArgs)...);
	}

	/** Creates a new FSharedStruct from templated struct type. This will create a new instance of the shared struct memory. */
	template<typename T>
	static FSharedStruct Make()
	{
		FSharedStruct SharedStruct;
		SharedStruct.InitializeAs<T>();
		return SharedStruct;
	}

	/** Creates a new FSharedStruct from templated struct instance. This will create a new instance of the shared struct memory. */
	template<typename T>
	static FSharedStruct Make(const T& Struct)
	{
		FSharedStruct SharedStruct;
		SharedStruct.InitializeAs(Struct);
		return SharedStruct;
	}

	/** Creates a new FSharedStruct from struct type and optional instance memory. This will create a new instance of the shared struct memory. */
	static FSharedStruct Make(const UScriptStruct* InScriptStruct, const uint8* InStructMemory = nullptr)
	{
		FSharedStruct SharedStruct;
		SharedStruct.InitializeAs(InScriptStruct, InStructMemory);
		return SharedStruct;
	}

	/** Creates a new FSharedStruct from the templated type and forward all arguments to constructor. This will create a new instance of the shared struct memory. */
	template<typename T, typename... TArgs>
	static FSharedStruct Make(TArgs&&... InArgs)
	{
		FSharedStruct SharedStruct;
		SharedStruct.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		return SharedStruct;
	}

	/** Returns reference to the struct, this getter assumes that all data is valid. */
	template<typename T>
	T& Get() const
	{
		return UE::StructUtils::GetStructRef<T>(GetScriptStruct(), GetMemory());
	}

	/** Returns pointer to the struct, or nullptr if cast is not valid. */
	template<typename T>
	T* GetPtr() const
	{
		return UE::StructUtils::GetStructPtr<T>(GetScriptStruct(), GetMemory());
	}

	/** Returns a mutable pointer to struct memory. */
	UE_DEPRECATED(5.3, "Use GetMemory() instead")
	uint8* GetMutableMemory()
	{
		return GetMemory();
	}

	/** Returns mutable reference to the struct, this getter assumes that all data is valid. */
	template<typename T>
	UE_DEPRECATED(5.3, "Use Get() instead")
	T& GetMutable()
	{
		return Get<T>();
	}

	/** Returns mutable pointer to the struct, or nullptr if cast is not valid. */
	template<typename T>
	UE_DEPRECATED(5.3, "Use GetPtr() instead")
	T* GetMutablePtr()
	{
		return GetPtr<T>();
	}

	/** Returns True if the struct is valid.*/
	bool IsValid() const
	{
		return GetMemory() != nullptr && GetScriptStruct() != nullptr;
	}

	/** Comparison operators. Note: it does not compare the internal structure itself*/
	template <typename OtherType>
	bool operator==(const OtherType& Other) const
	{
		return ((GetScriptStruct() == Other.GetScriptStruct()) && (GetMemory() == Other.GetMemory()));
	}

	template <typename OtherType>
	bool operator!=(const OtherType& Other) const
	{
		return !operator==(Other);
	}

	/**
	 * Determines whether Other contains same values as `this`
	 * @return whether the values stored are equal
	 */
	template <typename OtherType>
	bool CompareStructValues(const OtherType& Other, uint32 PortFlags = 0) const
	{
		UE::StructUtils::CheckWrapperType<OtherType>();
		const UScriptStruct* ScriptStruct = GetScriptStruct();
		return (ScriptStruct == Other.GetScriptStruct())
			&& (!ScriptStruct || ScriptStruct->CompareScriptStruct(GetMemory(), Other.GetMemory(), PortFlags));
	}

protected:
	TSharedPtr<FStructSharedMemory> StructMemoryPtr;
};

/**
 * TSharedStruct is a type-safe FSharedStruct wrapper against the given BaseStruct type.
 * @note When used as a property, this automatically defines the BaseStruct property meta-data.
 * 
 * Example:
 *
 *	TSharedStruct<FTestStructBase> Test;
 *
 *	TArray<TSharedStruct<FTestStructBase>> TestArray;
 */
template<typename BaseStructT>
struct TSharedStruct
{
	explicit TSharedStruct() = default;

	/** Copy constructors */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	TSharedStruct(const TSharedStruct<T>& InOther)
		: SharedStruct(InOther.SharedStruct)
	{
	}

	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	TSharedStruct(TSharedStruct<T>&& InOther)
		: SharedStruct(MoveTemp(InOther.SharedStruct))
	{
	}

	/** Assignment operators */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	TSharedStruct& operator=(const TSharedStruct<T>& InOther)
	{
		if (this != &InOther)
		{
			SharedStruct = InOther.SharedStruct;
		}
		return *this;
	}

	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	TSharedStruct& operator=(TSharedStruct<T>&& InOther)
	{
		if (this != &InOther)
		{
			SharedStruct = MoveTemp(InOther.SharedStruct);
		}
		return *this;
	}

	/** Returns struct type. */
	const UScriptStruct* GetScriptStruct() const
	{
		return SharedStruct.GetScriptStruct();
	}

	TObjectPtr<const UScriptStruct>* const GetScriptStructPtr() const
	{
		return SharedStruct.GetScriptStructPtr();
	}

	/** Returns a mutable pointer to struct memory. */
	uint8* GetMemory() const
	{
		return SharedStruct.GetMemory();
	}

	/** Reset to empty. */
	void Reset()
	{
		SharedStruct.Reset();
	}

	/** Initializes from templated struct type. This will create a new instance of the shared struct memory. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	void Initialize()
	{
		SharedStruct.InitializeAs<T>();
	}

	/** Initializes from templated struct instance. This will create a new instance of the shared struct memory. */
	template<typename T = BaseStructT,
		typename = std::enable_if_t<
			std::is_base_of_v<BaseStructT, std::decay_t<T>>
			&& !(std::is_same_v<TStructView<T>, T>
				|| std::is_same_v<TConstStructView<T>, T>
				|| std::is_same_v<TSharedStruct<T>, T>
				|| std::is_same_v<TConstSharedStruct<T>, T>
				|| std::is_same_v<TInstancedStruct<T>, T>), T>>
	void Initialize(const T& Struct)
	{
		SharedStruct.InitializeAs(TBaseStructure<T>::Get(), reinterpret_cast<const uint8*>(&Struct));
	}

	/** Initializes from struct type and emplace args. This will create a new instance of the shared struct memory.*/
	template<typename T = BaseStructT, typename... TArgs, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	void Initialize(TArgs&&... InArgs)
	{
		SharedStruct.InitializeAs(Forward<TArgs>(InArgs)...);
	}

	/** Creates a new TSharedStruct. This will create a new instance of the shared struct memory. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	static TSharedStruct Make()
	{
		return FSharedStruct::Make<T>();
	}

	/** Creates a new TSharedStruct from templated struct instance. This will create a new instance of the shared struct memory. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	static TSharedStruct Make(const T& Struct)
	{
		return FSharedStruct::Make<T>(Struct);
	}

	/** Creates a new TSharedStruct from the templated type and forward all arguments to constructor. This will create a new instance of the shared struct memory. */
	template< typename... TArgs>
	static TSharedStruct Make(TArgs&&... InArgs)
	{
		return FSharedStruct::Make<BaseStructT>(Forward<TArgs>(InArgs)...);
	}

	/** Returns reference to the struct, this getter assumes that all data is valid. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	T& Get() const
	{
		return SharedStruct.Get<T>();
	}

	/** Returns pointer to the struct, or nullptr if cast is not valid. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	T* GetPtr() const
	{
		return SharedStruct.GetPtr<T>();
	}

	/** Returns True if the struct is valid.*/
	bool IsValid() const
	{
		return SharedStruct.IsValid();
	}

	/** Comparison operators. Note: it does not compare the internal structure itself */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	bool operator==(const T& Other) const
	{
		return SharedStruct.operator==(Other);
	}

	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	bool operator!=(const T& Other) const
	{
		return SharedStruct.operator!=(Other);
	}

private:
	/**
	 * Note:
	 *   TSharedStruct is a wrapper for a FSharedStruct (rather than inheriting) so that it can provide a locked-down type-safe 
	 *   API for use in C++, without being able to accidentally take a reference to the untyped API to workaround the restrictions.
	 * 
	 *   TSharedStruct MUST be the same size as FSharedStruct, as the reflection layer treats a TSharedStruct as a FSharedStruct.
	 *   This means that any reflected APIs (like ExportText) that accept an FSharedStruct pointer can also accept a TSharedStruct pointer.
	 */
	FSharedStruct SharedStruct;

	template <typename U> friend struct TSharedStruct;
};

template<>
struct TStructOpsTypeTraits<FSharedStruct> : public TStructOpsTypeTraitsBase2<FSharedStruct>
{
	enum
	{
		WithIdentical = true,
		WithAddStructReferencedObjects = true,
	};
};

///////////////////////////////////////////////////////////////// FConstSharedStruct /////////////////////////////////////////////////////////////////
/**
 * FConstSharedStruct is the same as the FSharedStruct but restricts the API to return const struct type. 
 * 
 * A 'const FConstSharedStruct' can not be made to point at another instance of a struct, whilst a vanila FConstSharedStruct can. In either case the struct data is
 * immutable.
 * 
 * See FSharedStruct for more information.
 */
USTRUCT()
struct COREUOBJECT_API FConstSharedStruct
{
	GENERATED_BODY();

	FConstSharedStruct() = default;

	FConstSharedStruct(const FConstSharedStruct& Other) = default;
	FConstSharedStruct(const FSharedStruct& SharedStruct)
		: StructMemoryPtr(SharedStruct.StructMemoryPtr)
	{}

	FConstSharedStruct(FConstSharedStruct&& Other) = default;
	FConstSharedStruct(FSharedStruct&& SharedStruct)
		: StructMemoryPtr(MoveTemp(SharedStruct.StructMemoryPtr))
	{}

	FConstSharedStruct& operator=(const FConstSharedStruct& Other) = default;
	FConstSharedStruct& operator=(const FSharedStruct& SharedStruct)
	{
		StructMemoryPtr = SharedStruct.StructMemoryPtr;
		return *this;
	}

	FConstSharedStruct& operator=(FConstSharedStruct&& Other) = default;
	FConstSharedStruct& operator=(FSharedStruct&& InSharedStruct)
	{
		StructMemoryPtr = MoveTemp(InSharedStruct.StructMemoryPtr);
		return *this;
	}

	/** For StructOpsTypeTraits */
	bool Identical(const FConstSharedStruct* Other, uint32 PortFlags) const;
	void AddStructReferencedObjects(class FReferenceCollector& Collector);

	/** Returns struct type. */
	const UScriptStruct* GetScriptStruct() const
	{
		return StructMemoryPtr ? &(StructMemoryPtr.Get()->GetScriptStruct()) : nullptr;
	}

	TObjectPtr<const UScriptStruct>* GetScriptStructPtr()
	{
		return StructMemoryPtr ?
			&const_cast<FStructSharedMemory*>(StructMemoryPtr.Get())->GetScriptStructPtr() : nullptr;
	}

	/** Returns const pointer to struct memory. */
	const uint8* GetMemory() const
	{
		return StructMemoryPtr ? StructMemoryPtr.Get()->GetMemory() : nullptr;
	}

	/** Reset to empty. */
	void Reset()
	{
		StructMemoryPtr.Reset();
	}

	/** Initializes from templated struct type. */
	template<typename T>
	void InitializeAs()
	{
		UE::StructUtils::CheckStructType<T>();

		InitializeAs(TBaseStructure<T>::Get(), nullptr);
	}

	/** Initializes from templated struct instance. This will create a new instance of the shared struct memory. */
	template <typename T, UE::StructUtils::EnableIfNotSharedInstancedOrViewStruct<T>* = nullptr>
	void InitializeAs(const T& Struct)
	{
		InitializeAs(TBaseStructure<T>::Get(), reinterpret_cast<const uint8*>(&Struct));
	}

	/** Initializes from other related struct types. This will create a new instance of the shared struct memory. */
	template <typename T, UE::StructUtils::EnableIfSharedInstancedOrViewStruct<T>* = nullptr>
	void InitializeAs(const T& Struct)
	{
		InitializeAs(Struct.GetScriptStruct(), Struct.GetMemory());
	}

	/** Initializes from struct type and optional data. This will create a new instance of the shared struct memory. */
	void InitializeAs(const UScriptStruct* InScriptStruct, const uint8* InStructMemory = nullptr)
	{
		if (InScriptStruct)
		{
			StructMemoryPtr = FStructSharedMemory::Create(*InScriptStruct, InStructMemory);
		}
		else
		{
			Reset();
		}
	}

	/** Initializes from struct type and emplace args. This will create a new instance of the shared struct memory. */
	template<typename T, typename... TArgs>
	void InitializeAs(TArgs&&... InArgs)
	{
		UE::StructUtils::CheckStructType<T>();

		StructMemoryPtr = FStructSharedMemory::CreateArgs<T>(Forward<TArgs>(InArgs)...);
	}

	/** Creates a new FSharedStruct from templated struct type. This will create a new instance of the shared struct memory. */
	template<typename T>
	static FConstSharedStruct Make()
	{
		FConstSharedStruct SharedStruct;
		SharedStruct.InitializeAs<T>();
		return SharedStruct;
	}

	/** Creates a new FSharedStruct from templated struct instance. This will create a new instance of the shared struct memory. */
	template<typename T>
	static FConstSharedStruct Make(const T& Struct)
	{
		FConstSharedStruct SharedStruct;
		SharedStruct.InitializeAs<T>(Struct);
		return SharedStruct;
	}

	/** Creates a new FSharedStruct from struct type and optional data. This will create a new instance of the shared struct memory. */
	static FConstSharedStruct Make(const UScriptStruct* InScriptStruct, const uint8* InStructMemory = nullptr)
	{
		FConstSharedStruct SharedStruct;
		SharedStruct.InitializeAs(InScriptStruct, InStructMemory);
		return SharedStruct;
	}

	/** Creates a new FSharedStruct from the templated type and forward all arguments to constructor. This will create a new instance of the shared struct memory. */
	template<typename T, typename... TArgs>
	static FConstSharedStruct Make(TArgs&&... InArgs)
	{
		FConstSharedStruct SharedStruct;
		SharedStruct.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		return SharedStruct;
	}

	/** Returns const reference to the struct, this getter assumes that all data is valid. */
	template<typename T>
	constexpr typename TEnableIf<TIsConst<T>::Value, T&>::Type Get() const
	{
		return UE::StructUtils::GetStructRef<T>(GetScriptStruct(), GetMemory());
	}

	/** Returns const reference to the struct, this getter assumes that all data is valid. */
	template<typename T>
	UE_DEPRECATED(5.3, "Use the version of this function which takes const in the template type")
	constexpr typename TEnableIf<!TIsConst<T>::Value, const T&>::Type Get() const
	{
		return UE::StructUtils::GetStructRef<T>(GetScriptStruct(), GetMemory());
	}

	/** Returns const pointer to the struct, or nullptr if cast is not valid. */
	template<typename T>
	constexpr typename TEnableIf<TIsConst<T>::Value, T*>::Type GetPtr() const
	{
		return UE::StructUtils::GetStructPtr<T>(GetScriptStruct(), GetMemory());
	}

	/** Returns const pointer to the struct, or nullptr if cast is not valid. */
	template<typename T>
	UE_DEPRECATED(5.3, "Use the version of this function which takes const in the template type")
	constexpr typename TEnableIf<!TIsConst<T>::Value, const T*>::Type GetPtr() const
	{
		return UE::StructUtils::GetStructPtr<T>(GetScriptStruct(), GetMemory());
	}

	/** Returns True if the struct is valid.*/
	bool IsValid() const
	{
		return GetMemory() != nullptr && GetScriptStruct() != nullptr;
	}

	/** Comparison operators. Note: it does not compare the internal structure itself*/
	template <typename OtherType>
	bool operator==(const OtherType& Other) const
	{
		return ((GetScriptStruct() == Other.GetScriptStruct()) && (GetMemory() == Other.GetMemory()));
	}

	template <typename OtherType>
	bool operator!=(const OtherType& Other) const
	{
		return !operator==(Other);
	}

	/** 
	 * Determines whether Other contains same values as `this` 
	 * @return whether the values stored are equal
	 */
	template <typename OtherType>
	bool CompareStructValues(const OtherType& Other, uint32 PortFlags = 0) const
	{
		UE::StructUtils::CheckWrapperType<OtherType>();
		const UScriptStruct* ScriptStruct = GetScriptStruct();
		return (ScriptStruct == Other.GetScriptStruct())
			&& (!ScriptStruct || ScriptStruct->CompareScriptStruct(GetMemory(), Other.GetMemory(), PortFlags));
	}

protected:

	TSharedPtr<const FStructSharedMemory> StructMemoryPtr;
};

/**
 * TConstSharedStruct is a type-safe FConstSharedStruct wrapper against the given BaseStruct type.
 * @note When used as a property, this automatically defines the BaseStruct property meta-data.
 * 
 * Example:
 *
 *	TConstSharedStruct<FTestStructBase> Test;
 *
 *	TArray<TConstSharedStruct<FTestStructBase>> TestArray;
 */
template<typename BaseStructT>
struct TConstSharedStruct
{
	explicit TConstSharedStruct() = default;

	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	TConstSharedStruct(const TConstSharedStruct<T>& Other)
		: ConstSharedStruct(Other.ConstSharedStruct)
	{}

	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	TConstSharedStruct(const TSharedStruct<T>& ConstSharedStruct)
		: ConstSharedStruct(ConstSharedStruct.SharedStruct)
	{}

	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	TConstSharedStruct(TConstSharedStruct<T>&& Other)
		: ConstSharedStruct(MoveTemp(Other.ConstSharedStruct))
	{}

	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	TConstSharedStruct(TSharedStruct<T>&& ConstSharedStruct)
		: ConstSharedStruct(MoveTemp(ConstSharedStruct.SharedStruct))
	{}

	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	TConstSharedStruct& operator=(const TConstSharedStruct<T>& Other)
	{
		if (this == &Other)
		{
			ConstSharedStruct = Other.ConstSharedStruct;
		}
		return *this;
	}

	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	TConstSharedStruct& operator=(const TSharedStruct<T>& SharedStruct)
	{
		if (this != &SharedStruct)
		{
			ConstSharedStruct = SharedStruct.SharedStruct;
		}
		return *this;
	}

	/** Returns const pointer to struct memory. */
	const uint8* GetMemory() const
	{
		return ConstSharedStruct.GetMemory();
	}

	/** Reset to empty. */
	void Reset()
	{
		ConstSharedStruct.Reset();
	}

	/** Initializes from templated struct type. */
	void Initialize()
	{
		ConstSharedStruct.InitializeAs<BaseStructT>();
	}

	/** Initializes from templated struct instance. This will create a new instance of the shared struct memory. */
	template<typename T = BaseStructT,
		typename = std::enable_if_t<
			std::is_base_of_v<BaseStructT, std::decay_t<T>>
			&& !(std::is_same_v<TStructView<T>, T>
				|| std::is_same_v<TConstStructView<T>, T>
				|| std::is_same_v<TSharedStruct<T>, T>
				|| std::is_same_v<TConstSharedStruct<T>, T>
				|| std::is_same_v<TInstancedStruct<T>, T>), T>>
	void Initialize(const T& Struct)
	{
		ConstSharedStruct.InitializeAs<T>(Struct);
	}

	/** Initializes from struct type and emplace args. This will create a new instance of the shared struct memory. */
	template<typename T = BaseStructT, typename... TArgs, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	void Initialize(TArgs&&... InArgs)
	{
		ConstSharedStruct.InitializeAs<T>(Forward<TArgs>(InArgs)...);
	}

	/** Creates a new TSharedStruct from templated struct type. This will create a new instance of the shared struct memory. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	static TConstSharedStruct Make()
	{
		FConstSharedStruct SharedStruct;
		SharedStruct.InitializeAs<BaseStructT>();
		return SharedStruct;
	}

	/** Creates a new TSharedStruct from templated struct instance. This will create a new instance of the shared struct memory. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	static TConstSharedStruct Make(const T& Struct)
	{
		FConstSharedStruct SharedStruct;
		SharedStruct.InitializeAs<BaseStructT>(Struct);
		return SharedStruct;
	}

	/** Creates a new TSharedStruct from struct type and optional data. This will create a new instance of the shared struct memory. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	static TConstSharedStruct Make(const uint8* InStructMemory = nullptr)
	{
		TConstSharedStruct SharedStruct;
		SharedStruct.template Initialize<T>(InStructMemory);
		return SharedStruct;
	}

	/** Creates a new TSharedStruct from the templated type and forward all arguments to constructor. This will create a new instance of the shared struct memory. */
	template<typename T = BaseStructT, typename... TArgs, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	static TConstSharedStruct Make(TArgs&&... InArgs)
	{
		TConstSharedStruct SharedStruct;
		SharedStruct.template Initialize<T>(Forward<TArgs>(InArgs)...);
		return SharedStruct;
	}

	/** Returns const reference to the struct, this getter assumes that all data is valid. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	constexpr const T& Get() const
	{
		return ConstSharedStruct.Get<T>();
	}

	/** Returns const reference to the struct, this getter assumes that all data is valid. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	constexpr typename TEnableIf<TIsConst<T>::Value, T&>::Type Get() const
	{
		return ConstSharedStruct.Get<T>();
	}

	/** Returns const pointer to the struct. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	constexpr const T* GetPtr() const
	{
		return ConstSharedStruct.GetPtr<T>();
	}

	/** Returns const pointer to the struct, or nullptr if cast is not valid. */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	constexpr typename TEnableIf<TIsConst<T>::Value, T*>::Type GetPtr() const
	{
		return ConstSharedStruct.GetPtr<T>();
	}

	/** Returns True if the struct is valid.*/
	bool IsValid() const
	{
		return ConstSharedStruct.IsValid();
	}

	/** Comparison operators. Note: it does not compare the internal structure itself */
	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	bool operator==(const T& Other) const
	{
		return ConstSharedStruct.operator==(Other);
	}

	template<typename T = BaseStructT, typename = std::enable_if_t<std::is_base_of_v<BaseStructT, std::decay_t<T>>>>
	bool operator!=(const T& Other) const
	{
		return ConstSharedStruct.operator!=(Other);
	}

private:
	/**
	 * Note:
	 *   TConstSharedStruct is a wrapper for a FConstSharedStruct (rather than inheriting) so that it can provide a locked-down type-safe 
	 *   API for use in C++, without being able to accidentally take a reference to the untyped API to workaround the restrictions.
	 * 
	 *   TConstSharedStruct MUST be the same size as FConstSharedStruct, as the reflection layer treats a TConstSharedStruct as a FConstSharedStruct.
	 *   This means that any reflected APIs (like ExportText) that accept an FConstSharedStruct pointer can also accept a TConstSharedStruct pointer.
	 */
	FConstSharedStruct ConstSharedStruct;

	template <typename U> friend struct TConstSharedStruct;
};

template<>
struct TStructOpsTypeTraits<FConstSharedStruct> : public TStructOpsTypeTraitsBase2<FConstSharedStruct>
{
	enum
	{
		WithIdentical = true,
		WithAddStructReferencedObjects = true,
	};
};
