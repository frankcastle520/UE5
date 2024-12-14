// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RCFieldGroupType.generated.h"

/** Types of remote control field groups */
UENUM()
enum class ERCFieldGroupType : uint8
{
	/** No Grouping. Will be shown as a flat list */
	None,

	/** Fields with same property id are grouped together */
	PropertyId,

	/** Fields with same owner are grouped together */
	Owner,
};

ENUM_CLASS_FLAGS(ERCFieldGroupType)

