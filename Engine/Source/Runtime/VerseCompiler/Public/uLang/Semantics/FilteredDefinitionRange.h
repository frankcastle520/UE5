// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Semantics/Definition.h"

namespace uLang
{
class CDefinition;

/// Filters a range of definitions to only include definitions of the kind corresponding to FilterClass.
template<typename FilterClass>
class TFilteredDefinitionRange
{
public:

    /// An iterator in a filtered definition range.
    class Iterator
    {
    public:
        Iterator(const TSRef<CDefinition>* Current, const TSRef<CDefinition>* End)
            : _Current(Current)
            , _End(End)
        {
            FindNext();
        }

        friend bool operator==(const Iterator& Lhs, const Iterator& Rhs)
        {
            return Lhs._Current == Rhs._Current
                && Lhs._End == Rhs._End;
        }

        friend bool operator!=(const Iterator& Lhs, const Iterator& Rhs)
        {
            return Lhs._Current != Rhs._Current
                || Lhs._End != Rhs._End;
        }

        Iterator& operator++()
        {
            if (_Current != _End)
            {
                ++_Current;
                FindNext();
            }
            return *this;
        }

        ULANG_FORCEINLINE const TSRef<FilterClass>& operator*() const
        {
            ULANG_ASSERTF(_Current != _End && (*_Current)->IsA<FilterClass>(), "Invalid iterator state");
            return _Current->As<FilterClass>();
        }

        ULANG_FORCEINLINE const FilterClass* operator->() const
        {
            ULANG_ASSERTF(_Current != _End && (*_Current)->IsA<FilterClass>(), "Invalid iterator state");
            return _Current->As<FilterClass>().Get();
        }

    private:
        const TSRef<CDefinition>* _Current;
        const TSRef<CDefinition>* _End;

        void FindNext()
        {
            while (_Current != _End && !(*_Current)->IsA<FilterClass>())
            {
                ++_Current;
            }
        }
    };

    TFilteredDefinitionRange(const TSRef<CDefinition>* Begin, const TSRef<CDefinition>* End)
    : _Begin(Begin), _End(End) {}

    Iterator begin() const { return Iterator(_Begin, _End); }
    Iterator end() const { return Iterator(_End, _End); }

private:
    const TSRef<CDefinition>* _Begin;
    const TSRef<CDefinition>* _End;
};

}