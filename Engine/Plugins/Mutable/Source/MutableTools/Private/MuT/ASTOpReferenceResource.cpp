// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpReferenceResource.h"

#include "MuT/ASTOpConstantResource.h"
#include "Containers/Array.h"
#include "HAL/PlatformMath.h"
#include "Misc/AssertionMacros.h"
#include "MuR/Types.h"


namespace mu
{

	void ASTOpReferenceResource::ForEachChild(const TFunctionRef<void(ASTChild&)>)
	{
	}


	//-------------------------------------------------------------------------------------------------
	bool ASTOpReferenceResource::IsEqual(const ASTOp& otherUntyped) const
	{
		if (otherUntyped.GetOpType() == GetOpType())
		{
			const ASTOpReferenceResource* other = static_cast<const ASTOpReferenceResource*>(&otherUntyped);
			return type == other->type && ID == other->ID && bForceLoad == other->bForceLoad && ImageDesc == other->ImageDesc;
		}
		return false;
	}


	//-------------------------------------------------------------------------------------------------
	mu::Ptr<ASTOp> ASTOpReferenceResource::Clone(MapChildFuncRef) const
	{
		Ptr<ASTOpReferenceResource> n = new ASTOpReferenceResource();
		n->type = type;
		n->ID = ID;
		n->bForceLoad = bForceLoad;
		n->ImageDesc = ImageDesc;
		return n;
	}


	//-------------------------------------------------------------------------------------------------
	uint64 ASTOpReferenceResource::Hash() const
	{
		uint64 res = std::hash<uint64>()(uint64(type));
		hash_combine(res, ID);
		return res;
	}


	//-------------------------------------------------------------------------------------------------
	void ASTOpReferenceResource::Link(FProgram& program, FLinkerOptions* Options)
	{
		if (!linkedAddress)
		{
			OP::ResourceReferenceArgs Args;
			FMemory::Memset(&Args, 0, sizeof(Args));
			Args.ID = ID;
			Args.ForceLoad = bForceLoad ? 1 : 0;
			Args.ImageDesc = ImageDesc;

			linkedAddress = (OP::ADDRESS)program.m_opAddress.Num();
			program.m_opAddress.Add((uint32)program.m_byteCode.Num());
			AppendCode(program.m_byteCode, type);
			AppendCode(program.m_byteCode, Args);
		}
	}


	//-------------------------------------------------------------------------------------------------
	FImageDesc ASTOpReferenceResource::GetImageDesc(bool, class FGetImageDescContext*) const
	{
		return ImageDesc;
	}


	//-------------------------------------------------------------------------------------------------
	void ASTOpReferenceResource::GetBlockLayoutSize(uint64 BlockId, int32* pBlockX, int32* pBlockY, FBlockLayoutSizeCache*)
	{
		switch (type)
		{

		case OP_TYPE::IM_REFERENCE:
		{
			*pBlockX = 0;
			*pBlockY = 0;
			break;
		}

		default:
			checkf(false, TEXT("Instruction not supported"));
		}
	}


	//-------------------------------------------------------------------------------------------------
	void ASTOpReferenceResource::GetLayoutBlockSize(int32* pBlockX, int32* pBlockY)
	{
		switch (type)
		{

		case OP_TYPE::IM_REFERENCE:
		{
			// We didn't find any layout.
			*pBlockX = 0;
			*pBlockY = 0;
			break;
		}

		default:
			checkf(false, TEXT("Instruction not supported"));
		}
	}


	//-------------------------------------------------------------------------------------------------
	bool ASTOpReferenceResource::GetNonBlackRect(FImageRect&) const
	{
		return false;
	}


	mu::Ptr<ImageSizeExpression> ASTOpReferenceResource::GetImageSizeExpression() const
	{
		Ptr<ImageSizeExpression> pRes = new ImageSizeExpression;
		pRes->type = ImageSizeExpression::ISET_UNKNOWN;

		return pRes;
	}


	FSourceDataDescriptor ASTOpReferenceResource::GetSourceDataDescriptor(FGetSourceDataDescriptorContext*) const
	{
		return SourceDataDescriptor;
	}


}