// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Templates/References.h"
#include "uLang/Common/Templates/Storage.h"

namespace uLang
{

/**
 * Traits class which tests if a type is optional.
 */
template <typename T> struct TIsOptional;

/**
 * When we have an optional value IsSet() returns true, and GetValue() is meaningful.
 * Otherwise GetValue() is not meaningful.
 */
template<typename OptionalType>
struct TOptional
{
public:
    /** Construct an OptionaType with a valid value. */
    TOptional(const OptionalType& Value)
    {
        new(&_Value.Get()) OptionalType(Value);
        _Result = EResult::OK;
    }
    TOptional(OptionalType&& Value)
    {
        new(&_Value.Get()) OptionalType(uLang::MoveIfPossible(Value));
        _Result = EResult::OK;
    }

    /** Construct an OptionalType with no value; i.e. unset */
    TOptional(EResult Result = EResult::Unspecified)
        : _Result(Result)
    {
        ULANG_ASSERTF(Result != EResult::OK, "Must not initialize TOptional with EResult::OK without also providing a value.");
    }

    ~TOptional()
    {
        Reset();
    }

    /** Copy/Move construction */
    TOptional(const TOptional& Value)
        : _Result(Value._Result)
    {
        if (Value._Result == EResult::OK)
        {
            new(&_Value.Get()) OptionalType(Value._Value.Get());
        }
    }
    TOptional(TOptional&& Value)
        : _Result(Value._Result)
    {
        if (Value._Result == EResult::OK)
        {
            new(&_Value.Get()) OptionalType(uLang::MoveIfPossible(Value._Value.Get()));
        }
    }

    TOptional& operator=(const TOptional& Value)
    {
        if (&Value != this)
        {
            Reset();
            _Result = Value._Result;
            if (Value._Result == EResult::OK)
            {
                new(&_Value.Get()) OptionalType(Value._Value.Get());
            }
        }
        return *this;
    }
    TOptional& operator=(TOptional&& Value)
    {
        if (&Value != this)
        {
            Reset();
            _Result = Value._Result;
            if (Value._Result == EResult::OK)
            {
                new(&_Value.Get()) OptionalType(uLang::MoveIfPossible(Value._Value.Get()));
            }
        }
        return *this;
    }

    TOptional& operator=(const OptionalType& Value)
    {
        if (&Value != &_Value.Get())
        {
            Reset();
            new(&_Value.Get()) OptionalType(Value);
            _Result = EResult::OK;
        }
        return *this;
    }
    TOptional& operator=(OptionalType&& Value)
    {
        if (&Value != &_Value.Get())
        {
            Reset();
            new(&_Value.Get()) OptionalType(uLang::MoveIfPossible(Value));
            _Result = EResult::OK;
        }
        return *this;
    }

    void Reset()
    {
        if (_Result == EResult::OK)
        {
            // We need a typedef here because VC won't compile the destructor call below if OptionalType itself has a member called OptionalType
            using OptionalDestructOptionalType = OptionalType;
            _Value.Get().OptionalDestructOptionalType::~OptionalDestructOptionalType();
            _Result = EResult::Unspecified;
        }
    }

    template <typename... ArgsType>
    void Emplace(ArgsType&&... Args)
    {
        Reset();
        new(&_Value.Get()) OptionalType(ForwardArg<ArgsType>(Args)...);
        _Result = EResult::OK;
    }

    friend bool operator==(const TOptional& lhs, const TOptional& rhs)
    {
        if (lhs._Result != rhs._Result)
        {
            return false;
        }
        if (lhs._Result != EResult::OK) // both unset
        {
            return true;
        }
        return lhs._Value.Get() == rhs._Value.Get();
    }

    friend bool operator!=(const TOptional& lhs, const TOptional& rhs)
    {
        if (lhs._Result != rhs._Result)
        {
            return true;
        }
        if (lhs._Result != EResult::OK) // both unset
        {
            return false;
        }
        return lhs._Value.Get() != rhs._Value.Get();
    }

    /** @return true when the value is meaningful; false if calling GetValue() is undefined. */
    ULANG_FORCEINLINE bool IsSet() const { return _Result == EResult::OK; }
    ULANG_FORCEINLINE EResult GetResult() const { return _Result; }
    ULANG_FORCEINLINE explicit operator bool() const { return _Result == EResult::OK; }

    ULANG_FORCEINLINE operator       OptionalType*()       { return IsSet() ? &_Value.Get() : nullptr; }
    ULANG_FORCEINLINE operator const OptionalType*() const { return IsSet() ? &_Value.Get() : nullptr; }

    /** @return The optional value; undefined when IsSet() returns false. */
    const OptionalType& GetValue() const { ULANG_ASSERTF(IsSet(), "It is an error to call GetValue() on an unset TOptional. Please either assert IsSet() or use Get(DefaultValue) instead."); return _Value.Get(); }
          OptionalType& GetValue()       { ULANG_ASSERTF(IsSet(), "It is an error to call GetValue() on an unset TOptional. Please either assert IsSet() or use Get(DefaultValue) instead."); return _Value.Get(); }

    /** @return The optional value; undefined when IsSet() returns false. */
    const OptionalType& operator*() const { return GetValue(); }
          OptionalType& operator*()       { return GetValue(); }

    const OptionalType* operator->() const { return &GetValue(); }
          OptionalType* operator->()       { return &GetValue(); }

    /** @return The optional value when set; DefaultValue otherwise. */
    const OptionalType& Get(const OptionalType& DefaultValue) const { return IsSet() ? _Value.Get() : DefaultValue; }

private:
    TTypeCompatibleBytes<OptionalType> _Value;
    EResult _Result;
};

template <typename T> struct TIsOptional { enum { Value = false }; };
template <typename T> struct TIsOptional<TOptional<T>> { enum { Value = true }; };

}