// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "MuT/NodeSurfaceNew.h"

class UCustomizableObjectNodeGroupProjectorParameter;
struct FGroupProjectorTempData;
class UCustomizableObjectNodeModifierExtendMeshSection;
class FString;
class UCustomizableObjectNodeMaterial;
class UCustomizableObjectNodeMaterialBase;
class UCustomizableObjectNodeObjectGroup;
class UEdGraphPin;
class UTexture2D;
struct FMutableGraphGenerationContext;


mu::NodeImagePtr GenerateMutableSourceGroupProjector(const int32 NodeLOD, const int32 ImageIndex, mu::NodeMeshPtr MeshNode, FMutableGraphGenerationContext& GenerationContext,
	UCustomizableObjectNodeMaterialBase* TypedNodeMat, UCustomizableObjectNodeModifierExtendMeshSection* TypedNodeExt, bool& bShareProjectionTexturesBetweenLODs, bool& bIsGroupProjectorImage,
	UTexture2D*& GroupProjectionReferenceTexture, TMap<FString, float>& TextureNameToProjectionResFactor, FString& AlternateResStateName);


TOptional<FGroupProjectorTempData> GenerateMutableGroupProjector(UCustomizableObjectNodeGroupProjectorParameter* ProjParamNode, FMutableGraphGenerationContext& GenerationContext);

