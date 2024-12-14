// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigHierarchyMetadata.h"
#include "RigConnectionRules.h"
#include "RigHierarchyElements.generated.h"

struct FRigVMExecuteContext;
struct FRigBaseElement;
struct FRigControlElement;
class URigHierarchy;
class FRigElementKeyRedirector;

DECLARE_DELEGATE_RetVal_ThreeParams(FTransform, FRigReferenceGetWorldTransformDelegate, const FRigVMExecuteContext*, const FRigElementKey& /* Key */, bool /* bInitial */);
DECLARE_DELEGATE_TwoParams(FRigElementMetadataChangedDelegate, const FRigElementKey& /* Key */, const FName& /* Name */);
DECLARE_DELEGATE_ThreeParams(FRigElementMetadataTagChangedDelegate, const FRigElementKey& /* Key */, const FName& /* Tag */, bool /* AddedOrRemoved */);
DECLARE_DELEGATE_RetVal_ThreeParams(FTransform, FRigReferenceGetWorldTransformDelegate, const FRigVMExecuteContext*, const FRigElementKey& /* Key */, bool /* bInitial */);

#define DECLARE_RIG_ELEMENT_METHODS(ElementType) \
template<typename T> \
friend const T* Cast(const ElementType* InElement) \
{ \
   return Cast<T>((const FRigBaseElement*) InElement); \
} \
template<typename T> \
friend T* Cast(ElementType* InElement) \
{ \
   return Cast<T>((FRigBaseElement*) InElement); \
} \
template<typename T> \
friend const T* CastChecked(const ElementType* InElement) \
{ \
	return CastChecked<T>((const FRigBaseElement*) InElement); \
} \
template<typename T> \
friend T* CastChecked(ElementType* InElement) \
{ \
	return CastChecked<T>((FRigBaseElement*) InElement); \
} \
virtual int32 GetElementTypeIndex() const override { return (int32)ElementType::ElementTypeIndex; }

UENUM()
namespace ERigTransformType
{
	enum Type : int
	{
		InitialLocal,
		CurrentLocal,
		InitialGlobal,
		CurrentGlobal,
		NumTransformTypes
	};
}

namespace ERigTransformType
{
	inline ERigTransformType::Type SwapCurrentAndInitial(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
			{
				return InitialLocal;
			}
			case CurrentGlobal:
			{
				return InitialGlobal;
			}
			case InitialLocal:
			{
				return CurrentLocal;
			}
			default:
			{
				break;
			}
		}
		return CurrentGlobal;
	}

	inline Type SwapLocalAndGlobal(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
			{
				return CurrentGlobal;
			}
			case CurrentGlobal:
			{
				return CurrentLocal;
			}
			case InitialLocal:
			{
				return InitialGlobal;
			}
			default:
			{
				break;
			}
		}
		return InitialLocal;
	}

	inline Type MakeLocal(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
			case CurrentGlobal:
			{
				return CurrentLocal;
			}
			default:
			{
				break;
			}
		}
		return InitialLocal;
	}

	inline Type MakeGlobal(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
			case CurrentGlobal:
			{
				return CurrentGlobal;
			}
			default:
			{
				break;
			}
		}
		return InitialGlobal;
	}

	inline Type MakeInitial(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
			case InitialLocal:
			{
				return InitialLocal;
			}
			default:
			{
				break;
			}
		}
		return InitialGlobal;
	}

	inline Type MakeCurrent(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
        	case InitialLocal:
			{
				return CurrentLocal;
			}
			default:
			{
				break;
			}
		}
		return CurrentGlobal;
	}

	inline bool IsLocal(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
        	case InitialLocal:
			{
				return true;
			}
			default:
			{
				break;
			}
		}
		return false;
	}

	inline bool IsGlobal(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentGlobal:
        	case InitialGlobal:
			{
				return true;;
			}
			default:
			{
				break;
			}
		}
		return false;
	}

	inline bool IsInitial(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case InitialLocal:
        	case InitialGlobal:
			{
				return true;
			}
			default:
			{
				break;
			}
		}
		return false;
	}

	inline bool IsCurrent(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
        	case CurrentGlobal:
			{
				return true;
			}
			default:
			{
				break;
			}
		}
		return false;
	}
}

UENUM()
namespace ERigTransformStorageType
{
	enum Type : int
	{
		Pose,
		Offset,
		Shape,
		NumStorageTypes
	};
}

template<typename T>
struct CONTROLRIG_API FRigReusableElementStorage
{
	TArray<T> Storage;
	TArray<int32> FreeList;

	bool IsValidIndex(const int32& InIndex) const
	{
		return Storage.IsValidIndex(InIndex);
	}

	int32 Num() const
	{
		return Storage.Num();
	}

	T& operator[](int InIndex)
	{
		return Storage[InIndex];
	}

	const T& operator[](int InIndex) const
	{
		return Storage[InIndex];
	}

	typename TArray<T>::RangedForIteratorType begin() { return Storage.begin(); }
	typename TArray<T>::RangedForIteratorType end() { return Storage.end(); }
	typename TArray<T>::RangedForConstIteratorType begin() const { return Storage.begin(); }
	typename TArray<T>::RangedForConstIteratorType end() const { return Storage.end(); }

	const T* GetData() const
	{
		return Storage.GetData();
	}

	TArray<int32, TInlineAllocator<4>> Allocate(int32 InCount, const T& InDefault)
	{
		TArray<int32, TInlineAllocator<4>> Indices;
	
		const int32 NumToAllocate = InCount - FMath::Min(InCount, FreeList.Num());
		if(NumToAllocate > 0)
		{
			Storage.Reserve(Storage.Num() + NumToAllocate);
		}
	
		for(int32 Index = 0; Index < InCount; Index++)
		{
			if(FreeList.IsEmpty())
			{
				Indices.Push(Storage.Add(InDefault));
			}
			else
			{
				Indices.Push(FreeList.Pop(EAllowShrinking::No));
				Storage[Indices.Last()] = InDefault;
			}
		}

		return Indices;
	}

	void Deallocate(int32& InIndex, T** InStorage)
	{
		if(InIndex == INDEX_NONE)
		{
			return;
		}
#if WITH_EDITOR
		check(Storage.IsValidIndex(InIndex));
		check(!FreeList.Contains(InIndex));
#endif
		FreeList.Add(InIndex);
		InIndex = INDEX_NONE;
		if(InStorage)
		{
			*InStorage = nullptr;
		}
	}

	void Deallocate(const TConstArrayView<int32>& InIndices)
	{
		if(InIndices.IsEmpty())
		{
			return;
		}
		FreeList.Reserve(FreeList.Num() + InIndices.Num());
		for(int32 Index : InIndices)
		{
			if(Index != INDEX_NONE && !FreeList.Contains(Index))
			{
				Deallocate(Index, nullptr);
			}
		}
	}

	template<typename OwnerType>
	void Deallocate(OwnerType* InOwner)
	{
		check(InOwner);
		Deallocate(InOwner->StorageIndex, &InOwner->Storage);
	}

	void Reset(TFunction<void(int32, T&)> OnDestroyCallback = nullptr)
	{
		if(OnDestroyCallback)
		{
			for(int32 Index = 0; Index < Storage.Num(); Index++)
			{
				OnDestroyCallback(Index, Storage[Index]);
			}
		}
		Storage.Reset();
		FreeList.Reset();
	}

