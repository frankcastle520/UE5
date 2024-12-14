// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/AvailableAttributeUtils.h"

#include "uLang/Common/Common.h"
#include "uLang/Common/Text/Symbol.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticTypes.h"

namespace uLang
{
namespace
{
TOptional<uint64_t> GetIntegerDefinitionValue(TSPtr<CExprDefinition> ExprDefinition, CSemanticProgram& SemanticProgram)
{
    const CNormalType& ArgType = ExprDefinition->GetResultType(SemanticProgram)->GetNormalType();
    if (const CIntType* IntArg = ArgType.AsNullable<CIntType>())
    {
        if (TSPtr<CExprInvokeType> ValueInvokeExpr = AsNullable<CExprInvokeType>(ExprDefinition->Value()))
        {
            if (TSPtr<CExprNumber> NumberExpr = AsNullable<CExprNumber>(ValueInvokeExpr->_Argument))
            {
                if (!NumberExpr->IsFloat())
                {
                    return static_cast<uint64_t>(NumberExpr->GetIntValue());
                }
            }
        }
    }

    return TOptional<uint64_t>();
}

CSymbol GetArgumentName(TSPtr<CExprDefinition> ExprDefinition)
{
    TSPtr<CExpressionBase> ElementExpr = ExprDefinition->Element();
    if (TSPtr<CExprIdentifierData> IdentifierData = AsNullable<CExprIdentifierData>(ElementExpr))
    {
        return IdentifierData->GetName();
    }

    return CSymbol();
}

} // namespace anonymous

TOptional<uint64_t> GetAvailableAttributeVersion(const CDefinition& Definition, CSemanticProgram& SemanticProgram)
{
    ULANG_ASSERTF(SemanticProgram._availableClass, "Available class definition not found");

    if (const CClass* AvailableClass = SemanticProgram._availableClass)
    {
        if (TOptional<SAttribute> AvailabieAttribute = Definition.FindAttribute(AvailableClass, SemanticProgram))
        {
            //const CExpressionBase* AvailableAttributeExpr = AvailabieAttribute->_Expression;
            if (const CExprArchetypeInstantiation* AvailableArchInst = AsNullable<CExprArchetypeInstantiation>(AvailabieAttribute->_Expression))
            {
                TOptional<uint64_t> MinVersion;

                for (TSRef<CExpressionBase> Argument : AvailableArchInst->Arguments())
                {
                    if (TSPtr<CExprDefinition> ArgDefinition = AsNullable<CExprDefinition>(Argument))
                    {
                        if (GetArgumentName(ArgDefinition) == SemanticProgram._IntrinsicSymbols._MinUploadedAtFNVersion)
                        {
                            MinVersion = GetIntegerDefinitionValue(ArgDefinition, SemanticProgram);
                            break;
                        }
                    }
                }

                return MinVersion;
            }
        }
    }

    // No @available attribute
    return TOptional<uint64_t>{};
}

// Combine the available-attribute version with any available-attributes found on the parent scopes.
// A likely case: 
//    @available{MinUploadedAtFNVersion:=3000} 
//    C := class { Value:int=42 }
// The combined available-version for Value is 3000 given it's parent context. This also applies if there
// are multiple versions at different containing scopes - the final applicable version is the most-restrictive one.
TOptional<uint64_t> CalculateCombinedAvailableAttributeVersion(const CDefinition& Definition, CSemanticProgram& SemanticProgram)
{
    auto CombineResultsHelper = [&SemanticProgram](const CDefinition* Definition, const TOptional<uint64_t>& CurrentResult) -> TOptional<uint64_t>
        {
            TOptional<uint64_t> Result = CurrentResult;
            if (TOptional<uint64_t> AttributeValue = GetAvailableAttributeVersion(*Definition, SemanticProgram))
            {
                Result = CMath::Max(CurrentResult.Get(0), AttributeValue.GetValue());
            }

            return Result;
        };

    TOptional<uint64_t> CombinedResult = CombineResultsHelper(&Definition, TOptional<uint64_t>());

    // TODO: @available isn't applied to CModulePart correctly - CModuleParts cannot themselves hold attributes, so this snippet becomes a problem:
    // 
    // @available{ MinUploadedAtFNVersion: = 3000 }
    // M<public>: = module {...}
    //
    // @available{ MinUploadedAtFNVersion: = 4000 }
    // M<public>: = module {...}
    //
    // The first module-M gets an available version of 3000. The second @available attribute is processed, but isn't applied to the CModule type. 
    // This kind of attribute should be held on the CModulePart instead.

    const CScope* Scope = &Definition._EnclosingScope;
    while (Scope != nullptr)
    {
        if (const CDefinition* ScopeDefinition = Scope->ScopeAsDefinition())
        {
            CombinedResult = CombineResultsHelper(ScopeDefinition, CombinedResult.Get(0));
        }

        Scope = Scope->GetParentScope();
    }

    return CombinedResult;
}

bool IsDefinitionAvailableAtVersion(const CDefinition& Definition, uint64_t Version, CSemanticProgram& SemanticProgram)
{
    if (TOptional<uint64_t> AttributeVersion = CalculateCombinedAvailableAttributeVersion(Definition, SemanticProgram))
    {
        return AttributeVersion.GetValue() <= Version;
    }

    // Not version-filtered
    return true;
}

} // namespace uLang
