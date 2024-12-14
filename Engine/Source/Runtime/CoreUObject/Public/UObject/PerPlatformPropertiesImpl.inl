// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
 * PerPlatformPropertiesImpl.inl: Serializer implementation
 *
 * This file needs to be included by a cpp once for each module that declares a PerPlatformProperty
 * And following the include, template instantiations of the serializaers for each property class, for example:
 * 
 * template SLATECORE_API FArchive& operator<<(FArchive&, TPerPlatformProperty<FPerPlatformMyEnumType, EMyEnumType, NAME_EnumProperty>&);
 * template SLATECORE_API void operator<<(FStructuredArchive::FSlot Slot, TPerPlatformProperty<FPerPlatformMyEnumType, EMyEnumType, NAME_EnumProperty>&);
 * =============================================================================*/

#pragma once

#if WITH_EDITOR
#include "Interfaces/ITargetPlatform.h"
#endif

/** Serializer to cook out the most appropriate platform override */
template<typename StructType, typename ValueType, EName BasePropertyName>
FArchive& operator<<(FArchive& Ar, TPerPlatformProperty<StructType, ValueType, BasePropertyName>& Property)
{
	bool bCooked = false;
#if WITH_EDITOR
	if (Ar.IsCooking())
	{
		bCooked = true;
		Ar << bCooked;
		// Save out platform override if it exists and Default otherwise
		ValueType Value = Property.GetValueForPlatform(*Ar.CookingTarget()->IniPlatformName());
		Ar << Value;
	}
	else
#endif
	{
		StructType* This = StaticCast<StructType*>(&Property);
		Ar << bCooked;
		Ar << This->Default;
#if WITH_EDITORONLY_DATA
		if (!bCooked)
		{
			using MapType = decltype(This->PerPlatform);
			using KeyFuncs = typename PerPlatformProperty::Private::KeyFuncs<MapType>;
			KeyFuncs::SerializePerPlatformMap(Ar, This->PerPlatform);
		}
#endif
	}
	return Ar;
}

/** Serializer to cook out the most appropriate platform override */
template<typename StructType, typename ValueType, EName BasePropertyName>
void operator<<(FStructuredArchive::FSlot Slot, TPerPlatformProperty<StructType, ValueType, BasePropertyName>& Property)
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	bool bCooked = false;
#if WITH_EDITOR
	if (UnderlyingArchive.IsCooking())
	{
		bCooked = true;
		Record << SA_VALUE(TEXT("bCooked"), bCooked);
		// Save out platform override if it exists and Default otherwise
		ValueType Value = Property.GetValueForPlatform(*UnderlyingArchive.CookingTarget()->IniPlatformName());
		Record << SA_VALUE(TEXT("Value"), Value);
	}
	else
#endif
	{
		StructType* This = StaticCast<StructType*>(&Property);
		Record << SA_VALUE(TEXT("bCooked"), bCooked);
		Record << SA_VALUE(TEXT("Value"), This->Default);
#if WITH_EDITORONLY_DATA
		if (!bCooked)
		{
			using MapType = decltype(This->PerPlatform);
			using KeyFuncs = typename PerPlatformProperty::Private::KeyFuncs<MapType>;
			KeyFuncs::SerializePerPlatformMap(UnderlyingArchive, Record, This->PerPlatform);
		}
#endif
	}
}

