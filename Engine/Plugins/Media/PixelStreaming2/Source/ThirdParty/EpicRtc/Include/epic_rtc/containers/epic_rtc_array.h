// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/common/common.h"
#include "epic_rtc/common/defines.h"
#include "epic_rtc/containers/epic_rtc_string.h"
#include "epic_rtc/core/ref_count.h"

#include <cstdint>

#pragma pack(push, 8)

// Forward Declarations
enum class EpicRtcVideoScalabilityMode : uint8_t;
struct EpicRtcVideoResolutionBitrateLimits;
enum class EpicRtcPixelFormat : uint8_t;
class EpicRtcVideoCodecInfoInterface;
struct EpicRtcAudioCodecInfo;
enum class EpicRtcVideoFrameType : char;
struct EpicRtcParameterPair;
struct EpicRtcSpatialLayer;
enum class EpicRtcDecodeTargetIndication : uint8_t;
struct EpicRtcCodecBufferUsage;
struct EpicRtcVideoResolution;
class EpicRtcGenericFrameInfoInterface;

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcStringArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcStringInterface* const* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcStringInterface** Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcStringArrayInterface(const EpicRtcStringArrayInterface&) = delete;
    EpicRtcStringArrayInterface& operator=(const EpicRtcStringArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcStringArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcStringArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcInt32ArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const int32_t* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API int32_t* Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcInt32ArrayInterface(const EpicRtcInt32ArrayInterface&) = delete;
    EpicRtcInt32ArrayInterface& operator=(const EpicRtcInt32ArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcInt32ArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcInt32ArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcBoolArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcBool* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcBool* Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcBoolArrayInterface(const EpicRtcBoolArrayInterface&) = delete;
    EpicRtcBoolArrayInterface& operator=(const EpicRtcBoolArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcBoolArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcBoolArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcVideoScalabilityModeArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcVideoScalabilityMode* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcVideoScalabilityMode* Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcVideoScalabilityModeArrayInterface(const EpicRtcVideoScalabilityModeArrayInterface&) = delete;
    EpicRtcVideoScalabilityModeArrayInterface& operator=(const EpicRtcVideoScalabilityModeArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcVideoScalabilityModeArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoScalabilityModeArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcVideoResolutionBitrateLimitsArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcVideoResolutionBitrateLimits* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcVideoResolutionBitrateLimits* Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcVideoResolutionBitrateLimitsArrayInterface(const EpicRtcVideoResolutionBitrateLimitsArrayInterface&) = delete;
    EpicRtcVideoResolutionBitrateLimitsArrayInterface& operator=(const EpicRtcVideoResolutionBitrateLimitsArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcVideoResolutionBitrateLimitsArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoResolutionBitrateLimitsArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcPixelFormatArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcPixelFormat* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcPixelFormat* Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcPixelFormatArrayInterface(const EpicRtcPixelFormatArrayInterface&) = delete;
    EpicRtcPixelFormatArrayInterface& operator=(const EpicRtcPixelFormatArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcPixelFormatArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcPixelFormatArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcVideoCodecInfoArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcVideoCodecInfoInterface* const* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcVideoCodecInfoInterface** Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcVideoCodecInfoArrayInterface(const EpicRtcVideoCodecInfoArrayInterface&) = delete;
    EpicRtcVideoCodecInfoArrayInterface& operator=(const EpicRtcVideoCodecInfoArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcVideoCodecInfoArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoCodecInfoArrayInterface() = default;
};

class EpicRtcParameterPairArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcParameterPair* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcParameterPair* Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcParameterPairArrayInterface(const EpicRtcParameterPairArrayInterface&) = delete;
    EpicRtcParameterPairArrayInterface& operator=(const EpicRtcParameterPairArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcParameterPairArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcParameterPairArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcAudioCodecInfoArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcAudioCodecInfo* Get() const = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;
};
/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcVideoFrameTypeArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcVideoFrameType* Get() const = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcVideoFrameTypeArrayInterface(const EpicRtcVideoFrameTypeArrayInterface&) = delete;
    EpicRtcVideoFrameTypeArrayInterface& operator=(const EpicRtcVideoFrameTypeArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcVideoFrameTypeArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoFrameTypeArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcSpatialLayerArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcSpatialLayer* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcSpatialLayer* Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcSpatialLayerArrayInterface(const EpicRtcSpatialLayerArrayInterface&) = delete;
    EpicRtcSpatialLayerArrayInterface& operator=(const EpicRtcSpatialLayerArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcSpatialLayerArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcSpatialLayerArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcDecodeTargetIndicationArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcDecodeTargetIndication* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcDecodeTargetIndication* Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcDecodeTargetIndicationArrayInterface(const EpicRtcDecodeTargetIndicationArrayInterface&) = delete;
    EpicRtcDecodeTargetIndicationArrayInterface& operator=(const EpicRtcDecodeTargetIndicationArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcDecodeTargetIndicationArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcDecodeTargetIndicationArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcCodecBufferUsageArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcCodecBufferUsage* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcCodecBufferUsage* Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcCodecBufferUsageArrayInterface(const EpicRtcCodecBufferUsageArrayInterface&) = delete;
    EpicRtcCodecBufferUsageArrayInterface& operator=(const EpicRtcCodecBufferUsageArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcCodecBufferUsageArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcCodecBufferUsageArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcVideoResolutionArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API const EpicRtcVideoResolution* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcVideoResolution* Get() = 0;

    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcVideoResolutionArrayInterface(const EpicRtcVideoResolutionArrayInterface&) = delete;
    EpicRtcVideoResolutionArrayInterface& operator=(const EpicRtcVideoResolutionArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcVideoResolutionArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoResolutionArrayInterface() = default;
};

/**
 * A reference counted Array container that allows arrays to be copied over ABI and DLL boundary.
 */
class EpicRtcGenericFrameInfoArrayInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcGenericFrameInfoInterface* const* Get() const = 0;

    /**
     * Get the pointer to the data.
     * @return The data.
     */
    virtual EMRTC_API EpicRtcGenericFrameInfoInterface** Get() = 0;
    /**
     * Get the size of the array.
     * @return the size of the array.
     */
    virtual EMRTC_API uint64_t Size() const = 0;

    // Prevent copying
    EpicRtcGenericFrameInfoArrayInterface(const EpicRtcGenericFrameInfoArrayInterface&) = delete;
    EpicRtcGenericFrameInfoArrayInterface& operator=(const EpicRtcGenericFrameInfoArrayInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcGenericFrameInfoArrayInterface() = default;
    virtual EMRTC_API ~EpicRtcGenericFrameInfoArrayInterface() = default;
};

#pragma pack(pop)
