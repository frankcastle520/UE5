// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Containers/StringView.h"
#include "VVMAtomics.h"
#include "VVMAux.h"
#include "VVMEmergentTypeCreator.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VVMHeap.h"
#include "VerseVM/VVMLog.h"

namespace Verse
{

struct VInt;
struct FOpResult;

enum class EArrayType : uint8
{
	None,
	VValue,
	Int32,
	Char8,
	Char32
};

inline bool IsString(EArrayType Type)
{
	return Type == EArrayType::Char8;
}

inline size_t ByteLength(EArrayType ArrayType)
{
	switch (ArrayType)
	{
		case EArrayType::None:
			return 0; // Empty-Untyped VMutableArray
		case EArrayType::VValue:
			return sizeof(TWriteBarrier<VValue>);
		case EArrayType::Int32:
			return sizeof(int32);
		case EArrayType::Char8:
			return sizeof(UTF8CHAR);
		case EArrayType::Char32:
			return sizeof(UTF32CHAR);
		default:
			V_DIE("Unhandled EArrayType encountered!");
	}
}

struct VBuffer : TAux<void>
{
	// Note: We don't need to align char/char32 arrays this way. However, that will
	// make accessing the data branch on Type. So it might never be worth doing.
	struct alignas(sizeof(VValue)) Header
	{
		uint32 NumValues;
		// These are immutable per VBuffer. This means that if the GC sees a buffer with
		// a particular type, it's type can't change while it's scanning the buffer.
		const uint32 Capacity;
		const EArrayType Type;
	};

	VBuffer() = default;

	VBuffer(FAllocationContext Context, uint32 NumValues, uint32 Capacity, EArrayType Type)
	{
		V_DIE_IF(Type == EArrayType::None);
		V_DIE_UNLESS(Capacity >= NumValues);

		// If we are UTF8, add +1 to capacity and set a null-terminator
		uint32 AllocationCapacity = Capacity + (IsString(Type) ? 1 : 0);
		V_DIE_UNLESS(AllocationCapacity > 0);

		Ptr = Context.AllocateAuxCell(sizeof(Header) + ByteLength(Type) * AllocationCapacity);
		new (GetHeader()) Header{NumValues, Capacity, Type};

		if (IsString(Type))
		{
			SetNullTerminator();
		}
	}

	VBuffer(FAllocationContext Context, uint32 NumValues, EArrayType Type)
		: VBuffer(Context, NumValues, NumValues, Type)
	{
	}

	Header* GetHeader() const
	{
		return static_cast<Header*>(Ptr);
	}

	void* GetDataStart()
	{
		return Ptr ? static_cast<char*>(Ptr) + sizeof(Header) : nullptr;
	}

	const void* GetDataStart() const
	{
		return const_cast<VBuffer*>(this)->GetDataStart();
	}

	EArrayType GetArrayType() const
	{
		if (Header* Header = GetHeader())
		{
			checkSlow(Header->Type != EArrayType::None);
			return Header->Type;
		}
		return EArrayType::None;
	}

	uint32 Num() const
	{
		if (Header* Header = GetHeader())
		{
			return Header->NumValues;
		}
		return 0;
	}

	uint32 Capacity() const
	{
		if (Header* Header = GetHeader())
		{
			return Header->Capacity;
		}
		return 0;
	}

	void SetNullTerminator()
	{
		SetChar(Num(), static_cast<UTF8CHAR>(0));
	}

