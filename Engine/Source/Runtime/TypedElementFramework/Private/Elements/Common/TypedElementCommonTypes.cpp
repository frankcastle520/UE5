// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Common/TypedElementCommonTypes.h"

namespace UE
{
	namespace Editor::DataStorage
	{
		const FName& FValueTag::GetName() const
		{
			return Name;
		}
		
		FValueTag::FValueTag(const FName& InTypeName)
			: Name(InTypeName)
		{}
		
		uint32 GetTypeHash(const FValueTag& InName)
		{
			return GetTypeHash(InName.Name);
		}

		uint32 GetTypeHash(const FDynamicColumnDescription& Descriptor)
		{
			return HashCombineFast(PointerHash(Descriptor.TemplateType), GetTypeHash(Descriptor.Identifier));
		}
	}
}
