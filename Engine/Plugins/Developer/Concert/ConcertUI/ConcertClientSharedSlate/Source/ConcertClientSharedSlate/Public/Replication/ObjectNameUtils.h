// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/SoftObjectPtr.h"

class FText;

namespace UE::ConcertClientSharedSlate
{
	/** @return The display name of ObjectPath to use for Concert replication UI. */
	CONCERTCLIENTSHAREDSLATE_API FText GetObjectDisplayName(const TSoftObjectPtr<>& ObjectPath);
}