	template <bool bTransactional>
	void SetVValue(FAllocationContext Context, uint32 Index, VValue Value)
	{
		checkSlow(GetArrayType() == EArrayType::VValue);
		if constexpr (bTransactional)
		{
			GetData<TWriteBarrier<VValue>>()[Index].SetTransactionally(Context, *this, Value);
		}
		else
		{
			new (&GetData<TWriteBarrier<VValue>>()[Index]) TWriteBarrier<VValue>(Context, Value);
		}
	}
	void SetInt32(uint32 Index, int32 Value)
	{
		checkSlow(GetArrayType() == EArrayType::Int32);
		GetData<int32>()[Index] = Value;
	}
	void SetChar(uint32 Index, UTF8CHAR Value)
	{
		checkSlow(GetArrayType() == EArrayType::Char8);
		GetData<UTF8CHAR>()[Index] = Value;
	}
	void SetChar32(uint32 Index, UTF32CHAR Value)
	{
		checkSlow(GetArrayType() == EArrayType::Char32);
		GetData<UTF32CHAR>()[Index] = Value;
	}

	template <typename T = void>
	T* GetData() { return BitCast<T*>(GetDataStart()); }

	template <typename T = void>
	const T* GetData() const { return BitCast<T*>(GetDataStart()); }
};

static_assert(IsTAux<VBuffer>);

struct VArrayBase : VHeapValue
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VHeapValue);

protected:
	TWriteBarrier<VBuffer> Buffer;

	template <bool bTransactional = false>
	void SetBufferWithoutStoreBarrier(FAccessContext Context, VBuffer NewBuffer)
	{
		if constexpr (bTransactional)
		{
			reinterpret_cast<TWriteBarrier<TAux<void>>&>(Buffer).SetTransactionally(Context, this, NewBuffer);
		}
		else
		{
			Buffer.Set(Context, NewBuffer);
		}
	}
	template <bool bTransactional = false>
	void SetBufferWithStoreBarrier(FAccessContext Context, VBuffer NewBuffer)
	{
		StoreStoreFence();
		SetBufferWithoutStoreBarrier<bTransactional>(Context, NewBuffer);
	}

	static EArrayType DetermineArrayType(VValue Value)
	{
		if (Value.IsInt32())
		{
			return EArrayType::Int32;
		}
		if (Value.IsChar())
		{
			return EArrayType::Char8;
		}
		if (Value.IsChar32())
		{
			return EArrayType::Char32;
		}
		return EArrayType::VValue;
	}

	static EArrayType DetermineCombinedType(EArrayType A, EArrayType B)
	{
		return A == B ? A : EArrayType::VValue;
	}

	VArrayBase(FAllocationContext Context, uint32 NumValues, uint32 Capacity, EArrayType ArrayType, VEmergentType* Type)
		: VHeapValue(Context, Type)
	{
		SetIsDeeplyMutable();

		V_DIE_UNLESS(Capacity >= NumValues);
		if (ArrayType != EArrayType::None && Capacity)
		{
			SetBufferWithoutStoreBarrier(Context, VBuffer(Context, NumValues, Capacity, ArrayType));
		}
		else
		{
			V_DIE_IF(NumValues);
		}
	}

	VArrayBase(FAllocationContext Context, uint32 NumValues, EArrayType ArrayType, VEmergentType* Type)
		: VArrayBase(Context, NumValues, NumValues, ArrayType, Type)
	{
	}

	VArrayBase(FAllocationContext Context, std::initializer_list<VValue> InitList, VEmergentType* Type)
		: VHeapValue(Context, Type)
	{
		SetIsDeeplyMutable();

		if (InitList.size())
		{
			SetBufferWithoutStoreBarrier(Context, VBuffer(Context, InitList.size(), DetermineArrayType(*InitList.begin())));
			uint32 Index = 0;
			for (VValue Value : InitList)
			{
				SetValue(Context, Index++, Value);
			}
		}
	}

	template <typename InitIndexFunc, typename = std::enable_if_t<std::is_same_v<VValue, std::invoke_result_t<InitIndexFunc, uint32>>>>
	VArrayBase(FAllocationContext Context, uint32 NumValues, InitIndexFunc&& InitFunc, VEmergentType* Type)
		: VHeapValue(Context, Type)
	{
		SetIsDeeplyMutable();

		if (NumValues)
		{
			VValue Elem0 = InitFunc(0); // Avoid invoking InitFunc twice for element 0
			SetBufferWithoutStoreBarrier(Context, VBuffer(Context, NumValues, DetermineArrayType(Elem0)));
			for (uint32 Index = 0; Index < NumValues; ++Index)
			{
				SetValue(Context, Index, Index > 0 ? InitFunc(Index) : Elem0);
			}
		}
	}

	VArrayBase(FAllocationContext Context, FUtf8StringView String, VEmergentType* Type)
		: VHeapValue(Context, Type)
		, Buffer(Context, VBuffer(Context, String.Len(), EArrayType::Char8))
	{
		SetIsDeeplyMutable();
		FMemory::Memcpy(GetData(), String.GetData(), String.Len());
	}

	void SetNullTerminator()
	{
		Buffer.Get().SetNullTerminator();
	}

	template <bool bTransactional = false>
	void ConvertDataToVValues(FAllocationContext Context, uint32 NewCapacity);

	template <typename T>
	static void Serialize(T*& This, FAllocationContext Context, FAbstractVisitor& Visitor);

