// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericHash.h"

#include "Containers/ArrayView.h"
#include "IO/IoHash.h"
#include "Misc/StringBuilder.h"
#include "String/BytesToHex.h"

namespace UE::GenericHash
{

static_assert(sizeof(FIoHash) == 20);

FStringBuilderBase& ToHex(FMemoryView Memory, FStringBuilderBase& Out)
{
	UE::String::BytesToHexLower(
		MakeArrayView<const uint8>(
			reinterpret_cast<const uint8*>(Memory.GetData()),
			IntCastChecked<int32>(Memory.GetSize())),
		Out);

	return Out;
}

FString ToHex(FMemoryView Memory)
{
	FStringBuilderBase Sb;
	ToHex(Memory, Sb);
	return Sb.ToString();
}

} // namesapce UE::GenericHash
