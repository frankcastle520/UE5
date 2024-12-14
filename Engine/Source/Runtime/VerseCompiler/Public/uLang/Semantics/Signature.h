// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Containers/Array.h"
#include "uLang/Semantics/SemanticTypes.h"

namespace uLang
{
    class CDataDefinition;

/**
 * Type signature / parameter interface for routines/invokables
 **/
struct SSignature
{
    using ParamDefinitions = TArray<CDataDefinition*>;

    // Methods

    SSignature(const CFunctionType& FunctionType, ParamDefinitions&& Params)
        : _FunctionType(&FunctionType)
        , _Params(Move(Params))
    {}

    // @TODO: This represents an invalid signature (likely, yet to be fully analyzed) -- we should
    //        promote this invalid state up to the use sites (say, have an TOptional<SSignature> instead)
    SSignature() = default;

    void SetFunctionType(const CFunctionType* FunctionType) { _FunctionType = FunctionType; }
    void SetParams(ParamDefinitions&& Params)               { _Params = Move(Params); }
    void EmptyParams()                                      { _Params.Empty(); }

    bool HasParams() const
    {
        return !_Params.IsEmpty();
    }

    int32_t NumParams() const
    {
        return _Params.Num();
    }

    const ParamDefinitions& GetParams() const
    {
        return _Params;
    }

    const CTypeBase* GetParamType(int32_t ParamIndex) const
    {
        const CTypeBase& ParamsType = _FunctionType->GetParamsType();
        if (const CTupleType* TupleParamsType = ParamsType.GetNormalType().AsNullable<CTupleType>())
        {
            return (*TupleParamsType)[ParamIndex];
        }
        return &ParamsType;
    }

    const CTypeBase* GetParamsType() const
    {
        return ULANG_ENSUREF(_FunctionType, "Querying for a params type, when the function type has not been set.")
            ? &_FunctionType->GetParamsType()
            : nullptr;
    }

    const CTypeBase* GetReturnType() const
    {
        return ULANG_ENSUREF(_FunctionType, "Querying for a return type, when the function type has not been set.")
            ? &_FunctionType->GetReturnType()
            : nullptr;
    }

    const CFunctionType* GetFunctionType() const
    {
        return _FunctionType;
    }

    SEffectSet GetEffects() const
    {
        return ULANG_ENSUREF(_FunctionType, "Querying for function type flags, when the function type has not been set.") ? _FunctionType->GetEffects() : EffectSets::FunctionDefault;
    }

    friend bool operator==(const SSignature& Left, const SSignature& Right)
    {
        return Left._FunctionType == Right._FunctionType && Left._Params == Right._Params;
    }

    friend bool operator!=(const SSignature& Left, const SSignature& Right)
    {
        return !(Left == Right);
    }

    // Data members

private:
    const CFunctionType* _FunctionType = nullptr;
    ParamDefinitions _Params;
};  // SSignature

}  // namespace uLang
