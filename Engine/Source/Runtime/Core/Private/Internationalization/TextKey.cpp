// Copyright Epic Games, Inc. All Rights Reserved.

#include "Internationalization/TextKey.h"

#include "AutoRTFM/AutoRTFM.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "Containers/ChunkedArray.h"
#include "Containers/Map.h"
#include "CoreGlobals.h"
#include "HAL/LowLevelMemTracker.h"
#include "Hash/CityHash.h"
#include "Logging/LogMacros.h"
#include "Misc/ByteSwap.h"
#include "Misc/Guid.h"
#include "Misc/LazySingleton.h"
#include "Misc/ScopeRWLock.h"
#include "Modules/VisualizerDebuggingState.h"

DEFINE_LOG_CATEGORY_STATIC(LogTextKey, Log, All);

// Note: If setting this to 0, you'll also want to update the FTextKey natvis to change ",s8" to ",su"
#ifndef UE_TEXTKEY_USE_UTF8
	#define UE_TEXTKEY_USE_UTF8 (1)
#endif

#ifndef UE_TEXTKEY_SLAB_ALLOCATOR_SLAB_SIZE
	#define UE_TEXTKEY_SLAB_ALLOCATOR_SLAB_SIZE (32768)
#endif

#ifndef UE_TEXTKEY_ELEMENTS_CHUNK_SIZE_BYTES
	#define UE_TEXTKEY_ELEMENTS_CHUNK_SIZE_BYTES (32768)
#endif

#ifndef UE_TEXTKEY_ELEMENTS_MIN_HASH_SIZE
	#define UE_TEXTKEY_ELEMENTS_MIN_HASH_SIZE (32768)
#endif

#if UE_TEXTKEY_USE_UTF8
using FTextKeyCharType = UTF8CHAR;
#else
using FTextKeyCharType = TCHAR;
#endif
using FTextKeyStringView = TStringView<FTextKeyCharType>;

class FTextKeyState
{
public:
	FTextKeyState()
	{
		// Register the natvis data accessor
		UE::Core::EVisualizerDebuggingStateResult Result = UE::Core::FVisualizerDebuggingState::Assign(FGuid(0xd31281c0, 0x182b4419, 0x814e25be, 0x4b7e7b41), this);
	}

	void FindOrAdd(FStringView InStr, FTextKey& OutTextKey)
	{
		check(!InStr.IsEmpty());

		// Note: This hash gets serialized so *DO NOT* change it without fixing the serialization to discard the old hash method (also update FTextKey::GetTypeHash)
		const uint32 StrHash = TextKeyUtil::HashString(InStr);

		// Open around adding this in a cache, if we abort just leak the value in the cache
		// as the cache takes ownership
		UE_AUTORTFM_OPEN
		{
			FindOrAddImpl(InStr, StrHash, OutTextKey);
		};
	}

	void FindOrAdd(FStringView InStr, const uint32 InStrHash, FTextKey& OutTextKey)
	{
		check(!InStr.IsEmpty());

		// Open around adding this in a cache, if we abort just leak the value in the cache
		// as the cache takes ownership
		UE_AUTORTFM_OPEN
		{
			FindOrAddImpl(InStr, InStrHash, OutTextKey);
		};
	}

	const TCHAR* GetLegacyTCHARPointerByIndex(const int32 InIndex)
	{
		check(InIndex != INDEX_NONE);

#if UE_TEXTKEY_USE_UTF8
		{
			// Read-only
			int32 NumElementsOnRead = 0;
			{
				FReadScopeLock ScopeLock(DataRW);

				if (const FString* FoundString = LegacyTCHARState.Find(InIndex))
				{
					return **FoundString;
				}

				NumElementsOnRead = LegacyTCHARState.Num();
			}

			// Write
			{
				FWriteScopeLock ScopeLock(DataRW);

				if (LegacyTCHARState.Num() > NumElementsOnRead)
				{
					// Find again in case another thread beat us to it!
					if (const FString* FoundString = LegacyTCHARState.Find(InIndex))
					{
						return **FoundString;
					}
				}

				const FKeyData& KeyData = KeyDataAllocations.Get(InIndex);

				LLM_SCOPE_BYNAME(TEXT("Localization/Deprecated"));
				// Open around adding this in a cache, if we abort just leak the value in the cache
				// as the cache takes ownership
				const TCHAR* AddedString = nullptr;
				UE_AUTORTFM_OPEN
				{
					AddedString = *LegacyTCHARState.Add(InIndex, FString(KeyData.ToView()));
				};
				return AddedString;
			}
		}
#else
		{
			FReadScopeLock ScopeLock(DataRW);

			const FKeyData& KeyData = KeyDataAllocations.Get(InIndex);
			return KeyData.Str;
		}
#endif
	}

