// Copyright Epic Games, Inc. All Rights Reserved.

#include "Extensions/UIComponent.h"

#include "CoreGlobals.h"
#include "Components/Widget.h"
#include "Blueprint/UserWidget.h"

bool UUIComponent::Initialize(UWidget* Target)
{
	Owner = Target;
	const bool Result = OnInitialize();

	if (!Result)
	{
		Owner = nullptr;
	}

	return Result;
}

void UUIComponent::Construct()
{
	OnConstruct();
}

void UUIComponent::Destruct()
{
	OnDestruct();
}

TWeakObjectPtr<UWidget> UUIComponent::GetOwner() const
{
	return Owner;
}

bool UUIComponent::OnInitialize()
{
	return true;
}

void UUIComponent::OnConstruct()
{

}

void UUIComponent::OnDestruct()
{

}
