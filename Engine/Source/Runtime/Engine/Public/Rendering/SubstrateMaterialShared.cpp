// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubstrateMaterialShared.h"


IMPLEMENT_TYPE_LAYOUT(FSubstrateMaterialCompilationOutput);

FSubstrateMaterialCompilationOutput::FSubstrateMaterialCompilationOutput()
	: SubstrateMaterialType(0)
	, SubstrateClosureCount(0)
	, SubstrateUintPerPixel(0)
#if WITH_EDITOR
	, SubstrateMaterialDescription()
	, SharedLocalBasesCount(0)
	, RequestedBytePerPixel(0)
	, PlatformBytePerPixel(0)
	, RequestedClosurePerPixel(0)
	, PlatformClosurePixel(0)
	, bIsThin(0)
	, MaterialType(0)
	, bMaterialOutOfBudgetHasBeenSimplified(0)
	, RootOperatorIndex(0)
#endif
{
#if WITH_EDITOR
	for (uint32 i = 0; i < SUBSTRATE_COMPILATION_OUTPUT_MAX_OPERATOR; ++i)
	{
		Operators[i] = FSubstrateOperator();
	}
#endif
}


IMPLEMENT_TYPE_LAYOUT(FSubstrateOperator);

FSubstrateOperator::FSubstrateOperator()
{
#if WITH_EDITOR
	OperatorType = INDEX_NONE;
	bNodeRequestParameterBlending = false;
	Index = INDEX_NONE;
	ParentIndex = INDEX_NONE;
	LeftIndex = INDEX_NONE;
	RightIndex = INDEX_NONE;
	ThicknessIndex = INDEX_NONE;

	BSDFIndex = INDEX_NONE;
	BSDFType = 0;
	BSDFRegisteredSharedLocalBasis = FSubstrateRegisteredSharedLocalBasis();
	BSDFFeatures = 0;
	SubUsage = SUBSTRATE_OPERATOR_SUBUSAGE_NONE;

	bBSDFWritesEmissive = false;
	bBSDFWritesAmbientOcclusion = false;

	MaxDistanceFromLeaves = 0;
	LayerDepth = 0;
	bIsTop = false;
	bIsBottom = false;
	bUseParameterBlending = false;
	bRootOfParameterBlendingSubTree = false;
	MaterialExpressionGuid = FGuid();
#endif
}

void FSubstrateOperator::CombineFlagsForParameterBlending(FSubstrateOperator& A, FSubstrateOperator& B)
{
#if WITH_EDITOR
	BSDFFeatures = A.BSDFFeatures | B.BSDFFeatures;
#endif
}

void FSubstrateOperator::CopyFlagsForParameterBlending(FSubstrateOperator& A)
{
#if WITH_EDITOR
	BSDFFeatures = A.BSDFFeatures;
#endif
}

bool FSubstrateOperator::IsDiscarded() const
{
#if WITH_EDITOR
	return bUseParameterBlending && !bRootOfParameterBlendingSubTree;
#else
	return true;
#endif
}

IMPLEMENT_TYPE_LAYOUT(FSubstrateRegisteredSharedLocalBasis);

FSubstrateRegisteredSharedLocalBasis::FSubstrateRegisteredSharedLocalBasis()
{
#if WITH_EDITOR
	NormalCodeChunk = INDEX_NONE;
	TangentCodeChunk = INDEX_NONE;
	NormalCodeChunkHash = 0;
	TangentCodeChunkHash = 0;
	GraphSharedLocalBasisIndex = 0;
#endif
}