	void AppendStringByIndex(const int32 InIndex, FString& Out) const
	{
		check(InIndex != INDEX_NONE);

		FReadScopeLock ScopeLock(DataRW);

		const FKeyData& KeyData = KeyDataAllocations.Get(InIndex);
		Out += KeyData.ToView();
	}

	void AppendStringByIndex(const int32 InIndex, FStringBuilderBase& Out) const
	{
		check(InIndex != INDEX_NONE);

		FReadScopeLock ScopeLock(DataRW);

		const FKeyData& KeyData = KeyDataAllocations.Get(InIndex);
		Out += KeyData.ToView();
	}

	uint32 GetHashByIndex(const int32 InIndex) const
	{
		check(InIndex != INDEX_NONE);

		FReadScopeLock ScopeLock(DataRW);

		const FKeyData& KeyData = KeyDataAllocations.Get(InIndex);
		return KeyData.StrHash;
	}

	void Shrink()
	{
		// Note: Nothing to shrink as things grow in chunks
	}

	static FTextKeyState& GetState()
	{
		return TLazySingleton<FTextKeyState>::Get();
	}

	static void TearDown()
	{
		return TLazySingleton<FTextKeyState>::TearDown();
	}

private:
	struct FKeyData
	{
		FKeyData()
#if UE_TEXTKEY_USE_UTF8
			: Str(UTF8TEXT(""))
#else
			: Str(TEXT(""))
#endif
			, StrLen(0)
			, StrHash(0)
		{
		}

		FKeyData(FTextKeyStringView InStr, const uint32 InStrHash)
			: Str(InStr.GetData())
			, StrLen(InStr.Len())
			, StrHash(InStrHash)
		{
		}

		FKeyData(FTextKeyStringView InStr, const FKeyData& InOther)
			: Str(InStr.GetData())
			, StrLen(InStr.Len())
			, StrHash(InOther.StrHash)
		{
		}

		FTextKeyStringView ToView() const
		{
			return FTextKeyStringView(Str, StrLen);
		}

		friend FORCEINLINE bool operator==(const FKeyData& A, const FKeyData& B)
		{
			// We can use Memcmp here as we know we're comparing two blocks of the same size and don't care about lexical ordering
			return A.StrLen == B.StrLen && FMemory::Memcmp(A.Str, B.Str, A.StrLen * sizeof(FTextKeyCharType)) == 0;
		}

		friend FORCEINLINE bool operator!=(const FKeyData& A, const FKeyData& B)
		{
			// We can use Memcmp here as we know we're comparing two blocks of the same size and don't care about lexical ordering
			return A.StrLen != B.StrLen || FMemory::Memcmp(A.Str, B.Str, A.StrLen * sizeof(FTextKeyCharType)) != 0;
		}

		friend FORCEINLINE uint32 GetTypeHash(const FKeyData& A)
		{
			return A.StrHash;
		}

		const FTextKeyCharType* Str;
		int32 StrLen;
		uint32 StrHash;
	};

	void FindOrAddImpl(FStringView InStr, const uint32 InStrHash, FTextKey& OutTextKey)
	{
		auto ConvertedString = StrCast<FTextKeyCharType>(InStr.GetData(), InStr.Len());
		const FKeyData KeyData = FKeyData(FTextKeyStringView(ConvertedString.Get(), ConvertedString.Length()), InStrHash);

		int32 Index = FindOrAddString(KeyData);
		check(Index != INDEX_NONE);

		OutTextKey.Index = Index;
#if UE_TEXTKEY_STORE_EMBEDDED_HASH
		OutTextKey.StrHash = KeyData.StrHash;
#endif
	}