	bool Contains(int32 InIndex, const T* InStorage)
	{
		if(!IsValidIndex(InIndex))
		{
			return false;
		}
		return GetData() + InIndex == InStorage;
	}

	template<typename OwnerType>
	bool Contains(const OwnerType* InOwner)
	{
		check(InOwner);
		return Contains(InOwner->StorageIndex, InOwner->Storage);
	}

	TMap<int32, int32> Shrink(TFunction<void(int32, T&)> OnDestroyCallback = nullptr)
	{
		TMap<int32, int32> OldToNew;
		
		if(!FreeList.IsEmpty())
		{
			TArray<bool> ToRemove;

			if(FreeList.Num() != Storage.Num() || OnDestroyCallback != nullptr)
			{
				ToRemove.AddZeroed(Storage.Num());
				for(int32 FreeIndex : FreeList)
				{
					ToRemove[FreeIndex] = true;
					if(OnDestroyCallback)
					{
						OnDestroyCallback(FreeIndex, Storage[FreeIndex]);
					}
				}
			}

			if(FreeList.Num() != Storage.Num())
			{
				TArray<T> NewStorage;
				NewStorage.Reserve(FMath::Max(Storage.Num() - FreeList.Num(), 0));
				for(int32 OldIndex = 0; OldIndex < Storage.Num(); OldIndex++)
				{
					if(!ToRemove[OldIndex])
					{
						OldToNew.Add(OldIndex, NewStorage.Add(Storage[OldIndex]));
					}
				}
				Storage = NewStorage;
			}
			else
			{
				Storage.Reset();
			}

			FreeList.Reset();
		}

		FreeList.Shrink();
		Storage.Shrink();

		return OldToNew;
	}
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigTransformDirtyState
{
public:

	GENERATED_BODY()

	FRigTransformDirtyState()
	: StorageIndex(INDEX_NONE)
	, Storage(nullptr)
	{
	}

	const bool& Get() const;
	bool& Get();
	bool Set(bool InDirty);
	FRigTransformDirtyState& operator =(const FRigTransformDirtyState& InOther);

	int32 GetStorageIndex() const
	{
		return StorageIndex;
	}

private:

	void LinkStorage(const TArrayView<bool>& InStorage);
	void UnlinkStorage(FRigReusableElementStorage<bool>& InStorage);

	int32 StorageIndex;
	bool* Storage;

	friend struct FRigLocalAndGlobalDirtyState;
	friend class URigHierarchy;
	friend class URigHierarchyController;
	friend class FRigHierarchyPoseAdapter;
	friend struct FRigReusableElementStorage<bool>;
	friend class FControlRigHierarchyRelinkElementStorage;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigLocalAndGlobalDirtyState
{
	GENERATED_BODY()

public:
	
	FRigLocalAndGlobalDirtyState()
	{
	}

	FRigLocalAndGlobalDirtyState& operator =(const FRigLocalAndGlobalDirtyState& InOther);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "DirtyState")
	FRigTransformDirtyState Global;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "DirtyState")
	FRigTransformDirtyState Local;

private:

	void LinkStorage(const TArrayView<bool>& InStorage);
	void UnlinkStorage(FRigReusableElementStorage<bool>& InStorage);

	friend struct FRigCurrentAndInitialDirtyState;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigCurrentAndInitialDirtyState
{
	GENERATED_BODY()

public:
	
	FRigCurrentAndInitialDirtyState()
	{
	}

	bool& GetDirtyFlag(const ERigTransformType::Type InTransformType)
	{
		switch (InTransformType)
		{
			case ERigTransformType::CurrentLocal:
				return Current.Local.Get();

			case ERigTransformType::CurrentGlobal:
				return Current.Global.Get();

			case ERigTransformType::InitialLocal:
				return Initial.Local.Get();
				
			default:
				return Initial.Global.Get();
		}
	}

	const bool& GetDirtyFlag(const ERigTransformType::Type InTransformType) const
	{
		switch (InTransformType)
		{
			case ERigTransformType::CurrentLocal:
				return Current.Local.Get();

			case ERigTransformType::CurrentGlobal:
				return Current.Global.Get();

			case ERigTransformType::InitialLocal:
				return Initial.Local.Get();

			default:
				return Initial.Global.Get();
		}
	}

	bool IsDirty(const ERigTransformType::Type InTransformType) const
	{
		return GetDirtyFlag(InTransformType);
	}

	void MarkDirty(const ERigTransformType::Type InTransformType)
	{
		ensure(!(GetDirtyFlag(ERigTransformType::SwapLocalAndGlobal(InTransformType))));
		GetDirtyFlag(InTransformType) = true;
	}

	void MarkClean(const ERigTransformType::Type InTransformType)
	{
		GetDirtyFlag(InTransformType) = false;
	}

	FRigCurrentAndInitialDirtyState& operator =(const FRigCurrentAndInitialDirtyState& InOther);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "DirtyState")
	FRigLocalAndGlobalDirtyState Current;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "DirtyState")
	FRigLocalAndGlobalDirtyState Initial;

