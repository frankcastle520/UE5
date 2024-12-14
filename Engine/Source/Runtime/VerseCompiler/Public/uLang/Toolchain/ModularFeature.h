// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Common/Common.h" // for uintptr_t
#include "uLang/Common/Containers/SharedPointer.h" // for CSharedMix
#include "uLang/Common/Templates/Conditionals.h"
#include "uLang/Common/Templates/TypeTraits.h"

namespace uLang
{

namespace Private
{
    /**
     * Base class for the ModularFeatures -- marked as "Private" to discourage
     * directly sub-classing this (use TModularFeature<> instead).
     */
    class IModularFeature : public CSharedMix
    {
    public:
        virtual ~IModularFeature() {}
        virtual int32_t GetPriority() const = 0;
    };
}

/**
 * ModularFeature base class -- all modular feature interfaces should derive
 * from this base.
 */
template<typename FeatureType>
class TModularFeature : public Private::IModularFeature
{
public:
    //~ Begin IModularFeature interface
    virtual int32_t GetPriority() const override { return 0; }
    //~ End IModularFeature interface
};

// Required to fully declare your feature class. Intended to be declared inside your
// class declaration. Defines the feature's name, which is used to generate its UID.
#define ULANG_FEATURE_ID_DECL(FeatureType) \
public: \
    using CFeatureType = FeatureType; \
    static constexpr const char* FeatureName = #FeatureType; \
private:

} // namespace uLang