	int32 FindOrAddString(const FKeyData& KeyData)
	{
		// Read-only
		int32 NumElementsOnRead = 0;
		{
			FReadScopeLock ScopeLock(DataRW);

			if (int32 FoundIndex = KeyDataAllocations.Find(KeyData);
				FoundIndex != INDEX_NONE)
			{
				return FoundIndex;
			}

			NumElementsOnRead = KeyDataAllocations.Num();
		}

		// Write
		{
			FWriteScopeLock ScopeLock(DataRW);

			if (KeyDataAllocations.Num() > NumElementsOnRead)
			{
				// Find again in case another thread beat us to it!
				if (int32 FoundIndex = KeyDataAllocations.Find(KeyData);
					FoundIndex != INDEX_NONE)
				{
					return FoundIndex;
				}
			}

			LLM_SCOPE_BYNAME(TEXT("Localization/TextKeys"));
			const FTextKeyCharType* NewStrPtr = StringAllocations.Add(KeyData.ToView());
			return KeyDataAllocations.Add(FKeyData(FTextKeyStringView(NewStrPtr, KeyData.StrLen), KeyData));
		}
	}

	class FKeyDataAllocator
	{
	public:
		FKeyDataAllocator() = default;

		~FKeyDataAllocator()
		{
			FreeHash();
		}

		int32 Add(const FKeyData& InKeyData)
		{
			ConditionalRehash(Elements.Num() + 1);

			const uint32 KeyDataHash = GetTypeHash(InKeyData);
			const uint32 HashIndex = KeyDataHash & (HashSize - 1);

			const int32 NewElementIndex = Elements.Num();
			Elements.AddElement(FElement{ InKeyData, Hash[HashIndex] });
			Hash[HashIndex] = NewElementIndex;
			return NewElementIndex;
		}

		int32 Find(const FKeyData& InKeyData) const
		{
			if (HashSize > 0)
			{
				const uint32 KeyDataHash = GetTypeHash(InKeyData);
				const uint32 HashIndex = KeyDataHash & (HashSize - 1);

				int32 ElementIndex = Hash[HashIndex];
				while (ElementIndex != INDEX_NONE)
				{
					const FElement& Element = Elements[ElementIndex];
					if (Element.Value == InKeyData)
					{
						return ElementIndex;
					}
					ElementIndex = Element.NextElementIndex;
				}
			}
			return INDEX_NONE;
		}

		const FKeyData& Get(int32 InIndex) const
		{
			return Elements[InIndex].Value;
		}

		int32 Num() const
		{
			return Elements.Num();
		}

	private:
		void AllocateHash()
		{
			Hash = static_cast<int32*>(FMemory::Malloc(HashSize * sizeof(int32)));
			for (uint32 HashIndex = 0; HashIndex < HashSize; ++HashIndex)
			{
				Hash[HashIndex] = INDEX_NONE;
			}
		}

		void FreeHash()
		{
			FMemory::Free(Hash);
			Hash = nullptr;
		}

		void ConditionalRehash(const int32 NumElements)
		{
			const uint32 NewHashSize = FMath::Max<uint32>(MinHashSize, FPlatformMath::RoundUpToPowerOfTwo(NumElements / DEFAULT_NUMBER_OF_ELEMENTS_PER_HASH_BUCKET));
			if (NewHashSize > HashSize)
			{
				FreeHash();
				HashSize = NewHashSize;
				AllocateHash();

				int32 ElementIndex = 0;
				for (FElement& Element : Elements)
				{
					const uint32 KeyDataHash = GetTypeHash(Element.Value);
					const uint32 HashIndex = KeyDataHash & (HashSize - 1);

					Element.NextElementIndex = Hash[HashIndex];
					Hash[HashIndex] = ElementIndex++;
				}
			}
		}

