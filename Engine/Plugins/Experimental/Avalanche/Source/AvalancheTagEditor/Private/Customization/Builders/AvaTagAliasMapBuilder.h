// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTagMapBuilder.h"

/** Map builder for the Alias Map */
class FAvaTagAliasMapBuilder : public FAvaTagMapBuilder
{
public:
	explicit FAvaTagAliasMapBuilder(const TSharedRef<IPropertyHandle>& InMapProperty)
		: FAvaTagMapBuilder(InMapProperty)
	{
	}

	//~ Begin IDetailCustomNodeBuilder
	virtual void GenerateChildContent(IDetailChildrenBuilder& InChildrenBuilder) override;
	//~ End IDetailCustomNodeBuilder
};
