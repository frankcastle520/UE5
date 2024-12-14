// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

struct FRCSignatureProtocolAction;

class FRCSignatureProtocolActionCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ Begin IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder) override;
	//~ End IDetailCustomization

private:
	TArray<const FRCSignatureProtocolAction*, TInlineAllocator<1>> GetProtocolActions(IDetailLayoutBuilder& InDetailBuilder) const;
};
