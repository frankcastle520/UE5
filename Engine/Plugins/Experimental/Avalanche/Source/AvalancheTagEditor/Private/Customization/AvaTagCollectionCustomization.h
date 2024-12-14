// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class IPropertyHandle;
class SWidget;

class FAvaTagCollectionCustomization : public IDetailCustomization
{
public:
	//~ Begin IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder) override;
	//~ End IDetailCustomization

	TSharedRef<SWidget> BuildHeaderContent(const TSharedRef<IPropertyHandle>& InPropertyHandle);
};