		struct FElement
		{
			FKeyData Value;
			/** Index of the next element in this hash bucket */
			int32 NextElementIndex = INDEX_NONE;
		};
		static constexpr int32 ElementChunkSize = UE_TEXTKEY_ELEMENTS_CHUNK_SIZE_BYTES;
		/** Values; indices are referenced by the hash and FTextKey */
		TChunkedArray<FElement, ElementChunkSize> Elements;

		static constexpr int32 MinHashSize = UE_TEXTKEY_ELEMENTS_MIN_HASH_SIZE;
		/** Current size of the hash; if this changes the hash must be rebuilt */
		uint32 HashSize = 0;
		/** Index of the root element in each hash bucket; use FElement::NextElementIndex to walk the bucket */
		int32* Hash = nullptr;
	};

	class FStringAllocator
	{
	public:
		FStringAllocator() = default;

		~FStringAllocator()
		{
			uint64 TotalNumElementsWasted = 0;
			for (FStringSlab& Slab : Slabs)
			{
				TotalNumElementsWasted += (SlabSizeInElements - Slab.NumElementsUsed);
				FMemory::Free(Slab.Allocation);
			}
			//UE_LOG(LogLocalization, Log, TEXT("FTextKey slab allocator allocated %d slabs (of %d elements) and wasted %d elements (%d bytes)"), Slabs.Num(), SlabSizeInElements, TotalNumElementsWasted, TotalNumElementsWasted * sizeof(FTextKeyCharType));
		}

		const FTextKeyCharType* Add(FTextKeyStringView InStr)
		{
			const int32 NumSlabElementsNeeded = InStr.Len() + 1;
			FStringSlab& Slab = GetSlab(NumSlabElementsNeeded);

			FTextKeyCharType* StringPtr = Slab.Allocation + Slab.NumElementsUsed;
			Slab.NumElementsUsed += NumSlabElementsNeeded;

			FMemory::Memcpy(StringPtr, InStr.GetData(), InStr.Len() * sizeof(FTextKeyCharType));
			*(StringPtr + InStr.Len()) = (FTextKeyCharType)0;

			return StringPtr;
		}

	private:
		static constexpr int32 SlabSizeInElements = UE_TEXTKEY_SLAB_ALLOCATOR_SLAB_SIZE;

		struct FStringSlab
		{
			FTextKeyCharType* Allocation = nullptr;
			int32 NumElementsUsed = 0;
		};

		FStringSlab& GetSlab(const int32 NumSlabElementsNeeded)
		{
			if (Slabs.Num() > 0)
			{
				// Always try the last slab first
				if (FStringSlab& Slab = Slabs.Last();
					(Slab.NumElementsUsed + NumSlabElementsNeeded) <= SlabSizeInElements)
				{
					return Slab;
				}

				if (Slabs.Num() > 1)
				{
					// We only add to the last slab in the array, so if we've run out of space, merge it back into the array based 
					// on its current used size (sorted most used first), and then check to see if the new last slab has space
					FStringSlab SlabToMerge = Slabs.Pop(EAllowShrinking::No);
					const int32 MergeIndex = Algo::LowerBound(Slabs, SlabToMerge, [](const FStringSlab& A, const FStringSlab& B)
					{
						return A.NumElementsUsed > B.NumElementsUsed;
					});
					Slabs.Insert(SlabToMerge, MergeIndex);
					if (MergeIndex != Slabs.Num())
					{
						if (FStringSlab& Slab = Slabs.Last();
							(Slab.NumElementsUsed + NumSlabElementsNeeded) <= SlabSizeInElements)
						{
							return Slab;
						}
					}
				}
			}

			// If no slabs have space then just allocate a new one
			checkf(NumSlabElementsNeeded <= SlabSizeInElements, TEXT("Tried to allocate a FTextKey string of %d elements, which is larger than the allowed slab size of %d elements!"), NumSlabElementsNeeded, SlabSizeInElements);
			FStringSlab& Slab = Slabs.AddDefaulted_GetRef();
			Slab.Allocation = static_cast<FTextKeyCharType*>(FMemory::Malloc(SlabSizeInElements * sizeof(FTextKeyCharType), 1));
			return Slab;
		}