private:

	void LinkStorage(const TArrayView<bool>& InStorage);
	void UnlinkStorage(FRigReusableElementStorage<bool>& InStorage);

	friend class URigHierarchy;
	friend class URigHierarchyController;
	friend struct FRigTransformElement;
	friend struct FRigControlElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigComputedTransform
{
	GENERATED_BODY()

public:
	
	FRigComputedTransform()
	: StorageIndex(INDEX_NONE)
	, Storage(nullptr)
	{}

	void Save(FArchive& Ar, const FRigTransformDirtyState& InDirtyState);
	void Load(FArchive& Ar, FRigTransformDirtyState& InDirtyState);
	
	const FTransform& Get() const;

	void Set(const FTransform& InTransform)
	{
#if WITH_EDITOR
		ensure(InTransform.GetRotation().IsNormalized());
#endif
		// ensure(!FMath::IsNearlyZero(InTransform.GetScale3D().X));
		// ensure(!FMath::IsNearlyZero(InTransform.GetScale3D().Y));
		// ensure(!FMath::IsNearlyZero(InTransform.GetScale3D().Z));
		if(Storage)
		{
			*Storage = InTransform;
		}
	}

	static bool Equals(const FTransform& A, const FTransform& B, const float InTolerance = 0.0001f)
	{
		return (A.GetTranslation() - B.GetTranslation()).IsNearlyZero(InTolerance) &&
			A.GetRotation().Equals(B.GetRotation(), InTolerance) &&
			(A.GetScale3D() - B.GetScale3D()).IsNearlyZero(InTolerance);
	}

	bool operator == (const FRigComputedTransform& Other) const
	{
		return Equals(Get(), Other.Get());
	}

	FRigComputedTransform& operator =(const FRigComputedTransform& InOther);

	int32 GetStorageIndex() const
	{
		return StorageIndex;
	}

private:

	void LinkStorage(const TArrayView<FTransform>& InStorage);
	void UnlinkStorage(FRigReusableElementStorage<FTransform>& InStorage);
	
	int32 StorageIndex;
	FTransform* Storage;
	
	friend struct FRigLocalAndGlobalTransform;
	friend class URigHierarchy;
	friend class URigHierarchyController;
	friend class FRigHierarchyPoseAdapter;
	friend struct FRigReusableElementStorage<FTransform>;
	friend class FControlRigHierarchyRelinkElementStorage;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigLocalAndGlobalTransform
{
	GENERATED_BODY()

public:
	
	FRigLocalAndGlobalTransform()
    : Local()
    , Global()
	{}

	void Save(FArchive& Ar, const FRigLocalAndGlobalDirtyState& InDirtyState);
	void Load(FArchive& Ar, FRigLocalAndGlobalDirtyState& OutDirtyState);

	FRigLocalAndGlobalTransform& operator =(const FRigLocalAndGlobalTransform& InOther);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FRigComputedTransform Local;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FRigComputedTransform Global;

private:
	
	void LinkStorage(const TArrayView<FTransform>& InStorage);
	void UnlinkStorage(FRigReusableElementStorage<FTransform>& InStorage);

	friend struct FRigCurrentAndInitialTransform;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigCurrentAndInitialTransform
{
	GENERATED_BODY()

public:
	
	FRigCurrentAndInitialTransform()
    : Current()
    , Initial()
	{}

	const FRigComputedTransform& operator[](const ERigTransformType::Type InTransformType) const
	{
		switch(InTransformType)
		{
			case ERigTransformType::CurrentLocal:
			{
				return Current.Local;
			}
			case ERigTransformType::CurrentGlobal:
			{
				return Current.Global;
			}
			case ERigTransformType::InitialLocal:
			{
				return Initial.Local;
			}
			default:
			{
				break;
			}
		}
		return Initial.Global;
	}

	FRigComputedTransform& operator[](const ERigTransformType::Type InTransformType)
	{
		switch(InTransformType)
		{
			case ERigTransformType::CurrentLocal:
			{
				return Current.Local;
			}
			case ERigTransformType::CurrentGlobal:
			{
				return Current.Global;
			}
			case ERigTransformType::InitialLocal:
			{
				return Initial.Local;
			}
			default:
			{
				break;
			}
		}
		return Initial.Global;
	}

	const FTransform& Get(const ERigTransformType::Type InTransformType) const
	{
		return operator[](InTransformType).Get();
	}

	void Set(const ERigTransformType::Type InTransformType, const FTransform& InTransform)
	{
		operator[](InTransformType).Set(InTransform);
	}

	void Save(FArchive& Ar, const FRigCurrentAndInitialDirtyState& InDirtyState);
	void Load(FArchive& Ar, FRigCurrentAndInitialDirtyState& OutDirtyState);

	bool operator == (const FRigCurrentAndInitialTransform& Other) const
	{
		return Current.Local  == Other.Current.Local
			&& Current.Global == Other.Current.Global
			&& Initial.Local  == Other.Initial.Local
			&& Initial.Global == Other.Initial.Global;
	}

	FRigCurrentAndInitialTransform& operator =(const FRigCurrentAndInitialTransform& InOther);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FRigLocalAndGlobalTransform Current;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FRigLocalAndGlobalTransform Initial;
	
private:
	
	void LinkStorage(const TArrayView<FTransform>& InStorage);
	void UnlinkStorage(FRigReusableElementStorage<FTransform>& InStorage);

	friend class URigHierarchy;
	friend class URigHierarchyController;
	friend struct FRigTransformElement;
	friend struct FRigControlElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigPreferredEulerAngles
{
	GENERATED_BODY()

	static constexpr EEulerRotationOrder DefaultRotationOrder = EEulerRotationOrder::YZX;

	FRigPreferredEulerAngles()
	: RotationOrder(DefaultRotationOrder) // default for rotator
	, Current(FVector::ZeroVector)
	, Initial(FVector::ZeroVector)
	{}

	void Save(FArchive& Ar);
	void Load(FArchive& Ar);

	bool operator == (const FRigPreferredEulerAngles& Other) const
	{
		return RotationOrder == Other.RotationOrder &&
			Current == Other.Current &&
			Initial == Other.Initial;
	}

	void Reset();
	FVector& Get(bool bInitial = false) { return bInitial ? Initial : Current; }
	const FVector& Get(bool bInitial = false) const { return bInitial ? Initial : Current; }
	FRotator GetRotator(bool bInitial = false) const;
	FRotator SetRotator(const FRotator& InValue, bool bInitial = false, bool bFixEulerFlips = false);
	FVector GetAngles(bool bInitial = false, EEulerRotationOrder InRotationOrder = DefaultRotationOrder) const;
	void SetAngles(const FVector& InValue, bool bInitial = false, EEulerRotationOrder InRotationOrder = DefaultRotationOrder, bool bFixEulerFlips = false);
	void SetRotationOrder(EEulerRotationOrder InRotationOrder);


	FRotator GetRotatorFromQuat(const FQuat& InQuat) const;
	FQuat GetQuatFromRotator(const FRotator& InRotator) const;
	
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	EEulerRotationOrder RotationOrder;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FVector Current;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FVector Initial;
};


struct FRigBaseElement;
//typedef TArray<FRigBaseElement*> FRigBaseElementChildrenArray;
typedef TArray<FRigBaseElement*, TInlineAllocator<3>> FRigBaseElementChildrenArray;
//typedef TArray<FRigBaseElement*> FRigBaseElementParentArray;
typedef TArray<FRigBaseElement*, TInlineAllocator<1>> FRigBaseElementParentArray;

struct CONTROLRIG_API FRigElementHandle
{
public:

	FRigElementHandle()
		: Hierarchy(nullptr)
		, Key()
	{}

	FRigElementHandle(URigHierarchy* InHierarchy, const FRigElementKey& InKey);
	FRigElementHandle(URigHierarchy* InHierarchy, const FRigBaseElement* InElement);

	bool IsValid() const { return Get() != nullptr; }
	operator bool() const { return IsValid(); }
	
	const URigHierarchy* GetHierarchy() const { return Hierarchy.Get(); }
	URigHierarchy* GetHierarchy() { return Hierarchy.Get(); }
	const FRigElementKey& GetKey() const { return Key; }

	const FRigBaseElement* Get() const;
	FRigBaseElement* Get();

	template<typename T>
	T* Get()
	{
		return Cast<T>(Get());
	}

	template<typename T>
	const T* Get() const
	{
		return Cast<T>(Get());
	}

	template<typename T>
	T* GetChecked()
	{
		return CastChecked<T>(Get());
	}

	template<typename T>
	const T* GetChecked() const
	{
		return CastChecked<T>(Get());
	}

private:

	TWeakObjectPtr<URigHierarchy> Hierarchy;
	FRigElementKey Key;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigBaseElement
{
	GENERATED_BODY()

public:

	enum EElementIndex
	{
		BaseElement,
		TransformElement,
		SingleParentElement,
		MultiParentElement,
		BoneElement,
		NullElement,
		ControlElement,
		CurveElement,
		PhysicsElement,
		ReferenceElement,
		ConnectorElement,
		SocketElement,

		Max
	};

	static const EElementIndex ElementTypeIndex;

	FRigBaseElement() = default;
	virtual ~FRigBaseElement();

	FRigBaseElement(const FRigBaseElement& InOther)
	{
		*this = InOther;
	}
	
	FRigBaseElement& operator=(const FRigBaseElement& InOther)
	{
		// We purposefully do not copy any non-UPROPERTY entries, including Owner. This is so that when the copied
		// element is deleted, the metadata is not deleted with it. These copies are purely intended for interfacing
		// with BP and details view wrappers. 
		// These copies are solely intended for UControlRig::OnControlSelected_BP
		Key = InOther.Key;
		Index = InOther.Index;
		SubIndex = InOther.SubIndex;
		CreatedAtInstructionIndex = InOther.CreatedAtInstructionIndex;
		bSelected = InOther.bSelected;
		return *this;
	}
	
	virtual int32 GetElementTypeIndex() const { return ElementTypeIndex; }
	static int32 GetElementTypeCount() { return EElementIndex::Max; }

	enum ESerializationPhase
	{
		StaticData,
		InterElementData
	};

protected:
	// Only derived types should be able to construct this one.
	explicit FRigBaseElement(URigHierarchy* InOwner, ERigElementType InElementType)
		: Owner(InOwner)
		, Key(InElementType)
	{
	}

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return true;
	}

	// ReSharper disable once CppUE4ProbableMemoryIssuesWithUObject
	URigHierarchy* Owner = nullptr;
	
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = RigElement, meta = (AllowPrivateAccess = "true"))
	FRigElementKey Key;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = RigElement, meta = (AllowPrivateAccess = "true"))
	int32 Index = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = RigElement, meta = (AllowPrivateAccess = "true"))
	int32 SubIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Transient, Category = RigElement, meta = (AllowPrivateAccess = "true"))
	int32 CreatedAtInstructionIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Transient, Category = RigElement, meta = (AllowPrivateAccess = "true"))
	bool bSelected = false;

	// used for constructing / destructing the memory. typically == 1
	// Set by URigHierarchy::NewElement.
	int32 OwnedInstances = 0;

	// Index into the child cache offset and count table in URigHierarchy. Set by URigHierarchy::UpdateCachedChildren
	int32 ChildCacheIndex = INDEX_NONE;

	// Index into the metadata storage for this element.
	int32 MetadataStorageIndex = INDEX_NONE;
	
	mutable FString CachedNameString;

