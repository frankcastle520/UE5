// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRTypes.h"

#if WITH_EDITOR

namespace MIR = UE::MIR;

FMaterialIRModule::FMaterialIRModule()
{
	RootBlock = new MIR::FBlock;
}

FMaterialIRModule::~FMaterialIRModule()
{
	Empty();
	delete RootBlock;
}

void FMaterialIRModule::Empty()
{
	RootBlock->Instructions = nullptr;

	for (MIR::FValue* Value : Values)
	{
		FMemory::Free(Value);
	}

	Values.Empty();
	Outputs.Empty();

	// Allocator.~FMemStackBase();
	// new (&Allocator) FMemStackBase;

	// Reset module statistics.
	for (int i = 0; i < (int)SF_NumFrequencies; ++i)
	{
		Statistics.ExternalInputUsedMask[i].Init(false, (int)MIR::EExternalInput::Count);
	}

	Statistics.NumVertexTexCoords = 0;
	Statistics.NumPixelTexCoords = 0;
}

#endif // #if WITH_EDITOR
