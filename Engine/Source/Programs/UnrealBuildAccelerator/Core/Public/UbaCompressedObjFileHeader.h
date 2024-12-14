// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaHash.h"

namespace uba
{
	struct CompressedObjFileHeader
	{
		inline static constexpr u8 Magic[] = { 'U', 'B', 'A', 0x01 };

		CompressedObjFileHeader(const CasKey& key) : casKey(key)
		{
			memcpy(&magic, Magic, sizeof(magic));
		}

		bool IsValid() const { return memcmp(&magic, Magic, sizeof(Magic)) == 0; }

		u8 magic[sizeof(Magic)];
		CasKey casKey;
	};
}
