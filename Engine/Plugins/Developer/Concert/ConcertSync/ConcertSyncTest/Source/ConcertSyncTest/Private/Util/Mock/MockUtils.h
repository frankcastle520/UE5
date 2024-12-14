// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AssertionMacros.h"

namespace UE::ConcertSyncTests
{
	// Utility functions used to detect when a non-mocked function is called, so that we can mock it properly when required.
	template<typename T> T NotMocked(T Ret) { check(false); return Ret; }
	template<typename T> T NotMocked()      { check(false); return T(); }
}