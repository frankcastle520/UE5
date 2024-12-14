// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Common/Common.h" // for int32_t
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Toolchain/ModularFeature.h"

#include "uLang/Common/Text/Symbol.h"
#include "uLang/Common/Containers/SharedPointerSet.h"

namespace uLang
{
/* TModularFeatureRegHandle<> -- Definitions
 ******************************************************************************/

namespace Private
{
    using RegistryId = SymbolId;

    class IModularFeatureRegistry : public CSharedMix
    {
    private:
        friend class CModularFeatureRegistry;
        IModularFeatureRegistry() {}
    };

    /** Private base class for sharing registration functionality only with our templatized RAII handles. */
    class CModularFeatureRegistrar
    {
    public: // intended to be private, to limit access, but clang apparently doesn't follow templatized friend decls
        template<class FeatureType> friend class TModularFeatureRegHandle;

        VERSECOMPILER_API static const TSRef<IModularFeatureRegistry>& GetRegistry();
        VERSECOMPILER_API static void SetRegistry(const TSRef<IModularFeatureRegistry>& NewRegistry);

        VERSECOMPILER_API static void Register(const TSRef<IModularFeature>& NewFeature, const RegistryId FeatureId);
        VERSECOMPILER_API static bool Unregister(const TSRef<IModularFeature>& Feature);

        VERSECOMPILER_API static RegistryId GetRegistryId(const char* FeatureName);
    };

    /**
     * SFINAE utility helpers for catching mis-implemented ModularFeature classes.
     */
    template<typename T>
    struct VoidType
    {
        using Type = void;
    };
    template<typename FeatureType, typename = void>
    struct TFeatureHasUid
    {
        enum { Value = false };
    };
    template<typename FeatureType>
    struct TFeatureHasUid<FeatureType, typename VoidType<typename FeatureType::CFeatureType>::Type>
    {
        using CFeatureType = typename FeatureType::CFeatureType;
        enum { Value = true };
    };

    template<class FeatureType>
    RegistryId GetUidForFeature()
    {
        static_assert(Private::TFeatureHasUid<FeatureType>::Value,
            "Your ModularFeature class is missing its UID -- You must add ULANG_FEATURE_ID_DECL() to the class body.");
        return CModularFeatureRegistrar::GetRegistryId(FeatureType::FeatureName);
    }
}

template<class FeatureType> template<typename... Args_t>
ULANG_FORCEINLINE TModularFeatureRegHandle<FeatureType>::TModularFeatureRegHandle(Args_t&&... Args)
    : _ModularFeatureRef(TSRef<FeatureType>::New(uLang::ForwardArg<Args_t>(Args)...))
{
    Private::CModularFeatureRegistrar::Register(_ModularFeatureRef, Private::GetUidForFeature<FeatureType>());
}

template<class FeatureType>
ULANG_FORCEINLINE uLang::TModularFeatureRegHandle<FeatureType>::~TModularFeatureRegHandle()
{
    Private::CModularFeatureRegistrar::Unregister(_ModularFeatureRef);
}

/* ModularFeatureManager API -- Definitions
 ******************************************************************************/

namespace Private
{
    VERSECOMPILER_API int32_t GetModularFeatureCount(const RegistryId FeatureId);
    VERSECOMPILER_API TSPtr<IModularFeature> GetModularFeature(const RegistryId FeatureId, const int32_t Index);

    template<class FeatureType>
    RegistryId GetUidForFeatureQuery()
    {
        RegistryId RegId = GetUidForFeature<FeatureType>();
        static_assert(std::is_same_v<FeatureType, typename FeatureType::CFeatureType>,
            "You cannot use sub-classes when querying for specific ModularFeatures. You must use the base feature class.");
        return RegId;
    }
}

template<class FeatureType>
ULANG_FORCEINLINE int32_t GetModularFeatureCount()
{
    return Private::GetModularFeatureCount(Private::GetUidForFeatureQuery<FeatureType>());
}

template<class FeatureType>
TOptional< TSRef<FeatureType> > GetModularFeature(const int32_t Index)
{
    TOptional< TSRef<FeatureType> > Result;

    TSPtr<Private::IModularFeature> RegisteredFeature = Private::GetModularFeature(Private::GetUidForFeatureQuery<FeatureType>(), Index);
    if (RegisteredFeature.IsValid())
    {
        // Noteworthy downcast (normally ill advised) -- required since we're storing base IModularFeature pointers
        // Okay, because we're indexing using a class unique identifier, and we strictly control allocation & registration
        Result = RegisteredFeature.As<FeatureType>().AsRef();
    }
    return Result;
}

template<class FeatureType>
TSRefArray<FeatureType> GetModularFeaturesOfType()
{
    TSRefArray<FeatureType> OutArray;
    for (TModularFeatureIterator<FeatureType> FeatureIt; FeatureIt; ++FeatureIt)
    {
        OutArray.Add(FeatureIt.Get());
    }
    return OutArray;
}

/* TModularFeatureIterator - Definitions
 ******************************************************************************/

template<class FeatureType>
ULANG_FORCEINLINE TModularFeatureIterator<FeatureType>::operator bool() const
{
    return GetModularFeature<FeatureType>(_Index).IsSet();
}

template<class FeatureType>
ULANG_FORCEINLINE bool TModularFeatureIterator<FeatureType>::operator!() const
{
    return !(bool)*this;
}

template<class FeatureType>
ULANG_FORCEINLINE TSRef<FeatureType> TModularFeatureIterator<FeatureType>::Get() const
{
    TOptional< TSRef<FeatureType> > Optional = GetModularFeature<FeatureType>(_Index);
    ULANG_ASSERTF(Optional.IsSet(), "Dereferencing an invalid feature iterator -- check validity first.");

    return Optional.GetValue();
}

template<class FeatureType>
ULANG_FORCEINLINE FeatureType& TModularFeatureIterator<FeatureType>::operator*() const
{
    return *Get();
}

template<class FeatureType>
ULANG_FORCEINLINE FeatureType* TModularFeatureIterator<FeatureType>::operator->() const
{
    return this->Get().Get();
}

template<class FeatureType>
ULANG_FORCEINLINE void TModularFeatureIterator<FeatureType>::operator++()
{
    ++_Index;
}

} // namespace uLang