public:

	UScriptStruct* GetElementStruct() const;
	void Serialize(FArchive& Ar, ESerializationPhase SerializationPhase);
	virtual void Save(FArchive& Ar, ESerializationPhase SerializationPhase);
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase);

	const FName& GetFName() const { return Key.Name; }
	const FString& GetName() const
	{
		if(CachedNameString.IsEmpty() && !Key.Name.IsNone())
		{
			CachedNameString = Key.Name.ToString();
		}
		return CachedNameString;
	}
	virtual const FName& GetDisplayName() const { return GetFName(); }
	ERigElementType GetType() const { return Key.Type; }
	const FRigElementKey& GetKey() const { return Key; }
	FRigElementKeyAndIndex GetKeyAndIndex() const { return {Key, Index}; };
	int32 GetIndex() const { return Index; }
	int32 GetSubIndex() const { return SubIndex; }
	bool IsSelected() const { return bSelected; }
	int32 GetCreatedAtInstructionIndex() const { return CreatedAtInstructionIndex; }
	bool IsProcedural() const { return CreatedAtInstructionIndex != INDEX_NONE; }
	const URigHierarchy* GetOwner() const { return Owner; }
	URigHierarchy* GetOwner() { return Owner; }

	// Metadata
	FRigBaseMetadata* GetMetadata(const FName& InName, ERigMetadataType InType = ERigMetadataType::Invalid);
	const FRigBaseMetadata* GetMetadata(const FName& InName, ERigMetadataType InType) const;
	bool SetMetadata(const FName& InName, ERigMetadataType InType, const void* InData, int32 InSize);
	FRigBaseMetadata* SetupValidMetadata(const FName& InName, ERigMetadataType InType);
	bool RemoveMetadata(const FName& InName);
	bool RemoveAllMetadata();
	
	void NotifyMetadataTagChanged(const FName& InTag, bool bAdded);

	virtual int32 GetNumTransforms() const { return 0; }
	virtual int32 GetNumCurves() const { return 0; }
	
	template<typename T>
	bool IsA() const { return T::IsClassOf(this); }

	bool IsTypeOf(ERigElementType InElementType) const
	{
		return Key.IsTypeOf(InElementType);
	}

	template<typename T>
    friend const T* Cast(const FRigBaseElement* InElement)
	{
		if(InElement)
		{
			if(InElement->IsA<T>())
			{
				return static_cast<const T*>(InElement);
			}
		}
		return nullptr;
	}

	template<typename T>
    friend T* Cast(FRigBaseElement* InElement)
	{
		if(InElement)
		{
			if(InElement->IsA<T>())
			{
				return static_cast<T*>(InElement);
			}
		}
		return nullptr;
	}

	template<typename T>
    friend const T* CastChecked(const FRigBaseElement* InElement)
	{
		const T* Element = Cast<T>(InElement);
		check(Element);
		return Element;
	}

	template<typename T>
    friend T* CastChecked(FRigBaseElement* InElement)
	{
		T* Element = Cast<T>(InElement);
		check(Element);
		return Element;
	}

	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial, bool bWeights) {}

protected:
	// Used to initialize this base element during URigHierarchy::CopyHierarchy. Once all elements are
	// initialized, the sub-class data copying is done using CopyFrom.
	void InitializeFrom(const FRigBaseElement* InOther);

	// helper function to be called as part of URigHierarchy::CopyHierarchy
	virtual void CopyFrom(const FRigBaseElement* InOther);

	virtual void LinkStorage(const TArrayView<FTransform>& InTransforms, const TArrayView<bool>& InDirtyStates, const TArrayView<float>& InCurves) {}
	virtual void UnlinkStorage(FRigReusableElementStorage<FTransform>& InTransforms, FRigReusableElementStorage<bool>& InDirtyStates, FRigReusableElementStorage<float>& InCurves) {}

	friend class FControlRigEditor;
	friend class URigHierarchy;
	friend class URigHierarchyController;
	friend struct FRigElementKeyAndIndex;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigTransformElement : public FRigBaseElement
{
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigTransformElement)

	static const EElementIndex ElementTypeIndex;

	FRigTransformElement() = default;
	FRigTransformElement(const FRigTransformElement& InOther)
	{
		*this = InOther;
	}
	FRigTransformElement& operator=(const FRigTransformElement& InOther)
	{
		Super::operator=(InOther);
		GetTransform() = InOther.GetTransform();
		GetDirtyState() = InOther.GetDirtyState();
		return *this;
	}
	virtual ~FRigTransformElement() override {}

	virtual void Save(FArchive& A, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase) override;

	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial, bool bWeights) override;

	// Current and Initial, both in Local and Global
	virtual int32 GetNumTransforms() const override { return 4; }

	const FRigCurrentAndInitialTransform& GetTransform() const;
	FRigCurrentAndInitialTransform& GetTransform();
	const FRigCurrentAndInitialDirtyState& GetDirtyState() const;
	FRigCurrentAndInitialDirtyState& GetDirtyState();

