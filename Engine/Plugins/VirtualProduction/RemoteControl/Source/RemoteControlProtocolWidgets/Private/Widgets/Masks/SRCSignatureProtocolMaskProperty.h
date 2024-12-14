// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Masks/SRCProtocolMaskTriplet.h"

class IPropertyHandle;
struct FRCSignatureProtocolAction;

class SRCSignatureProtocolMaskProperty : public SRCProtocolMaskTriplet
{
public:
	SLATE_BEGIN_ARGS(SRCSignatureProtocolMaskProperty) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs
		, const TSharedRef<IPropertyHandle>& InMaskPropertyHandle
		, TConstArrayView<const FRCSignatureProtocolAction*> InProtocolActions);

private:
	//~ Begin SRCProtocolMaskTriplet
	virtual ECheckBoxState IsMaskEnabled(ERCMask InMaskBit) const override;
	virtual void SetMaskEnabled(ECheckBoxState InNewState, ERCMask InNewMaskBit) override;
	//~ End SRCProtocolMaskTriplet

	TSharedPtr<IPropertyHandle> MaskPropertyHandle;
};
