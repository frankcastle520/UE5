// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#ifndef GPU_SIMULATION
	#define uint				uint32
	typedef FUintVector3		FNiagaraStatelessBuiltDistributionType;
	typedef const FUintVector3&	FNiagaraStatelessBuiltDistributionTypeIn;
#else
	//typedef uint3			FNiagaraStatelessBuiltDistributionType;
	#define FNiagaraStatelessBuiltDistributionType		uint3
	#define FNiagaraStatelessBuiltDistributionTypeIn	uint3
#endif

#define ENiagaraStatelessBuiltDistributionFlag_Binding			0x20000000u
#define ENiagaraStatelessBuiltDistributionFlag_Random			0x40000000u
#define ENiagaraStatelessBuiltDistributionFlag_Uniform			0x80000000u
#define ENiagaraStatelessBuiltDistributionFlag_TableLengthBits	9u				// 512 entries in LUT
#define ENiagaraStatelessBuiltDistributionFlag_TableLengthShift	20u
#define ENiagaraStatelessBuiltDistributionFlag_TableLengthMask	((1u << ENiagaraStatelessBuiltDistributionFlag_TableLengthBits) - 1u)
#define ENiagaraStatelessBuiltDistributionFlag_DataOffsetBits	20u
#define ENiagaraStatelessBuiltDistributionFlag_DataOffsetShift	0u
#define ENiagaraStatelessBuiltDistributionFlag_DataOffsetMask	((1u << ENiagaraStatelessBuiltDistributionFlag_DataOffsetBits) - 1u)

struct FNiagaraStatelessBuiltDistribution
{
	static bool IsValid(FNiagaraStatelessBuiltDistributionTypeIn BuiltData) { return BuiltData[2] != 0; }
	static bool IsBinding(FNiagaraStatelessBuiltDistributionTypeIn BuiltData) { return (BuiltData[0] & ENiagaraStatelessBuiltDistributionFlag_Binding) != 0; }
	static bool IsRandom(FNiagaraStatelessBuiltDistributionTypeIn BuiltData) { return (BuiltData[0] & ENiagaraStatelessBuiltDistributionFlag_Random) != 0; }
	static bool IsUniform(FNiagaraStatelessBuiltDistributionTypeIn BuiltData) { return (BuiltData[0] & ENiagaraStatelessBuiltDistributionFlag_Uniform) != 0; }

	static uint GetDataOffset(FNiagaraStatelessBuiltDistributionTypeIn BuiltData) { return (BuiltData[0] >> ENiagaraStatelessBuiltDistributionFlag_DataOffsetShift) & ENiagaraStatelessBuiltDistributionFlag_DataOffsetMask; }
	static uint GetTableLength(FNiagaraStatelessBuiltDistributionTypeIn BuiltData) { return (BuiltData[0] >> ENiagaraStatelessBuiltDistributionFlag_TableLengthShift) & ENiagaraStatelessBuiltDistributionFlag_TableLengthMask; }

#ifdef GPU_SIMULATION
	static float ConvertTimeToLookup(FNiagaraStatelessBuiltDistributionTypeIn BuiltData, float Time)
	{
		const float TimeBias	= asfloat(BuiltData[1]);
		const float TimeScale	= asfloat(BuiltData[2]);
		const float TableLength	= float(GetTableLength(BuiltData));
		const float Offset		= clamp((Time - TimeBias) * TimeScale, 0.0f, TableLength);
		return Offset;
	}
#else
	static float ConvertTimeToLookup(FNiagaraStatelessBuiltDistributionTypeIn BuiltData, float Time)
	{
		const float TimeBias	= reinterpret_cast<const float&>(BuiltData[1]);
		const float TimeScale	= reinterpret_cast<const float&>(BuiltData[2]);
		const float TableLength	= float(GetTableLength(BuiltData));
		const float Offset		= FMath::Clamp((Time - TimeBias) * TimeScale, 0.0f, TableLength);
		return Offset;
	}
	
	static FNiagaraStatelessBuiltDistributionType GetDefault() { return FNiagaraStatelessBuiltDistributionType::ZeroValue; }

	static void SetIsBinding(FNiagaraStatelessBuiltDistributionType& BuiltData) { BuiltData[0] |= ENiagaraStatelessBuiltDistributionFlag_Binding; }
	static void SetIsRandom(FNiagaraStatelessBuiltDistributionType& BuiltData) { BuiltData[0] |= ENiagaraStatelessBuiltDistributionFlag_Random; }
	static void SetIsUniform(FNiagaraStatelessBuiltDistributionType& BuiltData) { BuiltData[0] |= ENiagaraStatelessBuiltDistributionFlag_Uniform; }

	static void SetLookupParameters(FNiagaraStatelessBuiltDistributionType& BuiltData, uint DataOffset)
	{
		check(IsBinding(BuiltData));
		check(DataOffset <= ENiagaraStatelessBuiltDistributionFlag_DataOffsetMask);

		BuiltData[0] &= ~(ENiagaraStatelessBuiltDistributionFlag_DataOffsetMask << ENiagaraStatelessBuiltDistributionFlag_DataOffsetShift);
		BuiltData[0] &= ~(ENiagaraStatelessBuiltDistributionFlag_TableLengthMask << ENiagaraStatelessBuiltDistributionFlag_TableLengthShift);
		BuiltData[0] |= DataOffset << ENiagaraStatelessBuiltDistributionFlag_DataOffsetShift;

		reinterpret_cast<float&>(BuiltData[1]) = 0.0f;
		reinterpret_cast<float&>(BuiltData[2]) = 1.0f;
	}

	static void SetLookupParameters(FNiagaraStatelessBuiltDistributionType& BuiltData, uint DataOffset, uint TableLength, const FVector2f& TimeRange)
	{
		const uint TableLengthMinusOne = TableLength - 1;

		check(DataOffset <= ENiagaraStatelessBuiltDistributionFlag_DataOffsetMask);
		check(TableLengthMinusOne <= ENiagaraStatelessBuiltDistributionFlag_TableLengthMask);

		BuiltData[0] &= ~(ENiagaraStatelessBuiltDistributionFlag_DataOffsetMask << ENiagaraStatelessBuiltDistributionFlag_DataOffsetShift);
		BuiltData[0] |= DataOffset << ENiagaraStatelessBuiltDistributionFlag_DataOffsetShift;

		BuiltData[0] &= ~(ENiagaraStatelessBuiltDistributionFlag_TableLengthMask << ENiagaraStatelessBuiltDistributionFlag_TableLengthShift);
		BuiltData[0] |= TableLengthMinusOne << ENiagaraStatelessBuiltDistributionFlag_TableLengthShift;

		const float TimeDuration = TimeRange.Y - TimeRange.X;
		reinterpret_cast<float&>(BuiltData[1]) = TimeRange.X;
		reinterpret_cast<float&>(BuiltData[2]) = TableLengthMinusOne > 0 && TimeDuration > 0.0f ? float(TableLengthMinusOne) / TimeDuration : 0.0f;
	}
#endif
};

#ifndef GPU_SIMULATION
	#undef uint
#endif
