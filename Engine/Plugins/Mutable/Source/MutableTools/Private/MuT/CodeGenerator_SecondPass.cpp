// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/CodeGenerator_SecondPass.h"

#include "Misc/AssertionMacros.h"
#include "MuR/MutableTrace.h"
#include "MuR/Operations.h"
#include "MuR/Types.h"
#include "MuT/ASTOpConstantBool.h"
#include "MuT/ErrorLog.h"
#include "MuT/CodeGenerator_FirstPass.h"

namespace mu
{


	//---------------------------------------------------------------------------------------------
	SecondPassGenerator::SecondPassGenerator(
		FirstPassGenerator* firstPass,
		const CompilerOptions::Private* options)
	{
		check(firstPass);
		check(options);
		FirstPass = firstPass;
		CompilerOptions = options;
	}


	//---------------------------------------------------------------------------------------------
	mu::Ptr<ASTOp> SecondPassGenerator::GenerateTagCondition(size_t tagIndex,
		const set<size_t>& posSurf,
		const set<size_t>& negSurf,
		const set<size_t>& posTag,
		const set<size_t>& negTag)
	{
		FirstPassGenerator::FTag& t = FirstPass->Tags[tagIndex];

		// If this tag is already in the list of positive tags, return true as condition
		if (posTag.find(tagIndex) != posTag.end())
		{
			return m_opPool.Add(new ASTOpConstantBool(true));
		}

		// If this tag is already in the list of negative tags, return false as condition
		if (negTag.find(tagIndex) != negTag.end())
		{
			return m_opPool.Add(new ASTOpConstantBool(false));
		}

		// Cached?
		FConditionGenerationKey key;
		key.tagOrSurfIndex = tagIndex;
		//    key.negSurf = negSurf;
		//    key.posSurf = posSurf;
		//    key.negTag = negTag;
		//    key.posTag = posTag;
		for (auto s : negTag) { if (TagsPerTag[tagIndex].count(s) > 0) { key.negTag.insert(s); } }
		for (auto s : posTag) { if (TagsPerTag[tagIndex].count(s) > 0) { key.posTag.insert(s); } }
		for (auto s : negSurf) { if (SurfacesPerTag[tagIndex].count(s) > 0) { key.negSurf.insert(s); } }
		for (auto s : posSurf) { if (SurfacesPerTag[tagIndex].count(s) > 0) { key.posSurf.insert(s); } }

		{
			auto it = TagConditionGenerationCache.find(key);
			if (it != TagConditionGenerationCache.end())
			{
				return it->second;
			}
		}
		
		Ptr<ASTOp> c;

		// Condition expression for all the surfaces that activate the tag
		for (int32 surfIndex : t.Surfaces)
		{
			if (posSurf.find(surfIndex) != posSurf.end())
			{
				// This surface is already a positive requirement higher up in the condition so
				// we can ignore it here.
				continue;
			}

			if (negSurf.find(surfIndex) != negSurf.end())
			{
				// This surface is a negative requirement higher up in the condition so
				// this branch never be true.
				continue;
			}

			const FirstPassGenerator::FSurface& surface = FirstPass->Surfaces[surfIndex];

			auto PositiveTags = posTag;
			PositiveTags.insert(tagIndex);

			Ptr<ASTOp> surfCondition = GenerateDataCodition(surfIndex,
				FirstPass->Surfaces[surfIndex].PositiveTags,
				FirstPass->Surfaces[surfIndex].NegativeTags,
				posSurf,
				negSurf,
				PositiveTags,
				negTag);

			// If the surface is a constant false, we can skip adding it
			if (surfCondition && surfCondition->GetOpType()==OP_TYPE::BO_CONSTANT)
			{
				const ASTOpConstantBool* constOp = static_cast<const ASTOpConstantBool*>(surfCondition.get());
				if (constOp->value == false)
				{
					continue;
				}
			}

			Ptr<ASTOp> fullCondition;
			if (surfCondition)
			{
				Ptr<ASTOpFixed> f = new ASTOpFixed();
				f->op.type = OP_TYPE::BO_AND;
				f->SetChild(f->op.args.BoolBinary.a, surface.ObjectCondition);
				f->SetChild(f->op.args.BoolBinary.b, surfCondition);
				fullCondition = m_opPool.Add(f);
			}
			else
			{
				fullCondition = m_opPool.Add(surface.ObjectCondition);
			}


			if (!c)
			{
				c = fullCondition;
			}
			else
			{
				Ptr<ASTOpFixed> o = new ASTOpFixed();
				o->op.type = OP_TYPE::BO_OR;
				o->SetChild(o->op.args.BoolBinary.a, fullCondition);
				o->SetChild(o->op.args.BoolBinary.b, c);
				c = m_opPool.Add(o);
			}

			// Optimise the condition now.
			//PartialOptimise( c, CompilerOptions->OptimisationOptions );
		}

		TagConditionGenerationCache.insert(std::make_pair<>(key, c));

		return c;
	}

