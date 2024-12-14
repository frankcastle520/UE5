// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/UnknownType.h"
#include "uLang/Semantics/SemanticProgram.h"

namespace uLang
{

//=======================================================================================
// CExprError Methods
//=======================================================================================

CExprError::CExprError(TUPtr<CUnknownType>&& ExprType, bool bCanFail /* = false */)
    : _ExprType(Move(ExprType))
    , _bCanFail(bCanFail)
{
}

const CTypeBase* CExprError::GetResultType(const CSemanticProgram& Program) const
{
    return _ExprType ? _ExprType.Get() : Program.GetDefaultUnknownType();
}

//=======================================================================================
// CExprPlaceholder Methods
//=======================================================================================

CExprPlaceholder::CExprPlaceholder(TUPtr<CUnknownType>&& ExprType)
    : _ExprType(Move(ExprType))
{
}

const CTypeBase* CExprPlaceholder::GetResultType(const CSemanticProgram& Program) const
{
    return _ExprType ? _ExprType.Get() : Program.GetDefaultUnknownType();
}


} // namespace uLang