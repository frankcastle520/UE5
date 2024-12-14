// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

enum class EAvaTransitionEditorMode : uint8
{
	/** Simplified view of the State Tree. Core features for most Transition Logic Needs */
	Default,

	/** All the features in State Tree */
	Advanced,

	/** Only Parameters shown */
	Parameter,
};