	//---------------------------------------------------------------------------------------------
	mu::Ptr<ASTOp> SecondPassGenerator::GenerateDataCodition(size_t Index,
		const TArray<FString>& PositiveTags,
		const TArray<FString>& NegativeTags,
		const set<size_t>& posSurf,
		const set<size_t>& negSurf,
		const set<size_t>& posTag,
		const set<size_t>& negTag)
	{
		// If this surface is already in the list of positive surfaces, return true as condition
		if (posSurf.find(Index) != posSurf.end())
		{
			return m_opPool.Add(new ASTOpConstantBool(true));
		}

		// If this surface is already in the list of negative surfaces, return false as condition
		if (negSurf.find(Index) != negSurf.end())
		{
			return m_opPool.Add(new ASTOpConstantBool(false));
		}

		Ptr<ASTOp> c;

		for (const FString& t : PositiveTags)
		{
			const FirstPassGenerator::FTag* it = FirstPass->Tags.FindByPredicate([&](const FirstPassGenerator::FTag& e) { return e.Tag == t; });
			if (!it)
			{
				// This could happen if a tag is in a variation but noone defines it.
				// This surface depends on a tag that will never be active, so it will never be used.
				return m_opPool.Add(new ASTOpConstantBool(false));
			}

			size_t tagIndex = it - &FirstPass->Tags[0];

			set<size_t> positiveSurfacesVisited = posSurf;
			positiveSurfacesVisited.insert(Index);

			Ptr<ASTOp> tagCondition = GenerateTagCondition(tagIndex,
				positiveSurfacesVisited,
				negSurf,
				posTag,
				negTag);

			if (!tagCondition)
			{
				// This tag is unconditionally activated, so there's no condition logic to add
				continue;
			}

			// TODO: Optimise the tag condition here

			// If the tag is a constant ...
			bool isConstant = false;
			bool constantValue = false;
			if (tagCondition->GetOpType()==OP_TYPE::BO_CONSTANT)
			{
				const ASTOpConstantBool* constOp = static_cast<const ASTOpConstantBool*>(tagCondition.get());
				isConstant = true;
				constantValue = constOp->value;
			}

			if (!isConstant)
			{
				if (!c)
				{
					c = tagCondition;
				}
				else
				{
					Ptr<ASTOpFixed> o = new ASTOpFixed;
					o->op.type = OP_TYPE::BO_AND;
					o->SetChild(o->op.args.BoolBinary.a, tagCondition);
					o->SetChild(o->op.args.BoolBinary.b, c);
					c = m_opPool.Add(o);
				}
			}
			else if (constantValue == true)
			{
				// No need to add it to the AND
			}
			else //if (constantValue==false)
			{
				// Entire expression will be false
				Ptr<ASTOpConstantBool> f = new ASTOpConstantBool(false);

				// No need to evaluate anything else.
				c = m_opPool.Add(f);
				break;
			}
		}


		for (const FString& t : NegativeTags)
		{
			const FirstPassGenerator::FTag* it = FirstPass->Tags.FindByPredicate([&](const FirstPassGenerator::FTag& e) { return e.Tag == t; });
			if (!it)
			{
				// This could happen if a tag is in a variation but noone defines it.
				continue;
			}

			size_t tagIndex = it - FirstPass->Tags.GetData();

			set<size_t> positiveSurfacesVisited = negSurf;
			set<size_t> negativeSurfacesVisited = posSurf;
			negativeSurfacesVisited.insert(Index);
			set<size_t> positiveTagsVisited = negTag;
			set<size_t> negativeTagsVisited = posTag;
			Ptr<ASTOp> tagCondition = GenerateTagCondition(tagIndex,
				positiveSurfacesVisited,
				negativeSurfacesVisited,
				positiveTagsVisited,
				negativeTagsVisited);

			// No condition is equal to a conditional with a true constant
			if (!tagCondition)
			{
				ASTOpConstantBool* ConstOp = new ASTOpConstantBool();
				ConstOp->value = true;
				tagCondition = ConstOp;
			}

			// TODO: Optimise the tag condition here

			// If the tag is a constant ...
			bool isConstant = false;
			bool constantValue = false;
			if (tagCondition && tagCondition->GetOpType()== OP_TYPE::BO_CONSTANT)
			{
				const ASTOpConstantBool* constOp = static_cast<const ASTOpConstantBool*>(tagCondition.get());
				isConstant = true;
				constantValue = constOp->value;
			}


			if (!isConstant && tagCondition)
			{
				Ptr<ASTOpFixed> n = new ASTOpFixed;
				n->op.type = OP_TYPE::BO_NOT;
				n->SetChild(n->op.args.BoolNot.source, tagCondition);

				if (!c)
				{
					c = m_opPool.Add(n);
				}
				else
				{
					Ptr<ASTOpFixed> o = new ASTOpFixed;
					o->op.type = OP_TYPE::BO_AND;
					o->SetChild(o->op.args.BoolBinary.a, n);
					o->SetChild(o->op.args.BoolBinary.b, c);
					c = m_opPool.Add(o);
				}
			}
			else if (isConstant && constantValue == true)
			{
				// No expression here means always true which becomes always false
				Ptr<ASTOpConstantBool> f = new ASTOpConstantBool(false);

				// No need to evaluate anything else.
				c = m_opPool.Add(f);
				break;
			}
		}

		return c;
	}


