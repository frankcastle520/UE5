// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Operations.h"
#include "MuR/ParametersPrivate.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"


namespace mu
{
struct FProgram;

	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class ASTOpMeshClipMorphPlane final : public ASTOp
	{
	public:

		ASTChild source;

		FShape morphShape;
		FShape selectionShape;
		FBoneName vertexSelectionBone;

		EClipVertexSelectionType VertexSelectionType = EClipVertexSelectionType::None;
		EFaceCullStrategy FaceCullStrategy = EFaceCullStrategy::AllVerticesCulled;

		float dist = 0.f, factor = 0.f, vertexSelectionBoneMaxRadius = -1.f;

	public:

		ASTOpMeshClipMorphPlane();
		ASTOpMeshClipMorphPlane(const ASTOpMeshClipMorphPlane&) = delete;
		virtual ~ASTOpMeshClipMorphPlane();

		virtual OP_TYPE GetOpType() const override { return OP_TYPE::ME_CLIPMORPHPLANE; }
		virtual uint64 Hash() const override;
		virtual bool IsEqual(const ASTOp& otherUntyped) const override;
		virtual Ptr<ASTOp> Clone(MapChildFuncRef mapChild) const override;
		virtual void ForEachChild(const TFunctionRef<void(ASTChild&)>) override;
		virtual void Link(FProgram& program, FLinkerOptions* Options) override;
		virtual mu::Ptr<ASTOp> OptimiseSink(const FModelOptimizationOptions&, FOptimizeSinkContext&) const override;
		virtual FSourceDataDescriptor GetSourceDataDescriptor(FGetSourceDataDescriptorContext*) const override;
	};


}

