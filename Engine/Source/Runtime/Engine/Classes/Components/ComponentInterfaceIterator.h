// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/ComponentInterfaces.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

template< class T > 
class TComponentInterfaceIterator
{
public:

	enum EEndTagType
	{
		EndTag
	};

	explicit TComponentInterfaceIterator(EObjectFlags AdditionalExclusionFlags = RF_ClassDefaultObject, bool bIncludeDerivedClasses = true, EInternalObjectFlags InInternalExclusionFlags = EInternalObjectFlags::None)
		: Index(-1)
	{
		TArray<UObject*>	ObjectArray;

		for (FComponentInterfaceImplementation& Implementation : T::Implementers)
		{
			ObjectArray.Reset();
			GetObjectsOfClass(Implementation.Class, ObjectArray, bIncludeDerivedClasses, AdditionalExclusionFlags, GetObjectIteratorDefaultInternalExclusionFlags(InInternalExclusionFlags));
			Interfaces.Reserve(ObjectArray.Num() + Interfaces.Num());
			for (UObject* Object : ObjectArray)
			{
				T* Interface = reinterpret_cast<T*>(Implementation.Resolver(Object));
				check(Interface);
				Interfaces.Add(Interface);
			}
		}

		Index = 0;
	}

	TComponentInterfaceIterator(EEndTagType, const TComponentInterfaceIterator<T>& InIter )
		: Index(InIter.Interfaces.Num())
	{
	}

	FORCEINLINE void operator++(int)
	{		
		Index++;
	}	

	FORCEINLINE void operator++()
	{		
		Index++;
	}

	/** Conversion to "bool" returning true if the iterator is valid. */
	FORCEINLINE explicit operator bool() const
	{ 
		return Interfaces.IsValidIndex(Index); 
	}

	/** Conversion to "bool" returning true if the iterator is valid. */
	FORCEINLINE bool operator !() const 
	{
		return !(bool)*this;
	}

	FORCEINLINE T* operator*() const
	{
		if (Interfaces.IsValidIndex(Index))
		{
			return  Interfaces[Index];
		}
		else
		{
			return nullptr;
		}
	}

	FORCEINLINE T* operator->() const
	{
		if (Interfaces.IsValidIndex(Index))
		{
			return  Interfaces[Index];
		}
		else
		{
			return nullptr;
		}
	}

	FORCEINLINE bool operator==(const TComponentInterfaceIterator& Rhs) const { return Index == Rhs.Index; }
	FORCEINLINE bool operator!=(const TComponentInterfaceIterator& Rhs) const { return Index != Rhs.Index; }

protected:
	/** Resolved results for all the Results from the GetObjectsOfClass queries */
	TArray<T*>	Interfaces;
	
	/** index of the current element in the object array */
	int32 Index;
};

template <typename T>
struct TComponentInterfaceRange
{
	TComponentInterfaceRange()
	{
	}

	friend TComponentInterfaceIterator<T> begin(const TComponentInterfaceRange& Range) { return Range.Begin; }
	friend TComponentInterfaceIterator<T> end  (const TComponentInterfaceRange& Range) { return TComponentInterfaceIterator<T>(TComponentInterfaceIterator<T>::EndTag, Range.Begin); }

	TComponentInterfaceIterator<T> Begin;
};