public:
	uint32 Num() const { return Buffer.Get().Num(); }
	uint32 Capacity() const { return Buffer.Get().Capacity(); }
	bool IsInBounds(uint32 Index) const;
	bool IsInBounds(const VInt& Index, const uint32 Bounds) const;
	VValue GetValue(uint32 Index);
	const VValue GetValue(uint32 Index) const;

	/// Capacity parameter is required for handling when a re-allocation to VValues takes place during SetValue from a VMutableArray.
protected:
	template <bool bTransactional>
	void SetValueImpl(FAllocationContext Context, uint32 Index, VValue Value);

public:
	void SetValue(FAllocationContext Context, uint32 Index, VValue Value);
	void SetValueTransactionally(FAllocationContext Context, uint32 Index, VValue Value);
	template <bool bTransactional = false>
	void SetVValue(FAllocationContext Context, uint32 Index, VValue Value)
	{
		Buffer.Get().SetVValue<bTransactional>(Context, Index, Value);
	}
	void SetInt32(uint32 Index, int32 Value)
	{
		Buffer.Get().SetInt32(Index, Value);
	}
	void SetChar(uint32 Index, UTF8CHAR Value)
	{
		Buffer.Get().SetChar(Index, Value);
	}
	void SetChar32(uint32 Index, UTF32CHAR Value)
	{
		Buffer.Get().SetChar32(Index, Value);
	}

	void* GetData() { return Buffer.Get().GetData(); };
	const void* GetData() const { return Buffer.Get().GetData(); };

	template <typename T>
	T* GetData() { return Buffer.Get().GetData<T>(); }
	template <typename T>
	const T* GetData() const { return Buffer.Get().GetData<T>(); }

	EArrayType GetArrayType() const { return Buffer.Get().GetArrayType(); }

	size_t ByteLength() const
	{
		return Num() * ::Verse::ByteLength(GetArrayType());
	}

	bool IsString() const
	{
		if (GetArrayType() == EArrayType::VValue)
		{
			for (uint32 Index = 0, End = Num(); Index < End; ++Index)
			{
				if (!GetValue(Index).IsChar())
				{
					return false;
				}
			}
		}
		return ::Verse::IsString(GetArrayType());
	}

	FString AsString() const
	{
		if (GetArrayType() == EArrayType::VValue)
		{
			FString String = FString::ConstructWithSlack(UTF8TEXT(""), Num());
			for (uint32 Index = 0, End = Num(); Index < End; ++Index)
			{
				if (!GetValue(Index).IsChar())
				{
					V_DIE("Couldn't convert index %d to char! Partially parsed: %s", Index, *String);
				}
				String[Index] = GetValue(Index).AsChar();
			}
			return String;
		}
		if (::Verse::IsString(GetArrayType()))
		{
			return FString(GetData<UTF8CHAR>());
		}
		V_DIE("Couldn't convert Array to String!");
		return FString();
	}

	FUtf8StringView AsStringView() const
	{
		if (::Verse::IsString(GetArrayType()))
		{
			return FUtf8StringView(GetData<UTF8CHAR>(), Num());
		}
		V_DIE("Couldn't convert Array to String!");
		return FUtf8StringView();
	}

	bool Equals(const FUtf8StringView String) const
	{
		if (GetArrayType() == EArrayType::VValue)
		{
			for (uint32 Index = 0, End = Num(); Index < End; ++Index)
			{
				VValue Val = GetValue(Index);
				if (!Val.IsChar() || Val.AsChar() != String[Index])
				{
					return false;
				}
			}
			return true;
		}
		else if (::Verse::IsString(GetArrayType()))
		{
			return AsStringView().Equals(String);
		}
		return false;
	}

	COREUOBJECT_API bool EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder);

	COREUOBJECT_API VValue MeltImpl(FAllocationContext Context);

	COREUOBJECT_API uint32 GetTypeHashImpl();

	COREUOBJECT_API void ToStringImpl(FStringBuilderBase& Builder, FAllocationContext Context, const FCellFormatter& Formatter);

	// C++ ranged-based iteration
	class FConstIterator
	{
		union
		{
			const TWriteBarrier<VValue>* Barrier;
			const int32* Int32;
			const UTF8CHAR* Char;
			const UTF32CHAR* Char32;
			const void* None;
		};
		EArrayType ArrayType;

	public:
		FORCEINLINE VValue operator*() const
		{
			switch (ArrayType)
			{
				case EArrayType::VValue:
					return Barrier->Get();
				case EArrayType::Int32:
					return VValue::FromInt32(*Int32);
				case EArrayType::Char8:
					return VValue::Char(*Char);
				case EArrayType::Char32:
					return VValue::Char32(*Char32);
				default:
					V_DIE("Unhandled EArrayType encountered!");
			}
		}

		// Don't need to worry about the data-type here as we are just doing pointer comparison
		FORCEINLINE bool operator==(const FConstIterator& Rhs) const { return Barrier == Rhs.Barrier; }
		FORCEINLINE bool operator!=(const FConstIterator& Rhs) const { return Barrier != Rhs.Barrier; }

		FORCEINLINE FConstIterator& operator++()
		{
			switch (ArrayType)
			{
				case EArrayType::VValue:
					++Barrier;
					break;
				case EArrayType::Int32:
					++Int32;
					break;
				case EArrayType::Char8:
					++Char;
					break;
				case EArrayType::Char32:
					++Char32;
					break;
				default:
					V_DIE("Unhandled EArrayType encountered!");
			}
			return *this;
		}

	private:
		friend struct VArrayBase;
		FORCEINLINE FConstIterator(const TWriteBarrier<VValue>* InCurrentValue)
			: Barrier(InCurrentValue)
			, ArrayType(EArrayType::VValue) {}
		FORCEINLINE FConstIterator(const int32* InCurrentValue)
			: Int32(InCurrentValue)
			, ArrayType(EArrayType::Int32) {}
		FORCEINLINE FConstIterator(const UTF8CHAR* InCurrentValue)
			: Char(InCurrentValue)
			, ArrayType(EArrayType::Char8) {}
		FORCEINLINE FConstIterator(const UTF32CHAR* InCurrentValue)
			: Char32(InCurrentValue)
			, ArrayType(EArrayType::Char32) {}
		FORCEINLINE FConstIterator(const void* InCurrentValue)
			: None(InCurrentValue)
			, ArrayType(EArrayType::None) {}
	};

	COREUOBJECT_API FConstIterator begin() const;
	COREUOBJECT_API FConstIterator end() const;
};

} // namespace Verse
#endif // WITH_VERSE_VM
