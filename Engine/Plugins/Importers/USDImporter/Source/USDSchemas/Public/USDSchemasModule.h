// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UsdWrappers/ForwardDeclarations.h"

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FUsdRenderContextRegistry;
class FUsdSchemaTranslatorRegistry;

class IUsdSchemasModule : public IModuleInterface
{
public:
	virtual FUsdSchemaTranslatorRegistry& GetTranslatorRegistry() = 0;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual FUsdRenderContextRegistry& GetRenderContextRegistry() = 0;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

namespace UsdUnreal::Analytics
{
	/** Collects analytics about custom schemas, unsupported native schemas, and the count of custom registered schema translators */
	USDSCHEMAS_API void CollectSchemaAnalytics(const UE::FUsdStage& Stage, const FString& EventName);
}
