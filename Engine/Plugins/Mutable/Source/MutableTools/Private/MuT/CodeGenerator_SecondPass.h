// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/PlatformCrt.h"
#include "MuR/MutableMemory.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/AST.h"
#include "MuT/CodeGenerator_FirstPass.h"
#include "MuT/Compiler.h"
#include "MuT/Node.h"
#include "map"
#include "set"


namespace mu
{

	/** Second pass of the code generation process.
     * It solves surface and modifier conditions from tags and variations
	 */
    class SecondPassGenerator
	{
	public:

		SecondPassGenerator( FirstPassGenerator*, const CompilerOptions::Private*  );

		// Return true on success.
        bool Generate( Ptr<ErrorLog>, const Node* Root );

	private:

        FirstPassGenerator* FirstPass = nullptr;
        const CompilerOptions::Private *CompilerOptions = nullptr;


        //!
        Ptr<ErrorLog> ErrorLog;

        //!
        struct FConditionGenerationKey
        {
            size_t tagOrSurfIndex = 0;
            set<size_t> posSurf;
            set<size_t> negSurf;
            set<size_t> posTag;
            set<size_t> negTag;

            inline bool operator<(const FConditionGenerationKey& o) const
            {
                if (tagOrSurfIndex<o.tagOrSurfIndex) return true;
                if (tagOrSurfIndex>o.tagOrSurfIndex) return false;
                if (posSurf<o.posSurf) return true;
                if (posSurf>o.posSurf) return false;
                if (negSurf<o.negSurf) return true;
                if (negSurf>o.negSurf) return false;
                if (posTag<o.posTag) return true;
                if (posTag>o.posTag) return false;
                if (negTag<o.negTag) return true;
                if (negTag>o.negTag) return false;
                return false;
            }
        };

        // List of surfaces that activate or deactivate every tag, or another surface that activates a tag in this set.
		// \TODO: Change to UE containers
		TArray< set<size_t> > SurfacesPerTag;
		TArray< set<size_t> > TagsPerTag;

        std::map<FConditionGenerationKey,Ptr<ASTOp>> TagConditionGenerationCache;

        UniqueOpPool m_opPool;

        //!
        Ptr<ASTOp> GenerateTagCondition( size_t tagIndex,
                                         const set<size_t>& posSurf,
                                         const set<size_t>& negSurf,
                                         const set<size_t>& posTag,
                                         const set<size_t>& negTag );

        /** Generate Surface, Edit or Modifier condition.
    	 * @param Index Surface, Edit, Component or Modifier index.
    	 * @param PositiveTags function that given the Surface, Edit or Modifier index, returns its positive tags.
    	 * @param NegativeTags function that given the Surface, Edit or Modifier, returns its negative tags.
    	 * @param posSurf already visited Surfaces, Edits, or Modifiers that participate positively in the condition.
    	 * @param negSurf already visited Surfaces, Edits, or Modifiers that participate negatively in the condition.
      	 * @param posSurf Tags that already belong to the condition (positively).
		 * @param negSurf Tags that already belong to the condition (negatively). */
        Ptr<ASTOp> GenerateDataCodition(size_t Index,
									        const TArray<FString>& PositiveTags,
											const TArray<FString>& NegativeTags,
                                            const set<size_t>& posSurf,
                                            const set<size_t>& negSurf,
                                            const set<size_t>& posTag,
                                            const set<size_t>& negTag);
    };

}

