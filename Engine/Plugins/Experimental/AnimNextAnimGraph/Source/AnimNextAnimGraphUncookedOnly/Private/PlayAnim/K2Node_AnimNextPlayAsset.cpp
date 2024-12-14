// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayAnim/K2Node_AnimNextPlayAsset.h"

#include "PlayAnim/PlayAnimCallbackProxy.h"

#define LOCTEXT_NAMESPACE "K2Node_AnimNextPlayAsset"

UK2Node_AnimNextPlayAsset::UK2Node_AnimNextPlayAsset()
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UPlayAnimCallbackProxy, CreateProxyObjectForPlayAsset);
	ProxyFactoryClass = UPlayAnimCallbackProxy::StaticClass();
	ProxyClass = UPlayAnimCallbackProxy::StaticClass();
}

FText UK2Node_AnimNextPlayAsset::GetTooltipText() const
{
	return LOCTEXT("K2Node_PlayAsset_Tooltip", "Plays an asset on an AnimNextComponent");
}

FText UK2Node_AnimNextPlayAsset::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("PlayAsset", "Play Asset");
}

FText UK2Node_AnimNextPlayAsset::GetMenuCategory() const
{
	return LOCTEXT("PlayAssetCategory", "Animation|AnimNext");
}

#undef LOCTEXT_NAMESPACE
