// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

namespace rl4 {

/**
    @brief Implementation type for RigLogic calculations.
*/
enum class CalculationType : std::uint8_t {
    Scalar,  ///< scalar CPU algorithm
    SSE,  ///< vectorized (SSE) CPU algorithm
    AVX,  ///< vectorized (AVX) CPU algorithm (RigLogic must be built with AVX support,
          ///< otherwise it falls back to using the Scalar version)
    NEON,  ///< vectorized (NEON) CPU algorithm (RigLogic must be built with NEON support,
           ///< otherwise it falls back to using the Scalar version)
    AnyVector  ///< Pick any available vectorization
};

/**
    @brief Translation type to be used by RigLogic.
*/
enum class TranslationType : std::uint8_t {
    Vector = 3,
};

/**
    @brief Rotation type to be used by RigLogic.
*/
enum class RotationType : std::uint8_t {
    EulerAngles = 3,
    Quaternions = 4
};

/**
    @brief Rotation order to use when converting Euler angles to/from quaternions.
*/
enum class RotationOrder : std::uint8_t {
    XYZ,
    XZY,
    YXZ,
    YZX,
    ZXY,
    ZYX
};

/**
    @brief Scale type to be used by RigLogic.
*/
enum class ScaleType : std::uint8_t {
    Vector = 3,
};

struct Configuration {
    CalculationType calculationType = CalculationType::SSE;
    bool loadJoints = true;
    bool loadBlendShapes = true;
    bool loadAnimatedMaps = true;
    bool loadMachineLearnedBehavior = true;
    bool loadRBFBehavior = true;
    bool loadTwistSwingBehavior = true;
    TranslationType translationType = TranslationType::Vector;
    RotationType rotationType = RotationType::EulerAngles;
    RotationOrder rotationOrder = RotationOrder::XYZ;
    ScaleType scaleType = ScaleType::Vector;
};

}  // namespace rl4
