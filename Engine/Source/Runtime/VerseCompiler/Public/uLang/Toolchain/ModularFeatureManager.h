// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Common/Containers/SharedPointerArray.h"
#include "uLang/Common/Misc/Optional.h"

namespace uLang
{
/**
 * RAII style handle that wraps a ModularFeature TSPtr<>. Authoritative
 * control for a features' registration lifetime -- sole controller for
 * registering features.
 *
 * Use this to spawn and manage the lifetime of your module's ModularFeature instances.
 */
template<class FeatureType>
class TModularFeatureRegHandle
{
public:
    template<typename... Args_t>
    ULANG_FORCEINLINE TModularFeatureRegHandle(Args_t&&... Args);

    ULANG_FORCEINLINE ~TModularFeatureRegHandle();

    // Prevent copying/assignment to properly control registration lifetime (since this unregisters on destruction)
    TModularFeatureRegHandle(const TModularFeatureRegHandle&) = delete;
    TModularFeatureRegHandle& operator=(const TModularFeatureRegHandle&) = delete;

    ULANG_FORCEINLINE TModularFeatureRegHandle(TModularFeatureRegHandle&& Other)
        : _ModularFeatureRef(Move(Other._ModularFeatureRef))
    {
    }

    ULANG_FORCEINLINE TModularFeatureRegHandle& operator=(TModularFeatureRegHandle&& Other)
    {
        _ModularFeatureRef = Move(Other._ModularFeatureRef);
        return *this;
    }

    // Conversion methods
    ULANG_FORCEINLINE operator TSRef<FeatureType>() const             { return _ModularFeatureRef; }
    ULANG_FORCEINLINE operator FeatureType*() const                   { return (FeatureType*)_ModularFeatureRef; }
    ULANG_FORCEINLINE FeatureType&                operator*() const   { return *_ModularFeatureRef; }
    ULANG_FORCEINLINE FeatureType*                operator->() const  { return _ModularFeatureRef.operator->(); }

    //---------------------------------------------------------------------------------------

    ULANG_FORCEINLINE TSRef<FeatureType>&         ToSharedRef()       { return _ModularFeatureRef; }
    ULANG_FORCEINLINE const TSRef<FeatureType>&   ToSharedRef() const { return _ModularFeatureRef; }

private:
    TSRef<FeatureType> _ModularFeatureRef;
};

/* ModularFeatureManager API
 ******************************************************************************/

/**
 * Returns the number of registered implementations of the specified feature type.
 */
template<class FeatureType>
int32_t GetModularFeatureCount();

/**
 * Queries for a specific modular feature. Returns an empty TOptional<> if the
 * feature is not available. Does not assert.
 *
 * @return An optional (which could be empty if no features of the specified type
 *         are registered), wrapping a shared ModularFeature pointer.
 */
template<class FeatureType>
TOptional< TSRef<FeatureType> > GetModularFeature(const int32_t Index = 0);

/**
 * Queries for a specific modular feature. Returns an empty array if the
 * feature is not available. Does not assert.
 *
 * @return An array of all registered features of a single type -- sorted in priority order.
 */
template<class FeatureType>
TSRefArray<FeatureType> GetModularFeaturesOfType();

/* TModularFeatureIterator
 ******************************************************************************/

/**
 * Mechanism for iterating over all registered modular features of a certain type.
 */
template<class FeatureType>
class TModularFeatureIterator
{
public:
    explicit operator bool() const;
    bool operator!() const;

    TSRef<FeatureType> Get() const;
    FeatureType& operator*() const;
    FeatureType* operator->() const;

    void operator++();

private:
    int32_t _Index = 0;
};

} // namespace uLang

// Template definitions
#include "uLang/Toolchain/ModularFeatureManager.inl"
