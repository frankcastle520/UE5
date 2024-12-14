// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGeneratedNavLinksProxy.h"

UBaseGeneratedNavLinksProxy::UBaseGeneratedNavLinksProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UBaseGeneratedNavLinksProxy::GetLinkData(FVector& LeftPt, FVector& RightPt, ENavLinkDirection::Type& Direction) const
{
	ensureMsgf(false, TEXT("Should not be called on a generated navlink proxy since it's not representing a single link."));
}

FNavLinkId UBaseGeneratedNavLinksProxy::GetId() const
{
	return LinkProxyId;
}

void UBaseGeneratedNavLinksProxy::UpdateLinkId(FNavLinkId ProxyId)
{
	LinkProxyId = ProxyId;
}

UObject* UBaseGeneratedNavLinksProxy::GetLinkOwner() const
{
	return Owner;
}