protected:
	
	FRigTransformElement(URigHierarchy* InOwner, const ERigElementType InType) :
		FRigBaseElement(InOwner, InType)
	{}

	// Pose storage for this element.
	FRigCurrentAndInitialTransform PoseStorage;

	// Dirty state storage for this element.
	FRigCurrentAndInitialDirtyState PoseDirtyState;
	
	struct FElementToDirty
	{
		FElementToDirty()
			: Element(nullptr)
			, HierarchyDistance(INDEX_NONE)
		{}

		FElementToDirty(FRigTransformElement* InElement, int32 InHierarchyDistance = INDEX_NONE)
			: Element(InElement)
			, HierarchyDistance(InHierarchyDistance)
		{}

		bool operator ==(const FElementToDirty& Other) const
		{
			return Element == Other.Element;
		}

		bool operator !=(const FElementToDirty& Other) const
		{
			return Element != Other.Element;
		}
		
		FRigTransformElement* Element;
		int32 HierarchyDistance;
	};

	//typedef TArray<FElementToDirty> FElementsToDirtyArray;
	typedef TArray<FElementToDirty, TInlineAllocator<3>> FElementsToDirtyArray;  
	FElementsToDirtyArray ElementsToDirty;

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Bone ||
			InElement->GetType() == ERigElementType::Null ||
			InElement->GetType() == ERigElementType::Control ||
			InElement->GetType() == ERigElementType::Physics ||
			InElement->GetType() == ERigElementType::Reference ||
			InElement->GetType() == ERigElementType::Socket;
	}

	virtual void CopyFrom(const FRigBaseElement* InOther) override;

	virtual void LinkStorage(const TArrayView<FTransform>& InTransforms, const TArrayView<bool>& InDirtyStates, const TArrayView<float>& InCurves) override;
	virtual void UnlinkStorage(FRigReusableElementStorage<FTransform>& InTransforms, FRigReusableElementStorage<bool>& InDirtyStates, FRigReusableElementStorage<float>& InCurves) override;

	friend class URigHierarchy;
	friend class URigHierarchyController;
	friend struct FRigBaseElement;
	friend class FControlRigEditor;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigSingleParentElement : public FRigTransformElement
{
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigSingleParentElement)

	static const EElementIndex ElementTypeIndex;

	FRigSingleParentElement() = default;
	virtual ~FRigSingleParentElement() override {}

	virtual void Save(FArchive& A, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase) override;

	FRigTransformElement* ParentElement = nullptr;

protected:
	explicit FRigSingleParentElement(URigHierarchy* InOwner, ERigElementType InType)
		: FRigTransformElement(InOwner, InType)
	{}

	virtual void CopyFrom(const FRigBaseElement* InOther) override;

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Bone ||
			InElement->GetType() == ERigElementType::Physics ||
			InElement->GetType() == ERigElementType::Reference ||
			InElement->GetType() == ERigElementType::Socket;
	}
	
	friend class URigHierarchy;
	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigElementWeight
{
public:
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Weight)
	float Location;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Weight)
	float Rotation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Weight)
	float Scale;

	FRigElementWeight()
		: Location(1.f)
		, Rotation(1.f)
		, Scale(1.f)
	{}

	FRigElementWeight(float InWeight)
		: Location(InWeight)
		, Rotation(InWeight)
		, Scale(InWeight)
	{}

	FRigElementWeight(float InLocation, float InRotation, float InScale)
		: Location(InLocation)
		, Rotation(InRotation)
		, Scale(InScale)
	{}

	friend FArchive& operator <<(FArchive& Ar, FRigElementWeight& Weight)
	{
		Ar << Weight.Location;
		Ar << Weight.Rotation;
		Ar << Weight.Scale;
		return Ar;
	}

	bool AffectsLocation() const
	{
		return Location > SMALL_NUMBER;
	}

	bool AffectsRotation() const
	{
		return Rotation > SMALL_NUMBER;
	}

	bool AffectsScale() const
	{
		return Scale > SMALL_NUMBER;
	}

	bool IsAlmostZero() const
	{
		return !AffectsLocation() && !AffectsRotation() && !AffectsScale();
	}

	friend FRigElementWeight operator *(FRigElementWeight InWeight, float InScale)
	{
		return FRigElementWeight(InWeight.Location * InScale, InWeight.Rotation * InScale, InWeight.Scale * InScale);
	}

	friend FRigElementWeight operator *(float InScale, FRigElementWeight InWeight)
	{
		return FRigElementWeight(InWeight.Location * InScale, InWeight.Rotation * InScale, InWeight.Scale * InScale);
	}
};

USTRUCT()
struct CONTROLRIG_API FRigElementParentConstraint
{
	GENERATED_BODY()

	FRigTransformElement* ParentElement;
	FRigElementWeight Weight;
	FRigElementWeight InitialWeight;
	mutable FTransform Cache;
	mutable bool bCacheIsDirty;
		
	FRigElementParentConstraint()
		: ParentElement(nullptr)
	{
		Cache = FTransform::Identity;
		bCacheIsDirty = true;
	}

	const FRigElementWeight& GetWeight(bool bInitial = false) const
	{
		return bInitial ? InitialWeight : Weight;
	}

	void CopyPose(const FRigElementParentConstraint& InOther, bool bCurrent, bool bInitial)
	{
		if(bCurrent)
		{
			Weight = InOther.Weight;
		}
		if(bInitial)
		{
			InitialWeight = InOther.InitialWeight;
		}
		bCacheIsDirty = true;
	}
};

#if URIGHIERARCHY_ENSURE_CACHE_VALIDITY
typedef TArray<FRigElementParentConstraint, TInlineAllocator<8>> FRigElementParentConstraintArray;
#else
typedef TArray<FRigElementParentConstraint, TInlineAllocator<1>> FRigElementParentConstraintArray;
#endif

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigMultiParentElement : public FRigTransformElement
{
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigMultiParentElement)

	static const EElementIndex ElementTypeIndex;

	FRigMultiParentElement() = default;	
	virtual ~FRigMultiParentElement() override {}

	virtual void Save(FArchive& A, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase) override;

	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial, bool bWeights) override;
	
	FRigElementParentConstraintArray ParentConstraints;
	TMap<FRigElementKey, int32> IndexLookup;

protected:
	explicit FRigMultiParentElement(URigHierarchy* InOwner, const ERigElementType InType)
		: FRigTransformElement(InOwner, InType)
	{}

	virtual void CopyFrom(const FRigBaseElement* InOther) override;
	
private:
	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Null ||
			InElement->GetType() == ERigElementType::Control;
	}

	friend class URigHierarchy;
	friend struct FRigBaseElement;
};


USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigBoneElement : public FRigSingleParentElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigBoneElement)

	static const EElementIndex ElementTypeIndex;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = RigElement)
	ERigBoneType BoneType = ERigBoneType::User;

	FRigBoneElement()
		: FRigBoneElement(nullptr)
	{}
	FRigBoneElement(const FRigBoneElement& InOther)
	{
		*this = InOther;
	}
	FRigBoneElement& operator=(const FRigBoneElement& InOther)
	{
		BoneType = InOther.BoneType;
		return *this;
	}
	
	virtual ~FRigBoneElement() override {}

	virtual void Save(FArchive& A, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase) override;

private:
	explicit FRigBoneElement(URigHierarchy* InOwner)
		: FRigSingleParentElement(InOwner, ERigElementType::Bone)
	{}

	virtual void CopyFrom(const FRigBaseElement* InOther) override;

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Bone;
	}

	friend class URigHierarchy;
	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigNullElement final : public FRigMultiParentElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigNullElement)

	static const EElementIndex ElementTypeIndex;

	FRigNullElement() 
		: FRigNullElement(nullptr)
	{}

	virtual ~FRigNullElement() override {}

private:
	explicit FRigNullElement(URigHierarchy* InOwner)
		: FRigMultiParentElement(InOwner, ERigElementType::Null)
	{}

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Null;
	}

	friend class URigHierarchy;
	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct FRigControlElementCustomization
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Customization)
	TArray<FRigElementKey> AvailableSpaces;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Customization)
	TArray<FRigElementKey> RemovedSpaces;
};

