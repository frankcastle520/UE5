// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "UObject/SoftObjectPtr.h"

class UDMXLibrary;

namespace UE::DMX
{
	class FDMXPatchToolItem
		: public TSharedFromThis<FDMXPatchToolItem>
	{
	public:
		FDMXPatchToolItem(TSoftObjectPtr<UDMXLibrary> InDMXLibrary)
			: SoftDMXLibrary(InDMXLibrary)
		{}

		const TSoftObjectPtr<UDMXLibrary> SoftDMXLibrary;
	};
}
