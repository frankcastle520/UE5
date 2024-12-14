// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/Utf8StrProperty.h"

// Include StrProperty.cpp.inl's includes before defining the macros, in case the macros 'poison' other headers or there are re-entrant includes.
#include "UObject/StrPropertyIncludes.cpp.inl"

#define UE_STRPROPERTY_CLASS                FUtf8StrProperty
#define UE_STRPROPERTY_STRINGTYPE           FUtf8String
#define UE_STRPROPERTY_CASTCLASSFLAG        CASTCLASS_FUtf8StrProperty
#define UE_STRPROPERTY_PROPERTYPARAMSSTRUCT FUtf8StrPropertyParams
	#include "UObject/StrProperty.cpp.inl"
#undef UE_STRPROPERTY_PROPERTYPARAMSSTRUCT
#undef UE_STRPROPERTY_CASTCLASSFLAG
#undef UE_STRPROPERTY_STRINGTYPE
#undef UE_STRPROPERTY_CLASS
