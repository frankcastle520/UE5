// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include <cstring> // for memcpy().

namespace uLang
{

/**
 * Implements a thread-safe random number stream.
 * Very bad quality in the lower bits. Don't use the modulus (%) operator.
 */
class CRandomStream
{
public:

	/** Default constructor. */
	CRandomStream(int32_t Seed = 0)
		: _Seed(Seed)
	{}

	/**
	 * Initializes this random stream with the specified seed value.
	 * @param Seed The seed value.
	 */
	void Initialize(int32_t Seed)
	{
		_Seed = uint32_t(Seed);
	}

	/** @return A random number >= Min and <= Max */
	int32_t RandRange(int32_t Min, int32_t Max)
	{
		MutateSeed();

		const int64_t Range = int64_t(Max) - int64_t(Min) + 1;
		return Min + uint32_t(Range == 0 ? 0 : (int64_t(_Seed) % Range));
	}
    
	/** @return A random number >= Min and < Max */
	float FRandRange(float Min, float Max)
	{
		MutateSeed();

        uint32_t UnitFractionAsInt = 0x3F800000U | (_Seed >> 9);
		float UnitFraction;
        memcpy(&UnitFraction, &UnitFractionAsInt, sizeof(float));

        return Min + (UnitFraction - 1.0f) * (Max - Min);
	}

    /** @return A random boolean */
    bool RandBool()
    {
        MutateSeed();
        return !!(_Seed & 1);
    }

protected:

	/** Mutates the current seed into the next seed. */
	void MutateSeed()
	{
		_Seed = (_Seed * 196314165U) + 907633515U; 
	}

private:

	// Holds the current seed. This should be an uint32 so that any shift to obtain top bits
	// is a logical shift, rather than an arithmetic shift (which smears down the negative bit).
	uint32_t _Seed;
};

}