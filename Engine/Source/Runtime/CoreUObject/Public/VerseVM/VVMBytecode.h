// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Templates/TypeCompatibleBytes.h"
#include "VVMBytecodeOps.h"
#include "VVMLocation.h"
#include "VVMMarkStackVisitor.h"

namespace Verse
{
struct VProcedure;

using FOpcodeInt = uint16_t;

enum class EOpcode : FOpcodeInt
{
#define VISIT_OP(Name) Name,
	VERSE_ENUM_OPS(VISIT_OP)
#undef VISIT_OP
};

COREUOBJECT_API const char* ToString(EOpcode Opcode);

/// This _must_ match up with the codegen in `VerseVMBytecodeGenerator.cs`.
enum class EOperandRole : uint8
{
	Use,
	Immediate,
	ClobberDef,
	UnifyDef,
};

// We align the bytecode stream to 8 bytes so we don't see tearing from the collector,
// and in the future other concurrent threads, when writing to a VValue/pointer sized
// entry.
struct alignas(8) FOp
{
	const EOpcode Opcode;

	explicit FOp(const EOpcode InOpcode)
		: Opcode(InOpcode) {}
};

struct FRegisterIndex
{
	static constexpr uint32 UNINITIALIZED = INT32_MAX;

	/// These are hardcoded register indices that we will always place the operands in by convention.
	static constexpr uint32 SELF = 0;  // for `Self`.
	static constexpr uint32 SCOPE = 1; // for `(super:)` and other generic captures in the future.
	static constexpr uint32 PARAMETER_START = 2;

	// Unsigned, but must be less than INT32_MAX
	uint32 Index;

	friend bool operator==(FRegisterIndex Left, FRegisterIndex Right)
	{
		return Left.Index == Right.Index;
	}

	friend bool operator!=(FRegisterIndex Left, FRegisterIndex Right)
	{
		return Left.Index != Right.Index;
	}
};

template <>
void Visit(FAbstractVisitor&, FRegisterIndex&, const TCHAR* ElementName);

template <>
inline void Visit(FMarkStackVisitor& Visitor, const FRegisterIndex& Value, FMarkStackVisitor::ConsumeElementName ElementName)
{
}

struct FConstantIndex
{
	// Unsigned, but must be less than or equal to INT32_MAX
	uint32 Index;
};

struct FValueOperand
{
	static constexpr uint32 UNINITIALIZED = INT32_MAX;

	uint32 Index{UNINITIALIZED};

	FValueOperand() = default;

	FValueOperand(FRegisterIndex Register)
		: Index(Register.Index)
	{
		check(Register.Index < UNINITIALIZED);
		check(IsRegister());
	}
	FValueOperand(FConstantIndex Constant)
		: Index{~Constant.Index}
	{
		check(Constant.Index <= UNINITIALIZED);
		check(IsConstant());
	}

	bool IsRegister() const { return Index < UNINITIALIZED; }
	bool IsConstant() const { return UNINITIALIZED < Index; }

	FRegisterIndex AsRegister() const
	{
		checkSlow(IsRegister());
		return FRegisterIndex{Index};
	}
	FConstantIndex AsConstant() const
	{
		checkSlow(IsConstant());
		return FConstantIndex{~Index};
	}
};

struct FLabelOffset
{
	int32 Offset; // In bytes, relative to the address of this FLabelOffset

	FOp* GetLabeledPC() const
	{
		return const_cast<FOp*>(BitCast<const FOp*>(BitCast<const uint8*>(this) + Offset));
	}
};

template <typename OperandType>
struct TOperandRange
{
	int32 Index;
	int32 Num;
};

// A range of opcode bytes, with a target label for unwinding from calls within that range.
// VProcedure holds a sorted array of non-overlapping unwind edges.
struct FUnwindEdge
{
	int32 Begin;
	int32 End;
	FLabelOffset OnUnwind;
};

// Mapping from an opcode offset to a location.  VProcedure holds a sorted array of such
// mappings where an op's location is the latest entry with an equal or lesser offset.
struct FOpLocation
{
	int32 Begin;
	FLocation Location;
};

const FLocation* GetLocation(FOpLocation* First, FOpLocation* Last, uint32 OpOffset);

template <>
void Visit(FAbstractVisitor&, FOpLocation&, const TCHAR* ElementName);

template <>
inline void Visit(FMarkStackVisitor&, const FOpLocation&, FMarkStackVisitor::ConsumeElementName)
{
}

// Mapping of a named parameter to its corresponding register. VProcedures hold an array of such mappings.
struct FNamedParam
{
	FNamedParam() = default;
	FNamedParam(FRegisterIndex InIndex, FAccessContext InContext, VUniqueString& InName)
		: Index(InIndex)
		, Name(InContext, InName)
	{
	}

	FRegisterIndex Index;
	TWriteBarrier<VUniqueString> Name;
};

template <>
void Visit(FAbstractVisitor&, FNamedParam&, const TCHAR* ElementName);

template <>
inline void Visit(FMarkStackVisitor& Visitor, const FNamedParam& Value, FMarkStackVisitor::ConsumeElementName)
{
	Visit(Visitor, Value.Name, TEXT(""));
}

// Mapping from register index to name. VProcedures hold an array of such mappings.
struct FRegisterName
{
	FRegisterName(FRegisterIndex InIndex, FAccessContext InContext, VUniqueString& InName)
		: Index(InIndex)
		, Name(InContext, InName)
	{
	}

	FRegisterIndex Index;
	TWriteBarrier<VUniqueString> Name;
};

template <>
void Visit(FAbstractVisitor&, FRegisterName&, const TCHAR* ElementName);

template <>
inline void Visit(FMarkStackVisitor& Visitor, const FRegisterName& Value, FMarkStackVisitor::ConsumeElementName)
{
	Visit(Visitor, Value.Name, TEXT(""));
}
} // namespace Verse
#endif // WITH_VERSE_VM
