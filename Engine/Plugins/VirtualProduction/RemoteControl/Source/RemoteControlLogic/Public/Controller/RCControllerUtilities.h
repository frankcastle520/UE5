// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

class FProperty;
struct FPropertyBagPropertyDesc;

namespace UE::RCControllers
{
	/**
	 * Determines whether a controller can be created from a given property desc explicitly
	 * Explicit meaning that it won't use IsChildOf or recurse up its Super Types for Object/Struct properties
	 */
	bool CanCreateControllerFromPropertyDesc(const FPropertyBagPropertyDesc& InPropertyDesc);

	/**
	 * Determines whether a controller can be created from a given property type explicitly
	 * Explicit meaning that it won't use IsChildOf or recurse up its Super Types for Object/Struct properties
	 */
	REMOTECONTROLLOGIC_API bool CanCreateControllerFromProperty(const FProperty* InProperty);
};
