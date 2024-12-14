// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpMeshTransformWithBoundingMesh.h"

#include "ASTOpImageTransform.h"


mu::ASTOpMeshTransformWithBoundingMesh::ASTOpMeshTransformWithBoundingMesh()
	: source(this)
	, boundingMesh(this)
	, matrix(this)
{
}

mu::ASTOpMeshTransformWithBoundingMesh::~ASTOpMeshTransformWithBoundingMesh()
{
	// Explicit call needed to avoid recursive destruction
	ASTOp::RemoveChildren();
}

uint64 mu::ASTOpMeshTransformWithBoundingMesh::Hash() const
{
	uint64 res = std::hash<OP_TYPE>()(GetOpType());
	hash_combine(res, source.child());
	hash_combine(res, boundingMesh.child());
	hash_combine(res, matrix.child());

	return res;
}

bool mu::ASTOpMeshTransformWithBoundingMesh::IsEqual(const ASTOp& otherUntyped) const
{
	if (GetOpType() == otherUntyped.GetOpType())
	{
		const ASTOpMeshTransformWithBoundingMesh& other = static_cast<const ASTOpMeshTransformWithBoundingMesh&>(otherUntyped);
		return source == other.source && boundingMesh == other.boundingMesh && matrix == other.matrix; 
	}
	return false;
}

mu::Ptr<mu::ASTOp> mu::ASTOpMeshTransformWithBoundingMesh::Clone(MapChildFuncRef mapChild) const
{
	Ptr<ASTOpMeshTransformWithBoundingMesh> n = new ASTOpMeshTransformWithBoundingMesh();
	n->source = mapChild(source.child());
	n->boundingMesh = mapChild(boundingMesh.child());
	n->matrix = mapChild(matrix.child());
	return n;
}

void mu::ASTOpMeshTransformWithBoundingMesh::ForEachChild(const TFunctionRef<void(ASTChild&)> f)
{
	f(source);
	f(boundingMesh);
	f(matrix);
}

void mu::ASTOpMeshTransformWithBoundingMesh::Link(FProgram& program, FLinkerOptions* Options)
{
	if (!linkedAddress)
	{
		OP::MeshTransformWithinMeshArgs args = {};
		if (source)
		{
			args.sourceMesh = source->linkedAddress;
		}
		if (boundingMesh)
		{
			args.boundingMesh = boundingMesh->linkedAddress;
		}
		if (matrix)
		{
			args.matrix = matrix->linkedAddress;
		}
		linkedAddress = static_cast<OP::ADDRESS>(program.m_opAddress.Num());
		program.m_opAddress.Add(program.m_byteCode.Num());
		AppendCode(program.m_byteCode, OP_TYPE::ME_TRANSFORMWITHMESH);
		AppendCode(program.m_byteCode, args);
	}
}

mu::FSourceDataDescriptor mu::ASTOpMeshTransformWithBoundingMesh::GetSourceDataDescriptor(
	FGetSourceDataDescriptorContext* GetSourceDataDescriptorContext) const
{
	return source->GetSourceDataDescriptor(GetSourceDataDescriptorContext);
}
