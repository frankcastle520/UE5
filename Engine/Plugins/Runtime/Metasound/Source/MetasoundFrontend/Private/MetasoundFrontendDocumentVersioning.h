// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/CoreMiscDefines.h"

#if WITH_EDITORONLY_DATA
#include "MetasoundFrontendDocument.h"


// Forward Declartions
class IMetaSoundDocumentInterface;
struct FMetaSoundFrontendDocumentBuilder;


namespace Metasound::Frontend
{
	static FMetasoundFrontendVersionNumber GetMaxDocumentVersion()
	{
		return FMetasoundFrontendVersionNumber { 1, 14 };
	}

	// Versions Frontend Document. Passed as AssetBase for backward compat to
	// version asset documents predating the IMetaSoundDocumentInterface
	bool VersionDocument(FMetaSoundFrontendDocumentBuilder& Builder);
} // namespace Metasound::Frontend
#endif // WITH_EDITORONLY_DATA