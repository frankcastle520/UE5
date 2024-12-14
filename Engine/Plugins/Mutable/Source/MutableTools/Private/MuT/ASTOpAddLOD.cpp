// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpAddLOD.h"

#include "HAL/PlatformMath.h"
#include "MuR/ModelPrivate.h"
#include "MuR/RefCounted.h"


namespace mu
{


	//-------------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------
	ASTOpAddLOD::ASTOpAddLOD()
	{
	}


	ASTOpAddLOD::~ASTOpAddLOD()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	bool ASTOpAddLOD::IsEqual(const ASTOp& otherUntyped) const
	{
		if (otherUntyped.GetOpType() == GetOpType())
		{
			const ASTOpAddLOD* other = static_cast<const ASTOpAddLOD*>(&otherUntyped);
			return lods == other->lods;
		}
		return false;
	}


	uint64 ASTOpAddLOD::Hash() const
	{
		uint64 res = std::hash<uint64>()(uint64(OP_TYPE::IN_ADDLOD));
		for (auto& c : lods)
		{
			hash_combine(res, c.child().get());
		}
		return res;
	}


	mu::Ptr<ASTOp> ASTOpAddLOD::Clone(MapChildFuncRef mapChild) const
	{
		Ptr<ASTOpAddLOD> n = new ASTOpAddLOD();
		for (auto& c : lods)
		{
			n->lods.Emplace(n, mapChild(c.child()));
		}
		return n;
	}


	void ASTOpAddLOD::ForEachChild(const TFunctionRef<void(ASTChild&)> f)
	{
		for (auto& l : lods)
		{
			f(l);
		}
	}


	void ASTOpAddLOD::Link(FProgram& program, FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			linkedAddress = (OP::ADDRESS)program.m_opAddress.Num();
			program.m_opAddress.Add((uint32)program.m_byteCode.Num());
			AppendCode(program.m_byteCode, OP_TYPE::IN_ADDLOD);

			// Calculate LODCount mimicking previous behaviour.
			// It may be better to leave null LODs if they are null in the AST.
			uint8 LODCount = 0;
			for (const ASTChild& LOD : lods)
			{
				OP::ADDRESS LODAddress = 0;
				if (LOD)
				{
					++LODCount;
					check(LODCount < 255);
					if (LODCount == 255)
					{
						break;
					}
				}
			}

			AppendCode(program.m_byteCode, LODCount);
			for (const ASTChild& LOD : lods)
			{
				OP::ADDRESS LODAddress = 0;
				if (LOD)
				{
					LODAddress = LOD->linkedAddress;
					AppendCode(program.m_byteCode, LODAddress);
					if (LODCount == 255)
					{
						break;
					}
				}
			}
		}

	}

}
