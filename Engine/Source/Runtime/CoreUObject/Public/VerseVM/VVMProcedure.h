// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Templates/TypeCompatibleBytes.h"
#include "VVMBytecode.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VVMType.h"
#include "VVMUniqueString.h"

namespace Verse
{
struct FAbstractVisitor;
/*
This is laid out in memory with (64-bit) pointers followed by (8-byte aligned) instructions followed by (32-bit) integers:
VProcedure
FNamedParam                    NamedParam[0]
FNamedParam                    NamedParam[1]
...
FNamedParam                    NamedParam[NumNamedParameters - 1]
TWriteBarrier<VValue>          Constant  [0]
TWriteBarrier<VValue>          Constant  [1]
...
TWriteBarrier<VValue>          Constant  [NumConstants - 1]
FOp                            Ops
  + NumOpBytes
FValueOperand                  Operand   [0]
FValueOperand                  Operand   [1]
...
FValueOperand                  Operand   [NumOperands - 1]
FLabelOffset                   Label     [0]
FLabelOffset                   Label     [1]
...
FLabelOffset                   Label     [NumLabels - 1]
FUnwindEdge                    UnwindEdge[0]
FUnwindEdge                    UnwindEdge[1]
...
FUnwindEdge                    UnwindEdge[NumUnwindEdges - 1]
FOpLocation                    OpLocation[0]
FOpLocation                    OpLocation[1]
...
FOpLocation                    OpLocation[NumOpLocations - 1]
FRegisterName                  RegisterName[0]
FRegisterName                  RegisterName[1]
...
FRegisterName                  RegisterName[NumRegisterNames - 1]
*/
struct VProcedure : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	// Used by the debugger when checking breakpoints
	TWriteBarrier<VUniqueString> FilePath;
	// Used by the debugger when showing stack frames
	TWriteBarrier<VUniqueString> Name;

	uint32 NumRegisters;
	uint32 NumPositionalParameters;

	// Sizes of trailing arrays
	uint32 NumNamedParameters;
	uint32 NumConstants;
	uint32 NumOpBytes;
	uint32 NumOperands;
	uint32 NumLabels;
	uint32 NumUnwindEdges;
	uint32 NumOpLocations;
	uint32 NumRegisterNames;

	TWriteBarrier<VCell> Trailing[];

	// Trailing array layout computation

	FNamedParam* GetNamedParamsBegin() { return BitCast<FNamedParam*>(&Trailing); }
	FNamedParam* GetNamedParamsEnd() { return GetNamedParamsBegin() + NumNamedParameters; }

	TWriteBarrier<VValue>* GetConstantsBegin() { return BitCast<TWriteBarrier<VValue>*>(GetNamedParamsEnd()); }
	TWriteBarrier<VValue>* GetConstantsEnd() { return GetConstantsBegin() + NumConstants; }

	FOp* GetOpsBegin() { return BitCast<FOp*>(GetConstantsEnd()); }
	FOp* GetOpsEnd() { return BitCast<FOp*>(BitCast<uint8*>(GetOpsBegin()) + NumOpBytes); }

	FValueOperand* GetOperandsBegin() { return BitCast<FValueOperand*>(GetOpsEnd()); }
	FValueOperand* GetOperandsEnd() { return GetOperandsBegin() + NumOperands; }

	FLabelOffset* GetLabelsBegin() { return BitCast<FLabelOffset*>(GetOperandsEnd()); }
	FLabelOffset* GetLabelsEnd() { return GetLabelsBegin() + NumLabels; }

	FUnwindEdge* GetUnwindEdgesBegin() { return BitCast<FUnwindEdge*>(GetLabelsEnd()); }
	FUnwindEdge* GetUnwindEdgesEnd() { return GetUnwindEdgesBegin() + NumUnwindEdges; }

	FOpLocation* GetOpLocationsBegin() { return BitCast<FOpLocation*>(GetUnwindEdgesEnd()); }
	FOpLocation* GetOpLocationsEnd() { return GetOpLocationsBegin() + NumOpLocations; }

	FRegisterName* GetRegisterNamesBegin() { return BitCast<FRegisterName*>(GetOpLocationsEnd()); }
	FRegisterName* GetRegisterNamesEnd() { return GetRegisterNamesBegin() + NumRegisterNames; }

	// In bytes.
	uint32 BytecodeOffset(const FOp& Bytecode)
	{
		return BytecodeOffset(&Bytecode);
	}

	uint32 BytecodeOffset(const void* Data)
	{
		checkSlow(GetOpsBegin() <= Data && Data < GetOpsEnd());
		return static_cast<uint32>(BitCast<char*>(Data) - BitCast<char*>(GetOpsBegin()));
	}

