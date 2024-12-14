// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Operations.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"


namespace mu
{
struct FProgram;


//---------------------------------------------------------------------------------------------
//!
//---------------------------------------------------------------------------------------------
class ASTOpMeshTransformWithBoundingMesh final : public ASTOp
{
public:

	ASTChild source;
	ASTChild boundingMesh;

	ASTChild matrix;
public:

	ASTOpMeshTransformWithBoundingMesh();
	ASTOpMeshTransformWithBoundingMesh(const ASTOpMeshTransformWithBoundingMesh&) = delete;
	~ASTOpMeshTransformWithBoundingMesh() override;

	OP_TYPE GetOpType() const override { return OP_TYPE::ME_TRANSFORMWITHMESH; }
	uint64 Hash() const override;
	bool IsEqual(const ASTOp& otherUntyped) const override;
	Ptr<ASTOp> Clone(MapChildFuncRef mapChild) const override;
	void ForEachChild(const TFunctionRef<void(ASTChild&)>) override;
	void Link(FProgram& program, FLinkerOptions* Options) override;
	FSourceDataDescriptor GetSourceDataDescriptor(FGetSourceDataDescriptorContext*) const override;
};

}

