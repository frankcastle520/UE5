// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

namespace uLang
{

/// Asserts that the active FP state is what is expected (IEEE compliant, round to nearest)
void ULANGCORE_API AssertExpectedFloatingPointState();

/// Sets machine floating point state to problematic values (round to zero, flush-to-zero on).
/// Intended for FP state save/restore tests.
void ULANGCORE_API SetProblematicFloatingPointStateForTesting();

/// Scope guard that saves current FP state (rounding mode, flush-to-zero etc.)
/// and puts us into fully IEEE compliant mode for the duration of the scope.
class CFloatStateSaveRestore
{
public:
    ULANGCORE_API CFloatStateSaveRestore();
    ULANGCORE_API ~CFloatStateSaveRestore();

private:
    // The relevant control register is 32-bit on all current targets.
    uint32_t _SavedState;
};

/// Scope guard that asserts the current FP state has the IEEE-compliant settings
/// we expect on entry, but never changes state. Drop-in compatible with
/// CFloatStateSaveRestore.
class CFloatStateCheckOnly
{
public:
    CFloatStateCheckOnly()
    {
        AssertExpectedFloatingPointState();
    }
};

/// Scope guard that doesn't actually do anything. This is here to make it as easy
/// as possible to swap out either of CFloatStateSaveRestore or CFloatStateCheckOnly
/// for a no-op by changing a single type.
class CFloatStateDoNothing
{
};

}
