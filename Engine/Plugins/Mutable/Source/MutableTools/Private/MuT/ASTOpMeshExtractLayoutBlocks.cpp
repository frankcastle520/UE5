// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpMeshExtractLayoutBlocks.h"

#include "MuT/ASTOpSwitch.h"
#include "MuT/ASTOpConditional.h"
#include "MuT/ASTOpMeshAddTags.h"
#include "MuT/ASTOpMeshMorph.h"
#include "MuT/ASTOpMeshRemoveMask.h"
#include "MuT/ASTOpMeshClipMorphPlane.h"
#include "MuT/ASTOpMeshApplyPose.h"
#include "MuR/ModelPrivate.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"
#include "Misc/AssertionMacros.h"

namespace mu
{

	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	ASTOpMeshExtractLayoutBlocks::ASTOpMeshExtractLayoutBlocks()
		: Source(this)
	{
	}


	ASTOpMeshExtractLayoutBlocks::~ASTOpMeshExtractLayoutBlocks()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	bool ASTOpMeshExtractLayoutBlocks::IsEqual(const ASTOp& otherUntyped) const
	{
		if (otherUntyped.GetOpType() == GetOpType())
		{
			const ASTOpMeshExtractLayoutBlocks* other = static_cast<const ASTOpMeshExtractLayoutBlocks*>(&otherUntyped);
			return Source == other->Source && LayoutIndex == other->LayoutIndex && Blocks == other->Blocks;
		}
		return false;
	}


	mu::Ptr<ASTOp> ASTOpMeshExtractLayoutBlocks::Clone(MapChildFuncRef mapChild) const
	{
		Ptr<ASTOpMeshExtractLayoutBlocks> n = new ASTOpMeshExtractLayoutBlocks();
		n->Source = mapChild(Source.child());
		n->LayoutIndex = LayoutIndex;
		n->Blocks = Blocks;
		return n;
	}


	void ASTOpMeshExtractLayoutBlocks::Assert()
	{
		check(Blocks.Num() < std::numeric_limits<uint16>::max());
		ASTOp::Assert();
	}


	void ASTOpMeshExtractLayoutBlocks::ForEachChild(const TFunctionRef<void(ASTChild&)> f)
	{
		f(Source);
	}


	uint64 ASTOpMeshExtractLayoutBlocks::Hash() const
	{
		uint64 res = std::hash<size_t>()(size_t(Source.child().get()));
		return res;
	}


