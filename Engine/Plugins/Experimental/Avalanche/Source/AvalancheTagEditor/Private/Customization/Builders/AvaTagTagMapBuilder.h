// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTagMapBuilder.h"

class FAvaTagElementHelper;

/** Map builder for the Tag Map */
class FAvaTagTagMapBuilder : public FAvaTagMapBuilder
{
public:
	explicit FAvaTagTagMapBuilder(const TSharedRef<IPropertyHandle>& InMapProperty);

	//~ Begin IDetailCustomNodeBuilder
	virtual void GenerateChildContent(IDetailChildrenBuilder& InChildrenBuilder) override;
	//~ End IDetailCustomNodeBuilder

private:
	TSharedRef<FAvaTagElementHelper> TagElementHelper;
};