		TArray<FStringSlab> Slabs;
	};

	mutable FRWLock DataRW;
	FStringAllocator StringAllocations;
	FKeyDataAllocator KeyDataAllocations;
#if UE_TEXTKEY_USE_UTF8
	// Sparse TCHAR state; built on-demand by anything still using the deprecated FTextKey::GetChars function
	TMap<int32, FString> LegacyTCHARState;
#endif
};

namespace TextKeyUtil
{

static constexpr int32 InlineStringSize = 128;
using FInlineStringBuffer = TArray<TCHAR, TInlineAllocator<InlineStringSize>>;
using FInlineStringBuilder = TStringBuilder<InlineStringSize>;

static_assert(PLATFORM_LITTLE_ENDIAN, "FTextKey serialization needs updating to support big-endian platforms!");

bool SaveKeyString(FArchive& Ar, const TCHAR* InStrPtr)
{
	// Note: This serialization should be compatible with the FString serialization, but avoids creating an FString if the FTextKey is already cached
	// > 0 for ANSICHAR, < 0 for UTF16CHAR serialization
	check(!Ar.IsLoading());

	const bool bSaveUnicodeChar = Ar.IsForcingUnicode() || !FCString::IsPureAnsi(InStrPtr);
	if (bSaveUnicodeChar)
	{
		// Note: This is a no-op on platforms that are using a 16-bit TCHAR
		FTCHARToUTF16 UTF16String(InStrPtr);
		const int32 Num = UTF16String.Length() + 1; // include the null terminator

		int32 SaveNum = -Num;
		Ar << SaveNum;

		if (Num)
		{
			Ar.Serialize((void*)UTF16String.Get(), sizeof(UTF16CHAR) * Num);
		}
	}
	else
	{
		int32 Num = FCString::Strlen(InStrPtr) + 1; // include the null terminator
		Ar << Num;

		if (Num)
		{
			Ar.Serialize((void*)StringCast<ANSICHAR>(InStrPtr, Num).Get(), sizeof(ANSICHAR) * Num);
		}
	}

	return true;
}

bool LoadKeyString(FArchive& Ar, FInlineStringBuffer& OutStrBuffer)
{
	// Note: This serialization should be compatible with the FString serialization, but avoids creating an FString if the FTextKey is already cached
	// > 0 for ANSICHAR, < 0 for UTF16CHAR serialization
	check(Ar.IsLoading());

	int32 SaveNum = 0;
	Ar << SaveNum;

	const bool bLoadUnicodeChar = SaveNum < 0;
	if (bLoadUnicodeChar)
	{
		SaveNum = -SaveNum;
	}

	// If SaveNum is still less than 0, they must have passed in MIN_INT. Archive is corrupted.
	if (SaveNum < 0)
	{
		Ar.SetCriticalError();
		return false;
	}

	// Protect against network packets allocating too much memory
	const int64 MaxSerializeSize = Ar.GetMaxSerializeSize();
	if ((MaxSerializeSize > 0) && (SaveNum > MaxSerializeSize))
	{
		Ar.SetCriticalError();
		return false;
	}

	// Create a buffer of the correct size
	OutStrBuffer.AddUninitialized(SaveNum);

	if (SaveNum)
	{
		if (bLoadUnicodeChar)
		{
			// Read in the Unicode string
			auto Passthru = StringMemoryPassthru<UCS2CHAR, TCHAR, InlineStringSize>(OutStrBuffer.GetData(), SaveNum, SaveNum);
			Ar.Serialize(Passthru.Get(), SaveNum * sizeof(UCS2CHAR));
			Passthru.Get()[SaveNum - 1] = 0; // Ensure the string has a null terminator
			Passthru.Apply();

			// Inline combine any surrogate pairs in the data when loading into a UTF-32 string
			StringConv::InlineCombineSurrogates_Array(OutStrBuffer);
		}
		else
		{
			// Read in the ANSI string
			auto Passthru = StringMemoryPassthru<ANSICHAR, TCHAR, InlineStringSize>(OutStrBuffer.GetData(), SaveNum, SaveNum);
			Ar.Serialize(Passthru.Get(), SaveNum * sizeof(ANSICHAR));
			Passthru.Get()[SaveNum - 1] = 0; // Ensure the string has a null terminator
			Passthru.Apply();
		}

		UE_CLOG(SaveNum > InlineStringSize, LogTextKey, VeryVerbose, TEXT("Key string '%s' was larger (%d) than the inline size (%d) and caused an allocation!"), OutStrBuffer.GetData(), SaveNum, InlineStringSize);
	}

	return true;
}

uint32 HashString(const FTCHARToUTF16& InStr)
{
	const uint64 StrHash = CityHash64((const char*)InStr.Get(), InStr.Length() * sizeof(UTF16CHAR));
	return GetTypeHash(StrHash);
}

}

