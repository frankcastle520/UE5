// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/Attributable.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticClass.h"

namespace uLang
{
static bool IsIdentifierHack(const CExpressionBase& AttributeExpression, const CDefinition* Definition, const CSemanticProgram& Program)
{
    if (AttributeExpression.GetNodeType() == EAstNodeType::Identifier_Unresolved)
    {
        const CExprIdentifierUnresolved& Identifier = static_cast<const CExprIdentifierUnresolved&>(AttributeExpression);
        return
            !Identifier.Context() &&
            !Identifier.Qualifier() &&
            Identifier._Symbol == Definition->GetName();
    }

    if (const CExprIdentifierClass* ClassIdentifier = AsNullable<CExprIdentifierClass>(&AttributeExpression))
    {
        return ClassIdentifier->GetClass(Program)->Definition() == Definition;
    }
    if (const CExprIdentifierFunction* FunctionIdentifier = AsNullable<CExprIdentifierFunction>(&AttributeExpression))
    {
        return &FunctionIdentifier->_Function == Definition;
    }
    if (const CExprArchetypeInstantiation* ArchetypeIdentifier = AsNullable<CExprArchetypeInstantiation>(&AttributeExpression))
    {
        return ArchetypeIdentifier->GetClass(Program)->Definition() == Definition;
    }

    return false;
}
bool IsAttributeHack(const SAttribute& Attribute, const CClass* AttributeClass, const CSemanticProgram& Program)
{
    return IsIdentifierHack(*Attribute._Expression, AttributeClass->Definition(), Program);
}
bool IsAttributeHack(const SAttribute& Attribute, const CFunction* AttributeFunction, const CSemanticProgram& Program)
{
    if (Attribute._Expression->GetNodeType() == EAstNodeType::Invoke_Invocation)
    {
        const CExprInvocation& Invocation = static_cast<const CExprInvocation&>(*Attribute._Expression);
        return IsIdentifierHack(*Invocation.GetCallee(), AttributeFunction, Program);
    }
    return false;
}

bool CAttributable::HasAttributeClass(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    return FindAttributeExpr(AttributeClass, Program) != nullptr;
}

int32_t CAttributable::GetAttributeClassCount(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    return FindAttributesImpl(AttributeClass, Program).Num();
}

TArray<const CExpressionBase*> CAttributable::GetAttributesWithAttribute(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TArray<const CExpressionBase*> RetVal;

    ULANG_ASSERTF(AttributeClass, "GetAttributesWithAttribute given a null attribute class");
    for (const SAttribute& Attr : _Attributes)
    {
        const uLang::CExpressionBase* AttrExpression = Attr._Expression;
        if (AttrExpression->HasAttributeClass(AttributeClass, Program))
        {
            RetVal.Add(Attr._Expression);
        }
    }

    return RetVal;
}

TOptional<int32_t> CAttributable::FindAttributeImpl(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    ULANG_ASSERTF(AttributeClass, "FindAttribute given a null attribute class");

    for (int32_t Index = 0; Index < _Attributes.Num(); ++Index)
    {
        const SAttribute& Attr = _Attributes[Index];
        if (const CTypeBase* ResultType = Attr._Expression->GetResultType(Program))
        {
            const CClass* ClassType = nullptr;

            // @HACK: SOL-972, need better (fuller) support for attribute functions/ctors
            if (const CTypeType* TypeType = ResultType->GetNormalType().AsNullable<CTypeType>())
            {
                if (const CTypeBase* PositiveType = TypeType->PositiveType())
                {
                    ClassType = PositiveType->GetNormalType().AsNullable<CClass>();
                }
            }

            if (ClassType == nullptr)
            {
                ClassType = ResultType->GetNormalType().AsNullable<CClass>();
            }

            if (ClassType != nullptr && ClassType->IsClass(*AttributeClass))
            {
                return Index;
            }
        }
    }

    return { };
}

TArray<int32_t> CAttributable::FindAttributesImpl(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    ULANG_ASSERTF(AttributeClass, "FindAttribute given a null attribute class");

    TArray<int32_t> RetVal;

    for (int32_t Index = 0; Index < _Attributes.Num(); ++Index)
    {
        const SAttribute& Attr = _Attributes[Index];
        if (const CTypeBase* ResultType = Attr._Expression->GetResultType(Program))
        {
            const CClass* ClassType = nullptr;

            // @HACK: SOL-972, need better (fuller) support for attribute functions/ctors
            if (const CTypeType* typeType = ResultType->GetNormalType().AsNullable<CTypeType>())
            {
                if (const CTypeBase* positiveType = typeType->PositiveType())
                {
                    ClassType = positiveType->GetNormalType().AsNullable<CClass>();
                }
            }

            if (ClassType == nullptr)
            {
                ClassType = ResultType->GetNormalType().AsNullable<CClass>();
            }

            if (ClassType != nullptr)
            {
                if (ClassType->IsClass(*AttributeClass))
                {
                    RetVal.Add(Index);
                }
            }
        }
    }

    return RetVal;
}

TOptional<SAttribute> CAttributable::FindAttribute(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TOptional<int32_t> Index = FindAttributeImpl(AttributeClass, Program);
    return Index ? _Attributes[*Index] : TOptional<SAttribute>{ };
}

TArray<SAttribute> CAttributable::FindAttributes(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TArray<SAttribute> RetVal;
    TArray<int32_t> Indices = FindAttributesImpl(AttributeClass, Program);

    for (int32_t Index : Indices)
    {
        RetVal.Add(_Attributes[Index]);
    }

    return RetVal;
}

const CExpressionBase* CAttributable::FindAttributeExpr(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    if (TOptional<SAttribute> Attr = FindAttribute(AttributeClass, Program))
    {
        return Attr->_Expression;
    }

    return nullptr;
}

const TArray<CExpressionBase*> CAttributable::FindAttributeExprs(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TArray<CExpressionBase*> RetVal;
    TArray<int32_t> Indices = FindAttributesImpl(AttributeClass, Program);

    for (int32_t Index : Indices)
    {
        RetVal.Add(_Attributes[Index]._Expression);
    }

    return RetVal;
}

void CAttributable::AddAttributeClass(const CClass* AttributeClass)
{
    SAttribute Attribute{TSRef<CExprIdentifierClass>::New(AttributeClass->GetTypeType()), SAttribute::EType::Specifier};
    _Attributes.Add(Move(Attribute));
}

void CAttributable::AddAttribute(SAttribute Attribute)
{
    _Attributes.Add(Move(Attribute));
}

void CAttributable::RemoveAttributeClass(const CClass* AttributeClass, const CSemanticProgram& Program)
{
    if (TOptional<int32_t> Index = FindAttributeImpl(AttributeClass, Program))
    {
        _Attributes.RemoveAt(*Index);
    }
}

// @HACK: SOL-972, We need full proper support for compile-time evaluation of attribute types
TOptional<CUTF8String> CAttributable::GetAttributeTextValue(const TArray<SAttribute>& Attributes, const CClass* AttributeClass, const CSemanticProgram& Program)
{
    ULANG_ASSERTF(AttributeClass, "GetAttributeTextValue given a null attribute class");

    TOptional<CUTF8String> TextValue;
    TextValue.Reset();

    for (const SAttribute& Attr : Attributes)
    {
        const CExpressionBase* AttribExpr = Attr._Expression;

        if (AttribExpr->GetNodeType() == EAstNodeType::Invoke_Invocation)
        {
            const CExprInvocation& AttrInvocation = *static_cast<const CExprInvocation*>(AttribExpr);
            if (&AttrInvocation.GetResolvedCalleeType()->GetReturnType().GetNormalType() == AttributeClass)
            {
                if (AttrInvocation.GetArgument()->GetNodeType() != EAstNodeType::Invoke_MakeTuple)
                {
                    const CExpressionBase& Argument = *AttrInvocation.GetArgument();
                    const EAstNodeType ArgumentNodeType = Argument.GetNodeType();
                    if (ArgumentNodeType == EAstNodeType::Literal_String)
                    {
                        TextValue = CUTF8String(static_cast<const CExprString&>(Argument)._String);
                        break;
                    }
                    else if (ArgumentNodeType == EAstNodeType::Invoke_Type)
                    {
                        const CExprInvokeType& InvokeType = static_cast<const CExprInvokeType&>(Argument);
                        if (InvokeType._Argument->GetNodeType() == EAstNodeType::Literal_String)
                        {
                            TextValue = CUTF8String(static_cast<const CExprString&>(*InvokeType._Argument)._String);
                            break;
                        }
                    }
                    // NOTE: (yiliang.siew) Example: this branch is hit when we have multi-line `@doc` attributes.
                    else if (ArgumentNodeType == EAstNodeType::Invoke_Invocation)
                    {
                        CUTF8StringBuilder TextBuilder;
                        const CExprInvocation& InvocationType = static_cast<const CExprInvocation&>(Argument);
                        const TSPtr<CExpressionBase>& InvocationArgument = InvocationType.GetArgument();
                        if (!InvocationArgument.IsValid() || InvocationArgument->GetNodeType() != EAstNodeType::Invoke_MakeTuple)
                        {
                            continue;
                        }
                        const CExprMakeTuple& MakeTupleType = static_cast<const CExprMakeTuple&>(*InvocationArgument);
                        for (const TSPtr<CExpressionBase>& SubExpr : MakeTupleType.GetSubExprs())
                        {
                            if (!SubExpr.IsValid())
                            {
                                continue;
                            }
                            // NOTE: (yiliang.siew) This is a HACK that is specifically for `@doc` attributes, since the
                            // macro invocation syntax is the only way to make multi-line doc comments work right now.
                            const EAstNodeType SubExprNodeType = SubExpr->GetNodeType();
                            if (SubExprNodeType == EAstNodeType::Invoke_Invocation && AttributeClass->GetScopeName().AsStringView() == "doc_attribute")
                            {
                                TextBuilder.Append('\n');
                            }
                            else if (SubExprNodeType == EAstNodeType::Literal_String)
                            {
                                const CExprString& TheString = static_cast<const CExprString&>(*SubExpr);
                                TextBuilder.Append(TheString._String);
                            }
                        }
                        TextValue = TextBuilder.MoveToString();
                        break;
                    }
                }
            }
        }
    }
    return TextValue;
}

TOptional<CUTF8String> CAttributable::GetAttributeTextValue(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    return GetAttributeTextValue(_Attributes, AttributeClass, Program);
}

bool CAttributable::HasAttributeClassHack(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    auto Last = _Attributes.end();
    return FindAttributeHack(_Attributes.begin(), Last, AttributeClass, Program) != Last;
}

bool CAttributable::HasAttributeFunctionHack(const CFunction* AttributeFunction, const CSemanticProgram& Program) const
{
    auto Last = _Attributes.end();
    return FindAttributeHack(_Attributes.begin(), Last, AttributeFunction, Program) != Last;
}
}
