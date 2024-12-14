// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <HAL/Platform.h>

#if PLATFORM_WINDOWS

#include <Containers/UnrealString.h>
#include <Misc/ScopeRWLock.h>
#include <Templates/SharedPointer.h>

namespace UE::StylusInput::Private::Windows
{
	void LogCOMError(const FString& Preamble, HRESULT Result);

	inline bool Succeeded(const HRESULT Result, const FString& LogPreamble)
	{
		if (Result >= 0)
		{
			return true;
		}
		LogCOMError(LogPreamble, Result);
		return false;
	}

	inline bool Failed(const HRESULT Result, const FString& LogPreamble)
	{
		if (Result >= 0)
		{
			return false;
		}
		LogCOMError(LogPreamble, Result);
		return true;
	}

	template <typename DataType, bool ThreadSafe>
	class TSharedRefDataContainer
	{
	public:
		TSharedRef<DataType> Add(uint32 ID)
		{
			WriteScopeLockType WriteLock(RWLock);
			return Data.Emplace_GetRef(MakeShared<DataType>(ID));
		}

		bool Contains(uint32 ID) const
		{
			ReadScopeLockType ReadLock(RWLock);
			const TSharedRef<DataType>* TabletContext = Data.FindByPredicate([ID](const TSharedRef<DataType>& Context)
			{
				return Context->ID == ID;
			});
			return TabletContext != nullptr;
		}

		TSharedPtr<DataType> Get(uint32 ID) const
		{
			ReadScopeLockType ReadLock(RWLock);
			const TSharedRef<DataType>* TabletContext = Data.FindByPredicate([ID](const TSharedRef<DataType>& Context)
			{
				return Context->ID == ID;
			});
			return TabletContext ? TabletContext->ToSharedPtr() : TSharedPtr<DataType>{};
		}

		bool Remove(uint32 ID)
		{
			int32 Index;
			{
				ReadScopeLockType ReadLock(RWLock);
				Index = Data.IndexOfByPredicate([ID](const TSharedRef<DataType>& Tc) { return Tc->ID == ID; });
				if (Index == INDEX_NONE)
				{
					return false;
				}
			}
			WriteScopeLockType WriteLock(RWLock);
			Data.RemoveAt(Index, EAllowShrinking::No);
			return true;
		}

		void Clear()
		{
			WriteScopeLockType WriteLock(RWLock);
			Data.Reset();
		}

		void Update(const TSharedRefDataContainer<DataType, false>& InTabletContexts)
		{
			WriteScopeLockType WriteLock(RWLock);
			Data.Reset();
			for (uint32 Index = 0, Num = InTabletContexts.Num(); Index < Num; ++Index)
			{
				Data.Emplace(InTabletContexts[Index]);
			}
		}

		uint32 Num() const { return Data.Num(); }

		const TSharedRef<DataType>& operator[](const uint32 Index) const { return Data[Index]; }

	private:

		struct FNoLock {};
		struct FScopeNoLock { explicit FScopeNoLock(FNoLock&) {} };

		using LockType = std::conditional_t<ThreadSafe, FRWLock, FNoLock>;
		using ReadScopeLockType = std::conditional_t<ThreadSafe, FReadScopeLock, FScopeNoLock>;
		using WriteScopeLockType = std::conditional_t<ThreadSafe, FWriteScopeLock, FScopeNoLock>;
		
		UE_NO_UNIQUE_ADDRESS mutable LockType RWLock;
		TArray<TSharedRef<DataType>> Data;
	};
}

#endif