FTextKey::FTextKey(FStringView InStr)
{
	if (InStr.IsEmpty())
	{
		Reset();
	}
	else
	{
		FTextKeyState::GetState().FindOrAdd(InStr, *this);
	}
}

const TCHAR* FTextKey::GetChars() const
{
	return Index != INDEX_NONE
		? FTextKeyState::GetState().GetLegacyTCHARPointerByIndex(Index)
		: TEXT("");
}

FString FTextKey::ToString() const
{
	FString Out;
	AppendString(Out);
	return Out;
}

void FTextKey::ToString(FString& Out) const
{
	Out.Reset();
	AppendString(Out);
}

void FTextKey::ToString(FStringBuilderBase& Out) const
{
	Out.Reset();
	AppendString(Out);
}

void FTextKey::AppendString(FString& Out) const
{
	if (Index != INDEX_NONE)
	{
		FTextKeyState::GetState().AppendStringByIndex(Index, Out);
	}
}

void FTextKey::AppendString(FStringBuilderBase& Out) const
{
	if (Index != INDEX_NONE)
	{
		FTextKeyState::GetState().AppendStringByIndex(Index, Out);
	}
}

uint32 GetTypeHash(const FTextKey& A)
{
#if UE_TEXTKEY_STORE_EMBEDDED_HASH
	return A.StrHash;
#else
	return A.Index != INDEX_NONE
		? FTextKeyState::GetState().GetHashByIndex(A.Index)
		: 0;
#endif
}

void FTextKey::SerializeAsString(FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		TextKeyUtil::FInlineStringBuffer StrBuffer;
		TextKeyUtil::LoadKeyString(Ar, StrBuffer);

		if (StrBuffer.Num() <= 1)
		{
			Reset();
		}
		else
		{
			FTextKeyState::GetState().FindOrAdd(FStringView(StrBuffer.GetData(), StrBuffer.Num() - 1), *this);
		}
	}
	else
	{
		TextKeyUtil::FInlineStringBuilder StrBuilder;
		AppendString(StrBuilder);
		TextKeyUtil::SaveKeyString(Ar, StrBuilder.ToString());
	}
}

void FTextKey::SerializeWithHash(FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		uint32 TmpStrHash = 0;
		Ar << TmpStrHash;

		TextKeyUtil::FInlineStringBuffer StrBuffer;
		TextKeyUtil::LoadKeyString(Ar, StrBuffer);

		if (StrBuffer.Num() <= 1)
		{
			Reset();
		}
		else
		{
			FTextKeyState::GetState().FindOrAdd(FStringView(StrBuffer.GetData(), StrBuffer.Num() - 1), TmpStrHash, *this);
		}
	}
	else
	{
		uint32 TmpStrHash = GetTypeHash(*this);
		Ar << TmpStrHash;

		TextKeyUtil::FInlineStringBuilder StrBuilder;
		AppendString(StrBuilder);
		TextKeyUtil::SaveKeyString(Ar, StrBuilder.ToString());
	}
}

