// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "HAL/Platform.h"
#include "MaterialDomain.h"
#include "SceneTypes.h"

struct FDMMaterialUtils
{
	struct FParams
	{
		EMaterialProperty Property;
		EMaterialDomain Domain;
		EBlendMode BlendMode;
		FMaterialShadingModelField ShadingModels;
		ETranslucencyLightingMode TranslucencyLightingMode;
		bool bIsTessellationEnabled;
		bool bBlendableOutputAlpha;
		bool bUsesDistortion;
		bool bUsesShadingModelFromMaterialExpression;
		bool bIsTranslucencyWritingVelocity;
		bool bIsThinSurface;
		bool bIsSupported;
	};

	static bool IsMaterialPropertyActive(const FParams& InParams);		

protected:
	static bool IsMaterialPropertyActive_PostProcess(const FParams& InParams, bool bSubstrateEnabled, bool bSubstrateOpacityOverrideAllowed);

	static bool IsMaterialPropertyActive_LightFunction(const FParams& InParams, bool bSubstrateEnabled, bool bSubstrateOpacityOverrideAllowed);

	static bool IsMaterialPropertyActive_DeferredDecal(const FParams& InParams, bool bSubstrateEnabled, bool bSubstrateOpacityOverrideAllowed);

	static bool IsMaterialPropertyActive_Volume(const FParams& InParams, bool bSubstrateEnabled, bool bSubstrateOpacityOverrideAllowed);

	static bool IsMaterialPropertyActive_UI(const FParams& InParams, bool bSubstrateEnabled, bool bSubstrateOpacityOverrideAllowed);

	static bool IsMaterialPropertyActive_Surface(const FParams& InParams, bool bSubstrateEnabled, bool bSubstrateOpacityOverrideAllowed);
};
