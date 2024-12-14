// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundArrayNodes.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontend"

namespace Metasound
{
	namespace MetasoundArrayNodesPrivate
	{
		FNodeClassMetadata CreateArrayNodeClassMetadata(const FName& InDataTypeName, const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface, int32 MajorVersion, int32 MinorVersion, bool bIsDeprecated)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName{FName("Array"), InOperatorName, InDataTypeName},
				MajorVersion, 
				MinorVersion,
				InDisplayName, 
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{ METASOUND_LOCTEXT("ArrayCategory", "Array") },
				{ METASOUND_LOCTEXT("MetasoundArrayKeyword", "Array") },
				FNodeDisplayStyle{}, 
				bIsDeprecated
			};

			return Metadata;
		}
	}
}

#undef LOCTEXT_NAMESPACE
