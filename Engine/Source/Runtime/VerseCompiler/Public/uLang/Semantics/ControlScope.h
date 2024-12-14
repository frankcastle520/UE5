// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/SharedPointerArray.h"

namespace uLang
{

/**
 * Represents a function body or a nested scope within a function body.
 */
class CControlScope : public CLogicalScope, public CSharedMix
{
public:
    CControlScope(CScope* Parent, CSemanticProgram& Program, CSymbol Name = CSymbol())
        : CLogicalScope(EKind::ControlScope, Parent, Program)
    {}

    //~ Begin CScope interface
    virtual CSymbol GetScopeName() const override { return _Name; }
    //~ End CScope interface

private:
    CSymbol _Name;
};

};