UENUM(BlueprintType)
enum class ERigControlTransformChannel : uint8
{
	TranslationX,
	TranslationY,
	TranslationZ,
	Pitch,
	Yaw,
	Roll,
	ScaleX,
	ScaleY,
	ScaleZ
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControlSettings
{
	GENERATED_BODY()

	FRigControlSettings();

	void Save(FArchive& Ar);
	void Load(FArchive& Ar);

	friend uint32 GetTypeHash(const FRigControlSettings& Settings);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	ERigControlAnimationType AnimationType;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control, meta=(DisplayName="Value Type"))
	ERigControlType ControlType;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	FName DisplayName;

	/** the primary axis to use for float controls */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	ERigControlAxis PrimaryAxis;

	/** If Created from a Curve  Container*/
	UPROPERTY(transient)
	bool bIsCurve;

	/** True if the control has limits. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Limits)
	TArray<FRigControlLimitEnabled> LimitEnabled;

	/**
	 * True if the limits should be drawn in debug.
	 * For this to be enabled you need to have at least one min and max limit turned on.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Limits)
	bool bDrawLimits;

	/** The minimum limit of the control's value */
	UPROPERTY(BlueprintReadWrite, Category = Limits)
	FRigControlValue MinimumValue;

	/** The maximum limit of the control's value */
	UPROPERTY(BlueprintReadWrite, Category = Limits)
	FRigControlValue MaximumValue;

	/** Set to true if the shape is currently visible in 3d */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Shape)
	bool bShapeVisible;

	/** Defines how the shape visibility should be changed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Shape)
	ERigControlVisibility ShapeVisibility;

	/* This is optional UI setting - this doesn't mean this is always used, but it is optional for manipulation layer to use this*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Shape)
	FName ShapeName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Shape)
	FLinearColor ShapeColor;

	/** If the control is transient and only visible in the control rig editor */
	UPROPERTY(BlueprintReadWrite, Category = Control)
	bool bIsTransientControl;

	/** If the control is integer it can use this enum to choose values */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Control)
	TObjectPtr<UEnum> ControlEnum;

	/**
	 * The User interface customization used for a control
	 * This will be used as the default content for the space picker and other widgets
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Animation, meta = (DisplayName = "Customization"))
	FRigControlElementCustomization Customization;

	/**
	 * The list of driven controls for this proxy control.
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Animation)
	TArray<FRigElementKey> DrivenControls;

	/**
	 * The list of previously driven controls - prior to a procedural change
	 */
	TArray<FRigElementKey> PreviouslyDrivenControls;

	/**
	 * If set to true the animation channel will be grouped with the parent control in sequencer
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Animation)
	bool bGroupWithParentControl;

	/**
	 * Allow to space switch only to the available spaces
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Animation)
	bool bRestrictSpaceSwitching;

	/**
	 * Filtered Visible Transform channels. If this is empty everything is visible
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Animation)
	TArray<ERigControlTransformChannel> FilteredChannels;

	/**
	 * The euler rotation order this control prefers for animation, if we aren't using default UE rotator
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Animation)
	EEulerRotationOrder PreferredRotationOrder;

	/**
	* Whether to use a specified rotation order or just use the default FRotator order and conversion functions
	*/
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Animation)
	bool bUsePreferredRotationOrder;

	/**
	* The euler rotation order this control prefers for animation if it is active. If not set then we use the default UE rotator.
	*/
	TOptional<EEulerRotationOrder> GetRotationOrder() const
	{
		TOptional<EEulerRotationOrder> RotationOrder;
		if (bUsePreferredRotationOrder)
		{
			RotationOrder = PreferredRotationOrder;
		}
		return RotationOrder;
	}

	/**
	*  Set the rotation order if the rotation is set otherwise use default rotator
	*/
	void SetRotationOrder(const TOptional<EEulerRotationOrder>& EulerRotation)
	{
		if (EulerRotation.IsSet())
		{
			bUsePreferredRotationOrder = true;
			PreferredRotationOrder = EulerRotation.GetValue();
		}
		else
		{
			bUsePreferredRotationOrder = false;
		}
	}
#if WITH_EDITORONLY_DATA
	/**
	 * Deprecated properties.
	 */
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage = "Use animation_type instead."))
	bool bAnimatable_DEPRECATED = true;
	
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage = "Use animation_type or shape_visible instead."))
	bool bShapeEnabled_DEPRECATED = true;
#endif
	
	/** Applies the limits expressed by these settings to a value */
	void ApplyLimits(FRigControlValue& InOutValue) const
	{
		InOutValue.ApplyLimits(LimitEnabled, ControlType, MinimumValue, MaximumValue);
	}

	/** Applies the limits expressed by these settings to a transform */
	void ApplyLimits(FTransform& InOutValue) const
	{
		FRigControlValue Value;
		Value.SetFromTransform(InOutValue, ControlType, PrimaryAxis);
		ApplyLimits(Value);
		InOutValue = Value.GetAsTransform(ControlType, PrimaryAxis);
	}

	FRigControlValue GetIdentityValue() const
	{
		FRigControlValue Value;
		Value.SetFromTransform(FTransform::Identity, ControlType, PrimaryAxis);
		return Value;
	}

	bool operator == (const FRigControlSettings& InOther) const;

	bool operator != (const FRigControlSettings& InOther) const
	{
		return !(*this == InOther);
	}

	void SetupLimitArrayForType(bool bLimitTranslation = false, bool bLimitRotation = false, bool bLimitScale = false);

	bool IsAnimatable() const
	{
		return (AnimationType == ERigControlAnimationType::AnimationControl) ||
			(AnimationType == ERigControlAnimationType::AnimationChannel);
	}

	bool ShouldBeGrouped() const
	{
		return IsAnimatable() && bGroupWithParentControl;
	}

	bool SupportsShape() const
	{
		return (AnimationType != ERigControlAnimationType::AnimationChannel) &&
			(ControlType != ERigControlType::Bool);
	}

	bool IsVisible() const
	{
		return SupportsShape() && bShapeVisible;
	}
	
	bool SetVisible(bool bVisible, bool bForce = false)
	{
		if(!bForce)
		{
			if(AnimationType == ERigControlAnimationType::ProxyControl)
			{
				if(ShapeVisibility == ERigControlVisibility::BasedOnSelection)
				{
					return false;
				}
			}
		}
		
		if(SupportsShape())
		{
			if(bShapeVisible == bVisible)
			{
				return false;
			}
			bShapeVisible = bVisible;
		}
		return SupportsShape();
	}

	bool IsSelectable(bool bRespectVisibility = true) const
	{
		return (AnimationType == ERigControlAnimationType::AnimationControl ||
			AnimationType == ERigControlAnimationType::ProxyControl) &&
			(IsVisible() || !bRespectVisibility);
	}

	void SetAnimationTypeFromDeprecatedData(bool bAnimatable, bool bShapeEnabled)
	{
		if(bAnimatable)
		{
			if(bShapeEnabled && (ControlType != ERigControlType::Bool))
			{
				AnimationType = ERigControlAnimationType::AnimationControl;
			}
			else
			{
				AnimationType = ERigControlAnimationType::AnimationChannel;
			}
		}
		else
		{
			AnimationType = ERigControlAnimationType::ProxyControl;
		}
	}
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControlElement final : public FRigMultiParentElement
{
	public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigControlElement)

	static const EElementIndex ElementTypeIndex;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Control)
	FRigControlSettings Settings;

	// Current and Initial, both in Local and Global, for Pose, Offset and Shape
	virtual int32 GetNumTransforms() const override { return 12; }
	
	const FRigCurrentAndInitialTransform& GetOffsetTransform() const;
	FRigCurrentAndInitialTransform& GetOffsetTransform();
	const FRigCurrentAndInitialDirtyState& GetOffsetDirtyState() const;
	FRigCurrentAndInitialDirtyState& GetOffsetDirtyState();
	const FRigCurrentAndInitialTransform& GetShapeTransform() const;
	FRigCurrentAndInitialTransform& GetShapeTransform();
	const FRigCurrentAndInitialDirtyState& GetShapeDirtyState() const;
	FRigCurrentAndInitialDirtyState& GetShapeDirtyState();

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = RigElement)
	FRigPreferredEulerAngles PreferredEulerAngles;

	FRigControlElement()
		: FRigControlElement(nullptr)
	{ }
	
	FRigControlElement(const FRigControlElement& InOther)
	{
		*this = InOther;
	}
	
	FRigControlElement& operator=(const FRigControlElement& InOther)
	{
		Super::operator=(InOther);
		Settings = InOther.Settings;
		GetOffsetTransform() = InOther.GetOffsetTransform();
		GetShapeTransform() = InOther.GetShapeTransform();
		PreferredEulerAngles = InOther.PreferredEulerAngles;
		return *this;
	}

	virtual ~FRigControlElement() override {}
	
	virtual const FName& GetDisplayName() const override
	{
		if(!Settings.DisplayName.IsNone())
		{
			return Settings.DisplayName;
		}
		return FRigMultiParentElement::GetDisplayName();
	}

	bool IsAnimationChannel() const { return Settings.AnimationType == ERigControlAnimationType::AnimationChannel; }

	bool CanDriveControls() const { return Settings.AnimationType == ERigControlAnimationType::ProxyControl || Settings.AnimationType == ERigControlAnimationType::AnimationControl; }

	bool CanTreatAsAdditive() const
	{
		if (Settings.ControlType == ERigControlType::Bool)
		{
			return false;
		}
		if (Settings.ControlType == ERigControlType::Integer && Settings.ControlEnum != nullptr)
		{
			return false;
		}
		return true;
	}

	virtual void Save(FArchive& A, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase) override;

	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial, bool bWeights) override;

