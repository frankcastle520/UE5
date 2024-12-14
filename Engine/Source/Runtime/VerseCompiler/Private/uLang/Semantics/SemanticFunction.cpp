// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/SemanticClass.h"
#include "uLang/Semantics/SemanticInterface.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/TypeVariable.h"

namespace uLang
{
//=======================================================================================
// CFunction Methods
//=======================================================================================

CFunction::CFunction(int32_t Index, const CSymbol& FunctionName, CScope& EnclosingScope)
    : CDefinition(StaticDefinitionKind, EnclosingScope, FunctionName)
    , CLogicalScope(CScope::EKind::Function, &EnclosingScope, EnclosingScope.GetProgram())
    , _Index(Index)
    , _SignatureRevision(1)
    , _BodyRevision(1)
{
}

int32_t CFunction::Index() const
{
    return _Index;
}

void CFunction::SetSignature(SSignature&& Signature, SemanticRevision NextRevision)
{
    ULANG_ENSUREF(NextRevision > _SignatureRevision, "Revision to be set must be a greater number than any existing revisions.");

    _Signature = Move(Signature);
    _SignatureRevision = NextRevision;
    CLogicalScope::SetRevision(NextRevision);
}

void CFunction::MapSignature(const CFunctionType& FuncType, SemanticRevision NextRevision)
{
    ULANG_ENSUREF(NextRevision > _SignatureRevision, "Revision to be set must be a greater number than any existing revisions.");

    _Signature.SetFunctionType(&FuncType);

    TArray<CDataDefinition*> Params;
    for (CDataDefinition* DataDefinition : GetDefinitionsOfKind<CDataDefinition>())
    {
        Params.Add(DataDefinition);
    }
    _Signature.SetParams(Move(Params));

    _SignatureRevision = NextRevision;
    CLogicalScope::SetRevision(NextRevision);
}

Verse::TSPtr<uLang::CExprClassDefinition> CFunction::GetBodyClassDefinitionAst() const
{
    ULANG_ASSERTF(!GetIrNode(true), "Called AST function on when IR available");
    if (TSPtr<CExpressionBase> BodyAst = GetBodyAst())
    {
        if (BodyAst->GetNodeType() == EAstNodeType::Definition_Class)
        {
            return Move(BodyAst.As<CExprClassDefinition>());
        }
    }
    return nullptr;
}

Verse::TSPtr<uLang::CExprInterfaceDefinition> CFunction::GetBodyInterfaceDefinitionAst() const
{
    ULANG_ASSERTF(!GetIrNode(true), "Called AST function on when IR available");
    if (TSPtr<CExpressionBase> BodyAst = GetBodyAst())
    {
        if (BodyAst->GetNodeType() == EAstNodeType::Definition_Interface)
        {
            return Move(BodyAst.As<CExprInterfaceDefinition>());
        }
    }
    return nullptr;
}

CExprClassDefinition* CFunction::GetBodyClassDefinitionIr() const
{
    if (CExpressionBase* BodyIr = GetBodyIr())
    {
        if (BodyIr->GetNodeType() == EAstNodeType::Definition_Class)
        {
            return static_cast<CExprClassDefinition*>(BodyIr);
        }
    }
    return nullptr;
}

CExprInterfaceDefinition* CFunction::GetBodyInterfaceDefinitionIr() const
{
    if (CExpressionBase* BodyIr = GetBodyIr())
    {
        if (BodyIr->GetNodeType() == EAstNodeType::Definition_Interface)
        {
            return static_cast<CExprInterfaceDefinition*>(BodyIr);
        }
    }
    return nullptr;
}

TOptional<const CClass*> CFunction::GetMaybeClassScope() const
{
    TOptional<const CClass*> MaybeClass;

    const CScope* ParentScope = GetParentScope();
    while (ParentScope && ParentScope->GetKind() != CScope::EKind::Class)
    {
        ParentScope = ParentScope->GetParentScope();
    }

    if (ParentScope)
    {
        MaybeClass = static_cast<const CClass*>(ParentScope);
    }

    return MaybeClass;
}

TOptional<const CModule*> CFunction::GetMaybeModuleScope() const
{
    const CModule* MaybeModule = GetModule();
    return MaybeModule ? TOptional<const CModule*>(MaybeModule) : TOptional<const CModule*>();
}

TOptional<const CNominalType*> CFunction::GetMaybeContextType() const
{
    const CScope* ParentScope = GetParentScope();
    if (ParentScope->GetKind() == CScope::EKind::Class)
    {
        return static_cast<const CClass*>(ParentScope);
    }
    else if (ParentScope->GetKind() == CScope::EKind::Interface)
    {
        return static_cast<const CInterface*>(ParentScope);
    }
    else
    {
        return {};
    }
}

CUTF8String CFunction::GetDecoratedName(uint16_t StrFlags) const
{
    // See corresponding demangling code in FSolarisDebuggeeConnection::DemangleFunctionName() in SolarisDebugeeConnection.cpp
    const CFunction* BaseOverriddenFunction = GetBaseOverriddenDefinition().GetPrototypeDefinition();
    const CFunction* BaseCoercedOverriddenFunction = GetBaseCoercedOverriddenFunction().GetPrototypeDefinition();
    CUTF8StringBuilder Builder;

    // Overridden functions need to match so cannot differentiate native vs non-native
    bool bIsAsync = (_Signature.GetFunctionType() != nullptr) && _Signature.GetEffects()[EEffect::suspends];  // Error mid construction of function may not have function type set

    // we omit the full qualifier for async functions, at least for now, because
    // this gets included in the name of structures and gets overly verbose
    if ((StrFlags & uint16_t(EFunctionStringFlag::Scoped_)) && !bIsAsync)
    {
        if (StrFlags & uint16_t(EFunctionStringFlag::Qualified))
        {
            Builder.Append('(');
        }

        Builder.Append(BaseCoercedOverriddenFunction->_EnclosingScope.GetScopePath('/', CScope::EPathMode::PrefixSeparator).ToStringView());

        if (StrFlags & uint16_t(EFunctionStringFlag::Qualified))
        {
            Builder.Append(":)");
        }
        else if (StrFlags & uint16_t(EFunctionStringFlag::QualifiedDotted))
        {
            Builder.Append('.');
        }
    }

    Builder.Append(BaseCoercedOverriddenFunction->AsNameStringView());

    const SSignature& Signature = BaseCoercedOverriddenFunction->_Signature;

    const CFunctionType* Type = Signature.GetFunctionType();

    auto AppendParams = [&]
    {
        Builder.Append('(');
        Builder.Append(Type->GetParamsType().AsParamsCode().AsCString());
        Type->BuildTypeVariableCode(Builder);
        Builder.Append(')');
    };

    // If a coerced function was generated to override a base class function
    // that itself is a coerced override, or this function is a coercion whose
    // name need not otherwise match a base class function, add additional
    // decoration.  Note only overrides for whom the type exactly matches the
    // overridden function will share a name with the overridden function
    // (required by how instance invocation is handled).  Furthermore, a coerced
    // override (an override for which a coercion exists) may itself be
    // overridden with a function requiring a coercion.  The original override
    // is considered a special case, and receives the undecorated named, while
    // all other overrides use the decorated name.
    if (BaseOverriddenFunction != BaseCoercedOverriddenFunction || (IsCoercion() && this == BaseOverriddenFunction))
    {
        AppendParams();
        Type->BuildEffectAttributeCode(Builder);
        Builder.Append(':');
        Builder.Append(Type->GetReturnType().AsCode(ETypeSyntaxPrecedence::Definition));
    }
    else if (Signature.HasParams())
    {
        // Only decorate the name with the parameters if it has any.
        AppendParams();
    }
    return Builder.MoveToString();
}

bool CFunction::HasImplementation() const
{
    return GetBodyIr() || IsNative();
}

bool CFunction::IsNative() const
{
    return HasAttributeClass(GetProgram()._nativeClass, GetProgram());
}

// CDefinition interface.
void CFunction::SetAstNode(CExprFunctionDefinition* AstNode)
{
    CDefinition::SetAstNode(AstNode);
}
CExprFunctionDefinition* CFunction::GetAstNode() const
{
    return static_cast<CExprFunctionDefinition*>(CDefinition::GetAstNode());
}

void CFunction::SetIrNode(CExprFunctionDefinition* AstNode)
{
    CDefinition::SetIrNode(AstNode);
}
CExprFunctionDefinition* CFunction::GetIrNode(bool bForce) const
{
    return static_cast<CExprFunctionDefinition*>(CDefinition::GetIrNode(bForce));
}
}  // namespace uLang