	//---------------------------------------------------------------------------------------------
	bool SecondPassGenerator::Generate( Ptr<mu::ErrorLog> InErrorLog, const Node* root)
	{
		MUTABLE_CPUPROFILER_SCOPE(SecondPassGenerate);

		check(root);
		ErrorLog = InErrorLog;

		// Find the list of surfaces every tag depends on
		SurfacesPerTag.Empty();
		SurfacesPerTag.SetNum(FirstPass->Tags.Num());
		TagsPerTag.Empty();
		TagsPerTag.SetNum(FirstPass->Tags.Num());
		for (size_t t = 0; t < FirstPass->Tags.Num(); ++t)
		{
			set<size_t> pendingSurfs;
			for (size_t s : FirstPass->Tags[t].Surfaces)
			{
				pendingSurfs.insert(s);
			}

			set<size_t> processedSurfs;
			while (!pendingSurfs.empty())
			{
				size_t cs = *pendingSurfs.begin();
				pendingSurfs.erase(pendingSurfs.begin());

				if (std::find(processedSurfs.begin(), processedSurfs.end(), cs)
					!= processedSurfs.end())
				{
					continue;
				}

				processedSurfs.insert(cs);

				SurfacesPerTag[t].insert(cs);

				auto& csurf = FirstPass->Surfaces[cs];
				for (const FString& sct : csurf.PositiveTags)
				{
					const FirstPassGenerator::FTag* it = FirstPass->Tags.FindByPredicate([&](const FirstPassGenerator::FTag& e) { return e.Tag == sct; });
					if (!it)
					{
						// This could happen if a tag is in a variation but noone defines it.
						continue;
					}

					size_t ct = it - &FirstPass->Tags[0];

					TagsPerTag[t].insert(ct);

					for (size_t s : FirstPass->Tags[ct].Surfaces)
					{
						if (SurfacesPerTag[t].find(s) != SurfacesPerTag[t].end())
						{
							pendingSurfs.insert(s);
						}
					}
				}
				for (const FString& sct : csurf.NegativeTags)
				{
					const FirstPassGenerator::FTag* it = FirstPass->Tags.FindByPredicate([&](const FirstPassGenerator::FTag& e) { return e.Tag == sct; });
					if (!it)
					{
						// This could happen if a tag is in a variation but noone defines it.
						continue;
					}

					size_t ct = it - &FirstPass->Tags[0];

					TagsPerTag[t].insert(ct);

					for (size_t s : FirstPass->Tags[ct].Surfaces)
					{
						if (SurfacesPerTag[t].find(s) != SurfacesPerTag[t].end())
						{
							pendingSurfs.insert(s);
						}
					}
				}
			}
		}

		// Create the conditions for every surface, modifier, component and individual tag.
		TagConditionGenerationCache.clear();

		set<size_t> Empty;

		for (int32 SurfaceIndex = 0; SurfaceIndex < FirstPass->Surfaces.Num(); ++SurfaceIndex)
		{
			FirstPassGenerator::FSurface& Surface = FirstPass->Surfaces[SurfaceIndex];

			{
				Ptr<ASTOp> c = GenerateDataCodition(
					SurfaceIndex, 
					Surface.PositiveTags,
					Surface.NegativeTags,
					Empty, Empty, Empty, Empty);

				Ptr<ASTOpFixed> ConditionOp = new ASTOpFixed();
				ConditionOp->op.type = OP_TYPE::BO_AND;
				ConditionOp->SetChild(ConditionOp->op.args.BoolBinary.a, Surface.ObjectCondition);
				ConditionOp->SetChild(ConditionOp->op.args.BoolBinary.b, c);

				Surface.FinalCondition = ConditionOp;
			}
			
			// TODO: Convert to modifiers that enable tags?
			//for (int32 EditIndex = 0; EditIndex < Surface.Edits.Num(); ++EditIndex)
			//{
			//	FirstPassGenerator::FSurface::FEdit& Edit = Surface.Edits[EditIndex];
			//	
			//	Ptr<ASTOp> c = GenerateDataCodition(
			//		EditIndex, 
			//		Surface.Edits[EditIndex].PositiveTags,
			//		Surface.Edits[EditIndex].NegativeTags,
			//		Empty, Empty, Empty, Empty);

			//	Ptr<ASTOpFixed> OpAnd = new ASTOpFixed;
			//	OpAnd->op.type = OP_TYPE::BO_AND;
			//	OpAnd->SetChild(OpAnd->op.args.BoolBinary.a, Edit.Condition);
			//	OpAnd->SetChild(OpAnd->op.args.BoolBinary.b, c);
			//	c = m_opPool.Add(OpAnd);
			//	
			//	Edit.Condition = OpAnd;
			//}
		}

		for (int32 ModifierIndex = 0; ModifierIndex < FirstPass->Modifiers.Num(); ++ModifierIndex)
		{
			Ptr<ASTOp> c = GenerateDataCodition(
				ModifierIndex, 
				FirstPass->Modifiers[ModifierIndex].PositiveTags,
				FirstPass->Modifiers[ModifierIndex].NegativeTags,
				Empty, Empty, Empty, Empty);
			
			Ptr<ASTOpFixed> ConditionOp = new ASTOpFixed();
			ConditionOp->op.type = OP_TYPE::BO_AND;
			ConditionOp->SetChild(ConditionOp->op.args.BoolBinary.a, FirstPass->Modifiers[ModifierIndex].ObjectCondition);
			ConditionOp->SetChild(ConditionOp->op.args.BoolBinary.b, c);

			FirstPass->Modifiers[ModifierIndex].FinalCondition = ConditionOp;
		}

		for (int32 ComponentIndex = 0; ComponentIndex < FirstPass->Components.Num(); ++ComponentIndex)
		{
			Ptr<ASTOp> c = GenerateDataCodition(
				ComponentIndex, 
				FirstPass->Components[ComponentIndex].PositiveTags,
				FirstPass->Components[ComponentIndex].NegativeTags,
				Empty, Empty, Empty, Empty);

			FirstPass->Components[ComponentIndex].ComponentCondition = c;
		}

		// TODO: Do we really need the tag conditions from here on?
		for (int32 s = 0; s < FirstPass->Tags.Num(); ++s)
		{
			Ptr<ASTOp> c = GenerateTagCondition(s, Empty, Empty, Empty, Empty);
			FirstPass->Tags[s].GenericCondition = c;
		}

		FirstPass = nullptr;

		return true;
	}


}
