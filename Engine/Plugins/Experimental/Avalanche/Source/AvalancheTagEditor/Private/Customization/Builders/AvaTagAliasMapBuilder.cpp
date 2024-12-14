// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaTagAliasMapBuilder.h"
#include "IDetailChildrenBuilder.h"

void FAvaTagAliasMapBuilder::GenerateChildContent(IDetailChildrenBuilder& InChildrenBuilder)
{
	ForEachChildProperty(
		[&InChildrenBuilder](const TSharedRef<IPropertyHandle>& InChildHandle)
		{
			InChildrenBuilder.AddProperty(InChildHandle);
		});
}
