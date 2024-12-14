// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/PlatformMisc.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::MetaHuman
{
struct FMetaHumanAssetImportDescription;

namespace TestUtils
{
	/**
	 * Add the necessary latent commands to validate a MetaHuman of the given name
	 * @param InImportDescription an import description describing the MetaHuman to be validate
	 */
	METAHUMANSDKEDITOR_API void AddValidateMetaHumanLatentCommands(const FMetaHumanAssetImportDescription& InImportDescription);
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