protected:

	virtual void LinkStorage(const TArrayView<FTransform>& InTransforms, const TArrayView<bool>& InDirtyStates, const TArrayView<float>& InCurves) override;
	virtual void UnlinkStorage(FRigReusableElementStorage<FTransform>& InTransforms, FRigReusableElementStorage<bool>& InDirtyStates, FRigReusableElementStorage<float>& InCurves) override;
	
private:
	explicit FRigControlElement(URigHierarchy* InOwner)
		: FRigMultiParentElement(InOwner, ERigElementType::Control)
	{ }

	virtual void CopyFrom(const FRigBaseElement* InOther) override;

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Control;
	}

	// Offset storage for this element.
	FRigCurrentAndInitialTransform OffsetStorage;
	FRigCurrentAndInitialDirtyState OffsetDirtyState;

	// Shape storage for this element.
	FRigCurrentAndInitialTransform ShapeStorage;
	FRigCurrentAndInitialDirtyState ShapeDirtyState;

	friend class URigHierarchy;
	friend class URigHierarchyController;
	friend struct FRigBaseElement;
	friend class FControlRigEditor;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigCurveElement final : public FRigBaseElement
{
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigCurveElement)

	static const EElementIndex ElementTypeIndex;

	FRigCurveElement()
		: FRigCurveElement(nullptr)
	{}
	
	virtual ~FRigCurveElement() override {}

	virtual int32 GetNumCurves() const override { return 1; }

	virtual void Save(FArchive& A, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase) override;

	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial, bool bWeights) override;

	const float& Get() const;
	void Set(const float& InValue, bool InValueIsSet = true);

	bool IsValueSet() const { return bIsValueSet; }

	int32 GetStorageIndex() const
	{
		return StorageIndex;
	}

protected:

	virtual void LinkStorage(const TArrayView<FTransform>& InTransforms, const TArrayView<bool>& InDirtyStates, const TArrayView<float>& InCurves) override;
	virtual void UnlinkStorage(FRigReusableElementStorage<FTransform>& InTransforms, FRigReusableElementStorage<bool>& InDirtyStates, FRigReusableElementStorage<float>& InCurves) override;
	
private:
	FRigCurveElement(URigHierarchy* InOwner)
		: FRigBaseElement(InOwner, ERigElementType::Curve)
	{}
	
	virtual void CopyFrom(const FRigBaseElement* InOther) override;

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Curve;
	}

	// Set to true if the value was actually set. Used to carry back and forth blend curve
	// value validity state.
	bool bIsValueSet = true;

	int32 StorageIndex = INDEX_NONE;
	float* Storage = nullptr;

	friend class URigHierarchy;
	friend class URigHierarchyController;
	friend struct FRigBaseElement;
	friend class FRigHierarchyPoseAdapter;
	friend struct FRigReusableElementStorage<float>;
	friend class FControlRigHierarchyRelinkElementStorage;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigPhysicsSolverID
{
public:
	
	GENERATED_BODY()
	
	FRigPhysicsSolverID()
		: Guid()
	{
	}

	explicit FRigPhysicsSolverID(const FGuid& InGuid)
		: Guid(InGuid)
	{
	}

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Physics, meta=(ShowOnlyInnerProperties))
	FGuid Guid;

	bool IsValid() const
	{
		return Guid.IsValid();
	}

	FString ToString() const
	{
		return Guid.ToString();
	}

	bool operator == (const FRigPhysicsSolverID& InOther) const
	{
		return Guid == InOther.Guid;
	}

	bool operator != (const FRigPhysicsSolverID& InOther) const
	{
		return !(*this == InOther);
	}

	friend FArchive& operator <<(FArchive& Ar, FRigPhysicsSolverID& ID)
	{
		Ar << ID.Guid;
		return Ar;
	}

	friend uint32 GetTypeHash(const FRigPhysicsSolverID& InID)
	{
		return GetTypeHash(InID.Guid);
	}
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigPhysicsSolverDescription
{
public:
	
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Physics, meta=(ShowOnlyInnerProperties))
	FRigPhysicsSolverID ID;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Physics, meta=(ShowOnlyInnerProperties))
	FName Name;

	FRigPhysicsSolverDescription()
	: ID()
	, Name(NAME_None)
	{}
	
	FRigPhysicsSolverDescription(const FRigPhysicsSolverDescription& InOther)
	{
		*this = InOther;
	}
	
	FRigPhysicsSolverDescription& operator=(const FRigPhysicsSolverDescription& InOther)
	{
		CopyFrom(&InOther);
		return *this;
	}

	void Serialize(FArchive& Ar);
	void Save(FArchive& Ar);
	void Load(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FRigPhysicsSolverDescription& P)
	{
		P.Serialize(Ar);
		return Ar;
	}

	static FGuid MakeGuid(const FString& InObjectPath, const FName& InSolverName);
	static FRigPhysicsSolverID MakeID(const FString& InObjectPath, const FName& InSolverName);