	const FLocation* GetLocation(const FOp& Op)
	{
		return GetLocation(BytecodeOffset(Op));
	}

	const FLocation* GetLocation(int32 OpOffset)
	{
		return Verse::GetLocation(GetOpLocationsBegin(), GetOpLocationsEnd(), OpOffset);
	}

	void SetConstant(FAllocationContext Context, FConstantIndex ConstantIndex, VValue Value)
	{
		checkSlow(ConstantIndex.Index < NumConstants);
		GetConstantsBegin()[ConstantIndex.Index].Set(Context, Value);
	}

	VValue GetConstant(FConstantIndex ConstantIndex)
	{
		checkSlow(ConstantIndex.Index < NumConstants);
		return GetConstantsBegin()[ConstantIndex.Index].Get();
	}

	static VProcedure& NewUninitialized(
		FAllocationContext Context,
		VUniqueString& FilePath,
		VUniqueString& Name,
		uint32 NumRegisters,
		uint32 NumPositionalParameters,
		uint32 NumNamedParameters,
		uint32 NumConstants,
		uint32 NumOpBytes,
		uint32 NumOperands,
		uint32 NumLabels,
		uint32 NumUnwindEdges,
		uint32 NumOpLocations,
		uint32 NumRegisterNames)
	{
		const size_t NumBytes = offsetof(VProcedure, Trailing)
							  + sizeof(FNamedParam) * NumNamedParameters
							  + sizeof(TWriteBarrier<VValue>) * NumConstants
							  + NumOpBytes
							  + sizeof(FValueOperand) * NumOperands
							  + sizeof(FLabelOffset) * NumLabels
							  + sizeof(FUnwindEdge) * NumUnwindEdges
							  + sizeof(FOpLocation) * NumOpLocations
							  + sizeof(FRegisterName) * NumRegisterNames;
		return *new (Context.AllocateFastCell(NumBytes)) VProcedure(
			Context,
			FilePath,
			Name,
			NumRegisters,
			NumPositionalParameters,
			NumNamedParameters,
			NumConstants,
			NumOpBytes,
			NumOperands,
			NumLabels,
			NumUnwindEdges,
			NumOpLocations,
			NumRegisterNames);
	}

	static void SerializeImpl(VProcedure*& This, FAllocationContext Context, FAbstractVisitor& Visitor);

private:
	VProcedure(
		FAllocationContext Context,
		VUniqueString& FilePath,
		VUniqueString& Name,
		uint32 InNumRegisters,
		uint32 InNumPositionalParameters,
		uint32 InNumNamedParameters,
		uint32 InNumConstants,
		uint32 InNumOpBytes,
		uint32 InNumOperands,
		uint32 InNumLabels,
		uint32 InNumUnwindEdges,
		uint32 InNumOpLocations,
		uint32 InNumRegisterNames)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, FilePath(Context, FilePath)
		, Name(Context, Name)
		, NumRegisters(InNumRegisters)
		, NumPositionalParameters(InNumPositionalParameters)
		, NumNamedParameters(InNumNamedParameters)
		, NumConstants(InNumConstants)
		, NumOpBytes(InNumOpBytes)
		, NumOperands(InNumOperands)
		, NumLabels(InNumLabels)
		, NumUnwindEdges(InNumUnwindEdges)
		, NumOpLocations(InNumOpLocations)
		, NumRegisterNames(InNumRegisterNames)
	{
		for (FNamedParam* NamedParam = GetNamedParamsBegin(); NamedParam != GetNamedParamsEnd(); ++NamedParam)
		{
			new (NamedParam) FNamedParam{};
		}
		for (TWriteBarrier<VValue>* Constant = GetConstantsBegin(); Constant != GetConstantsEnd(); ++Constant)
		{
			new (Constant) TWriteBarrier<VValue>{};
		}
		for (FValueOperand* Operand = GetOperandsBegin(); Operand != GetOperandsEnd(); ++Operand)
		{
			new (Operand) FValueOperand{};
		}
		for (FLabelOffset* Label = GetLabelsBegin(); Label != GetLabelsEnd(); ++Label)
		{
			new (Label) FLabelOffset{};
		}
		for (FUnwindEdge* UnwindEdge = GetUnwindEdgesBegin(); UnwindEdge != GetUnwindEdgesEnd(); ++UnwindEdge)
		{
			new (UnwindEdge) FUnwindEdge{};
		}
	}

	template <typename FuncType>
	void ForEachOpCode(FuncType&& Func);

	void LoadOpCodes(FAbstractVisitor& Visitor);
	void SaveOpCodes(FAbstractVisitor& Visitor);
};
} // namespace Verse
#endif // WITH_VERSE_VM
