// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Widgets/SToolTip.h"

class SAssetThumbnail;

// AssetThumbnail ToolTip, Implementation is inside AssetThumbnail.cpp
class SAssetThumbnailToolTip : public SToolTip
{
public:
	SLATE_BEGIN_ARGS(SAssetThumbnailToolTip)
		: _AssetThumbnail()
	{ }

	SLATE_ARGUMENT(TSharedPtr<SAssetThumbnail>, AssetThumbnail)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// IToolTip interface
	virtual bool IsEmpty() const override;

	virtual void OnOpening() override;

	virtual void OnClosed() override;

private:
	TWeakPtr<SAssetThumbnail> AssetThumbnail;
};