void FTextKey::SerializeDiscardHash(FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		uint32 DiscardedHash = 0;
		Ar << DiscardedHash;

		TextKeyUtil::FInlineStringBuffer StrBuffer;
		TextKeyUtil::LoadKeyString(Ar, StrBuffer);

		if (StrBuffer.Num() <= 1)
		{
			Reset();
		}
		else
		{
			FTextKeyState::GetState().FindOrAdd(FStringView(StrBuffer.GetData(), StrBuffer.Num() - 1), *this);
		}
	}
	else
	{
		uint32 TmpStrHash = GetTypeHash(*this);
		Ar << TmpStrHash;

		TextKeyUtil::FInlineStringBuilder StrBuilder;
		AppendString(StrBuilder);
		TextKeyUtil::SaveKeyString(Ar, StrBuilder.ToString());
	}
}

void FTextKey::SerializeAsString(FStructuredArchiveSlot Slot)
{
	if (Slot.GetArchiveState().IsTextFormat())
	{
		if (Slot.GetUnderlyingArchive().IsLoading())
		{
			FString TmpStr;
			Slot << TmpStr;

			if (TmpStr.IsEmpty())
			{
				Reset();
			}
			else
			{
				FTextKeyState::GetState().FindOrAdd(TmpStr, *this);
			}
		}
		else
		{
			FString TmpStr = ToString();
			Slot << TmpStr;
		}
	}
	else
	{
		Slot.EnterStream(); // Let the slot know that we will custom serialize
		SerializeAsString(Slot.GetUnderlyingArchive());
	}
}

void FTextKey::SerializeWithHash(FStructuredArchiveSlot Slot)
{
	if (Slot.GetArchiveState().IsTextFormat())
	{
		FStructuredArchiveRecord Record = Slot.EnterRecord();

		if (Slot.GetUnderlyingArchive().IsLoading())
		{
			uint32 TmpStrHash = 0;
			Record << SA_VALUE(TEXT("Hash"), TmpStrHash);

			FString TmpStr;
			Record << SA_VALUE(TEXT("Str"), TmpStr);

			if (TmpStr.IsEmpty())
			{
				Reset();
			}
			else
			{
				FTextKeyState::GetState().FindOrAdd(TmpStr, TmpStrHash, *this);
			}
		}
		else
		{
			uint32 TmpStrHash = GetTypeHash(*this);
			Record << SA_VALUE(TEXT("Hash"), TmpStrHash);

			FString TmpStr = ToString();
			Record << SA_VALUE(TEXT("Str"), TmpStr);
		}
	}
	else
	{
		Slot.EnterStream(); // Let the slot know that we will custom serialize
		SerializeWithHash(Slot.GetUnderlyingArchive());
	}
}

void FTextKey::SerializeDiscardHash(FStructuredArchiveSlot Slot)
{
	if (Slot.GetArchiveState().IsTextFormat())
	{
		FStructuredArchiveRecord Record = Slot.EnterRecord();

		if (Slot.GetUnderlyingArchive().IsLoading())
		{
			uint32 DiscardedHash = 0;
			Record << SA_VALUE(TEXT("Hash"), DiscardedHash);

			FString TmpStr;
			Record << SA_VALUE(TEXT("Str"), TmpStr);

			if (TmpStr.IsEmpty())
			{
				Reset();
			}
			else
			{
				FTextKeyState::GetState().FindOrAdd(TmpStr, *this);
			}
		}
		else
		{
			uint32 TmpStrHash = GetTypeHash(*this);
			Record << SA_VALUE(TEXT("Hash"), TmpStrHash);

			FString TmpStr = ToString();
			Record << SA_VALUE(TEXT("Str"), TmpStr);
		}
	}
	else
	{
		Slot.EnterStream(); // Let the slot know that we will custom serialize
		SerializeDiscardHash(Slot.GetUnderlyingArchive());
	}
}

void FTextKey::CompactDataStructures()
{
	FTextKeyState::GetState().Shrink();
}

void FTextKey::TearDown()
{
	FTextKeyState::TearDown();
}