private:

	void CopyFrom(const FRigPhysicsSolverDescription* InOther);
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigPhysicsSettings
{
	GENERATED_BODY()

	FRigPhysicsSettings();

	void Save(FArchive& Ar);
	void Load(FArchive& Ar);


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	float Mass;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigPhysicsElement : public FRigSingleParentElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigPhysicsElement)

	static const EElementIndex ElementTypeIndex;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Physics, meta=(ShowOnlyInnerProperties))
	FRigPhysicsSolverID Solver;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Physics, meta=(ShowOnlyInnerProperties))
	FRigPhysicsSettings Settings;
	
	FRigPhysicsElement()
        : FRigPhysicsElement(nullptr)
	{ }
	
	FRigPhysicsElement(const FRigPhysicsElement& InOther)
	{
		*this = InOther;
	}
	
	FRigPhysicsElement& operator=(const FRigPhysicsElement& InOther)
	{
		Super::operator=(InOther);
		Solver = InOther.Solver;
		Settings = InOther.Settings;
		return *this;
	}
	
	virtual ~FRigPhysicsElement() override {}

	virtual void Save(FArchive& A, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase) override;

private:
	explicit FRigPhysicsElement(URigHierarchy* InOwner)
		: FRigSingleParentElement(InOwner, ERigElementType::Physics)
	{ }
	
	virtual void CopyFrom(const FRigBaseElement* InOther) override;

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Physics;
	}

	friend class URigHierarchy;
	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigReferenceElement final : public FRigSingleParentElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigReferenceElement)

	static const EElementIndex ElementTypeIndex;

    FRigReferenceElement()
        : FRigReferenceElement(nullptr)
	{ }
	
	virtual ~FRigReferenceElement() override {}

	virtual void Save(FArchive& A, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase) override;

	FTransform GetReferenceWorldTransform(const FRigVMExecuteContext* InContext, bool bInitial) const;

	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial, bool bWeights) override;

private:
	explicit FRigReferenceElement(URigHierarchy* InOwner)
		: FRigSingleParentElement(InOwner, ERigElementType::Reference)
	{ }

	FRigReferenceGetWorldTransformDelegate GetWorldTransformDelegate;

	virtual void CopyFrom(const FRigBaseElement* InOther) override;

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Reference;
	}

	friend class URigHierarchy;
	friend class URigHierarchyController;
	friend struct FRigBaseElement;
};

UENUM(BlueprintType)
enum class EConnectorType : uint8
{
	Primary, // Single primary connector, non-optional and always visible. When dropped on another element, this connector will resolve to that element.
	Secondary, // Could be multiple, can auto-solve (visible if not solved), can be optional
};


USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigConnectorSettings
{
	GENERATED_BODY()

	FRigConnectorSettings();
	static FRigConnectorSettings DefaultSettings();

	void Save(FArchive& Ar);
	void Load(FArchive& Ar);

	friend uint32 GetTypeHash(const FRigConnectorSettings& Settings);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	FString Description;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	EConnectorType Type;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	bool bOptional;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	TArray<FRigConnectionRuleStash> Rules;

	bool operator == (const FRigConnectorSettings& InOther) const;

	bool operator != (const FRigConnectorSettings& InOther) const
	{
		return !(*this == InOther);
	}

	template<typename T>
	int32 AddRule(const T& InRule)
	{
		return Rules.Emplace(&InRule);
	}

	uint32 GetRulesHash() const;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigConnectorState
{
	GENERATED_BODY()

	FRigConnectorState()
		: Name(NAME_None)
	{}
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	FName Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	FRigElementKey ResolvedTarget;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	FRigConnectorSettings Settings;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigConnectorElement final : public FRigBaseElement
{
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigConnectorElement)

	static const EElementIndex ElementTypeIndex;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Control)
	FRigConnectorSettings Settings;
	
	FRigConnectorElement()
		: FRigConnectorElement(nullptr)
	{}
	FRigConnectorElement(const FRigConnectorElement& InOther)
	{
		*this = InOther;
	}
	FRigConnectorElement& operator=(const FRigConnectorElement& InOther)
	{
		Super::operator=(InOther);
		Settings = InOther.Settings;
		return *this;
	}

	virtual ~FRigConnectorElement() override {}
	
	virtual void Save(FArchive& A, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase) override;

	FRigConnectorState GetConnectorState(const URigHierarchy* InHierarchy) const;

	bool IsPrimary() const { return Settings.Type == EConnectorType::Primary; }
	bool IsSecondary() const { return Settings.Type == EConnectorType::Secondary; }
	bool IsOptional() const { return IsSecondary() && Settings.bOptional; }

private:
	explicit FRigConnectorElement(URigHierarchy* InOwner)
		: FRigBaseElement(InOwner, ERigElementType::Connector)
	{ }
	
	virtual void CopyFrom(const FRigBaseElement* InOther) override;

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Connector;
	}

	friend class URigHierarchy;
	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigSocketState
{
	GENERATED_BODY()
	
	FRigSocketState();
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	FName Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	FRigElementKey Parent;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	FTransform InitialLocalTransform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	FLinearColor Color;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Connector)
	FString Description;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigSocketElement final : public FRigSingleParentElement
{
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigSocketElement)

	static const EElementIndex ElementTypeIndex;
	static const FName ColorMetaName;
	static const FName DescriptionMetaName;
	static const FName DesiredParentMetaName;
	static const FLinearColor SocketDefaultColor;

	FRigSocketElement()
		: FRigSocketElement(nullptr)
	{}
			
	virtual ~FRigSocketElement() override {}
	
	virtual void Save(FArchive& A, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, ESerializationPhase SerializationPhase) override;

	FRigSocketState GetSocketState(const URigHierarchy* InHierarchy) const;

	FLinearColor GetColor(const URigHierarchy* InHierarchy) const;
	void SetColor(const FLinearColor& InColor, URigHierarchy* InHierarchy, bool bNotify = true);

	FString GetDescription(const URigHierarchy* InHierarchy) const;
	void SetDescription(const FString& InDescription, URigHierarchy* InHierarchy, bool bNotify = true);

private:
	explicit FRigSocketElement(URigHierarchy* InOwner)
		: FRigSingleParentElement(InOwner, ERigElementType::Socket)
	{ }
	
	virtual void CopyFrom(const FRigBaseElement* InOther) override;

	static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Socket;
	}

	friend class URigHierarchy;
	friend struct FRigBaseElement;
};


USTRUCT()
struct CONTROLRIG_API FRigHierarchyCopyPasteContentPerElement
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FRigElementKey Key;

	UPROPERTY()
	FString Content;

	UPROPERTY()
	TArray<FRigElementKey> Parents;

	UPROPERTY()
	TArray<FRigElementWeight> ParentWeights;

	UPROPERTY()
	TArray<FTransform> Poses;

	UPROPERTY()
	TArray<bool> DirtyStates;
};

USTRUCT()
struct CONTROLRIG_API FRigHierarchyCopyPasteContent
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<FRigHierarchyCopyPasteContentPerElement> Elements;

	// Maintain properties below for backwards compatibility pre-5.0
	UPROPERTY()
	TArray<ERigElementType> Types;

	UPROPERTY()
	TArray<FString> Contents;

	UPROPERTY()
	TArray<FTransform> LocalTransforms;

	UPROPERTY()
	TArray<FTransform> GlobalTransforms;
};