	void ASTOpMeshExtractLayoutBlocks::Link(FProgram& program, FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			linkedAddress = (OP::ADDRESS)program.m_opAddress.Num();

			program.m_opAddress.Add((uint32)program.m_byteCode.Num());
			AppendCode(program.m_byteCode, OP_TYPE::ME_EXTRACTLAYOUTBLOCK);
			OP::ADDRESS sourceAt = Source ? Source->linkedAddress : 0;
			AppendCode(program.m_byteCode, sourceAt);
			AppendCode(program.m_byteCode, (uint16)LayoutIndex);
			AppendCode(program.m_byteCode, (uint16)Blocks.Num());

			for (uint64 Id : Blocks)
			{
				AppendCode(program.m_byteCode, Id);
			}
		}
	}

	 
	mu::Ptr<ASTOp> ASTOpMeshExtractLayoutBlocks::OptimiseSink(const FModelOptimizationOptions&, FOptimizeSinkContext& Context) const
	{
		mu::Ptr<ASTOp> NewOp = Context.MeshExtractLayoutBlocksSinker.Apply(this);
		return NewOp;
	}


	FSourceDataDescriptor ASTOpMeshExtractLayoutBlocks::GetSourceDataDescriptor(FGetSourceDataDescriptorContext* Context) const
	{
		if (Source)
		{
			return Source->GetSourceDataDescriptor(Context);
		}

		return {};
	}


	//---------------------------------------------------------------------------------------------
	mu::Ptr<ASTOp> Sink_MeshExtractLayoutBlocksAST::Apply(const ASTOpMeshExtractLayoutBlocks* root)
	{
		m_root = root;

		OldToNew.Reset();

		m_initialSource = m_root->Source.child();
		mu::Ptr<ASTOp> newSource = Visit(m_initialSource, m_root);

		// If there is any change, it is the new root.
		if (newSource != m_initialSource)
		{
			return newSource;
		}

		return nullptr;
	}

	mu::Ptr<ASTOp> Sink_MeshExtractLayoutBlocksAST::Visit(const mu::Ptr<ASTOp>& at, const ASTOpMeshExtractLayoutBlocks* currentSinkOp)
	{
		if (!at) return nullptr;

		// Already visited?
		const Ptr<ASTOp>* Cached = OldToNew.Find({ at, currentSinkOp });
		if (Cached)
		{
			return *Cached;
		}

		mu::Ptr<ASTOp> newAt = at;
		switch (at->GetOpType())
		{

		case OP_TYPE::ME_APPLYLAYOUT:
		{
			Ptr<ASTOpFixed> newOp = mu::Clone<ASTOpFixed>(at);
			newOp->SetChild(newOp->op.args.MeshApplyLayout.mesh, Visit(newOp->children[newOp->op.args.MeshApplyLayout.mesh].child(), currentSinkOp));
			newAt = newOp;
			break;
		}

		case OP_TYPE::ME_SETSKELETON:
		{
			Ptr<ASTOpFixed> newOp = mu::Clone<ASTOpFixed>(at);
			newOp->SetChild(newOp->op.args.MeshSetSkeleton.source, Visit(newOp->children[newOp->op.args.MeshSetSkeleton.source].child(), currentSinkOp));
			newAt = newOp;
			break;
		}

		case OP_TYPE::ME_ADDTAGS:
		{
			Ptr<ASTOpMeshAddTags> newOp = mu::Clone<ASTOpMeshAddTags>(at);
			newOp->Source = Visit(newOp->Source.child(), currentSinkOp);
			newAt = newOp;
			break;
		}

		case OP_TYPE::ME_CLIPMORPHPLANE:
		{
			Ptr<ASTOpMeshClipMorphPlane> newOp = mu::Clone<ASTOpMeshClipMorphPlane>(at);
			newOp->source = Visit(newOp->source.child(), currentSinkOp);
			newAt = newOp;
			break;
		}

		case OP_TYPE::ME_MORPH:
		{
			Ptr<ASTOpMeshMorph> NewOp = mu::Clone<ASTOpMeshMorph>(at);
			NewOp->Base = Visit(NewOp->Base.child(), currentSinkOp);
			NewOp->Target = Visit(NewOp->Target.child(), currentSinkOp);
			newAt = NewOp;
			break;
		}

		case OP_TYPE::ME_MERGE:
		{
			Ptr<ASTOpFixed> newOp = mu::Clone<ASTOpFixed>(at);
			newOp->SetChild(newOp->op.args.MeshMerge.base, Visit(newOp->children[newOp->op.args.MeshMerge.base].child(), currentSinkOp));
			newOp->SetChild(newOp->op.args.MeshMerge.added, Visit(newOp->children[newOp->op.args.MeshMerge.added].child(), currentSinkOp));
			newAt = newOp;
			break;
		}

		case OP_TYPE::ME_APPLYPOSE:
		{
			Ptr<ASTOpMeshApplyPose> NewOp = mu::Clone<ASTOpMeshApplyPose>(at);
			NewOp->base = Visit(NewOp->base.child(), currentSinkOp);
			newAt = NewOp;
			break;
		}

		case OP_TYPE::ME_INTERPOLATE:
		{
			Ptr<ASTOpFixed> newOp = mu::Clone<ASTOpFixed>(at);
			newOp->SetChild(newOp->op.args.MeshInterpolate.base, Visit(newOp->children[newOp->op.args.MeshInterpolate.base].child(), currentSinkOp));

			for (int32 t = 0; t < MUTABLE_OP_MAX_INTERPOLATE_COUNT - 1; ++t)
			{
				if (newOp->children[newOp->op.args.MeshInterpolate.targets[t]])
				{
					newOp->SetChild(newOp->op.args.MeshInterpolate.targets[t], Visit(newOp->children[newOp->op.args.MeshInterpolate.targets[t]].child(), currentSinkOp));
				}
			}

			newAt = newOp;
			break;
		}

		case OP_TYPE::ME_REMOVEMASK:
		{
			// TODO: Make mask smaller?
			Ptr<ASTOpMeshRemoveMask> newOp = mu::Clone<ASTOpMeshRemoveMask>(at);
			newOp->source = Visit(newOp->source.child(), currentSinkOp);
			newAt = newOp;
			break;
		}

		case OP_TYPE::ME_CONDITIONAL:
		{
			Ptr<ASTOpConditional> newOp = mu::Clone<ASTOpConditional>(at);
			newOp->yes = Visit(newOp->yes.child(), currentSinkOp);
			newOp->no = Visit(newOp->no.child(), currentSinkOp);
			newAt = newOp;
			break;
		}

		case OP_TYPE::ME_SWITCH:
		{
			Ptr<ASTOpSwitch> newOp = mu::Clone<ASTOpSwitch>(at);
			newOp->def = Visit(newOp->def.child(), currentSinkOp);
			for (ASTOpSwitch::FCase& c : newOp->cases)
			{
				c.branch = Visit(c.branch.child(), currentSinkOp);
			}
			newAt = newOp;
			break;
		}

		// If we reach here it means the operation type has not bee optimized.
		default:
			if (at != m_initialSource)
			{
				mu::Ptr<ASTOpMeshExtractLayoutBlocks> newOp = mu::Clone<ASTOpMeshExtractLayoutBlocks>(currentSinkOp);
				newOp->Source = at;
				newAt = newOp;
			}
			break;

		}

		OldToNew.Add({ at, currentSinkOp }, newAt);

		return newAt;
	}


}
