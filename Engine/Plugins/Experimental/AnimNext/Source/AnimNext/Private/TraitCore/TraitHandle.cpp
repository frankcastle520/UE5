// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraitCore/TraitHandle.h"
#include "TraitCore/TraitReader.h"
#include "Serialization/Archive.h"

bool FAnimNextTraitHandle::Serialize(FArchive& Ar)
{
	Ar << PackedTraitIndexAndNodeHandle;

	if (Ar.IsLoading() && IsValid())
	{
		// On load, we hold a node ID that needs fix-up
		check(GetNodeHandle().IsNodeID());

		UE::AnimNext::FTraitReader& TraitReader = static_cast<UE::AnimNext::FTraitReader&>(Ar);
		*this = TraitReader.ResolveTraitHandle(*this);
		check(GetNodeHandle().IsSharedOffset());
	}

	return true;
}
