// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Shader/ShaderTypes.h"

struct FExpressionInput;
struct FExpressionOutput;
class UMaterialExpression;

/**
 * @brief A structure that holds reflection information about a material.
 * This structure is typically populated by the material translator as a side product of
 * the translation process itself.
 *
 * You can use these insights for things like providing semantic colouring
 * of the graph UI or accurately knowing what resources are referenced by the translated
 * materials.
 */
struct FMaterialInsights
{
    /**
     * @brief Nested structure that represents a single connection insight.
     */
    struct FConnectionInsight
    {
        const UObject* InputObject;                   ///< Pointer to the input object of the connection.
        const UMaterialExpression* OutputExpression;  ///< Pointer to the output expression of the connection.
        int InputIndex;                               ///< Index of the input in the connection.
        int OutputIndex;                              ///< Index of the output in the connection.
        UE::Shader::EValueType ValueType;             ///< Type of the value flowing through the connection.
    };

    TArray<FConnectionInsight> ConnectionInsights;    ///< Array of connection insights.

    FMaterialInsights();

	/// Clears all insight data.
    void Empty();
};