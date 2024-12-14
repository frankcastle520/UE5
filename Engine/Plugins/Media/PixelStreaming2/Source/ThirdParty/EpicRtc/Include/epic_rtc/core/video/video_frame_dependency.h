// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

#include "epic_rtc/common/defines.h"
#include "epic_rtc/containers/epic_rtc_array.h"
#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

// NOLINTNEXTLINE(readability-identifier-naming) : AbstractClassSuffix
class EpicRtcFrameDependencyStructure : public EpicRtcRefCountInterface
{
public:
    /**
     * Gets the structure id.
     * @return Id.
     */
    virtual EMRTC_API int32_t GetStructureId() = 0;

    /**
     * Gets the number of decode targets.
     * @return NumDecodeTargets.
     */
    virtual EMRTC_API int32_t GetNumDecodeTargets() = 0;

    /**
     * Gets the number of chains.
     * @return NumChains.
     */
    virtual EMRTC_API int32_t GetNumChains() = 0;

    /**
     * Gets the array of decode targets protected by chain.
     * @return DecodeTargetProtectedByChain.
     */
    virtual EMRTC_API EpicRtcInt32ArrayInterface* GetDecodeTargetProtectedByChain() = 0;

    /**
     * Gets the array of resolutions.
     * @return Resolutions.
     */
    virtual EMRTC_API EpicRtcVideoResolutionArrayInterface* GetResolutions() = 0;

    /**
     * Gets the array of frame info templates.
     * @return Templates.
     */
    virtual EMRTC_API EpicRtcGenericFrameInfoArrayInterface* GetTemplates() = 0;

    EpicRtcFrameDependencyStructure(const EpicRtcFrameDependencyStructure&) = delete;
    EpicRtcFrameDependencyStructure& operator=(const EpicRtcFrameDependencyStructure&) = delete;

protected:
    EpicRtcFrameDependencyStructure() = default;
    virtual ~EpicRtcFrameDependencyStructure() = default;
};

#pragma pack(pop)
