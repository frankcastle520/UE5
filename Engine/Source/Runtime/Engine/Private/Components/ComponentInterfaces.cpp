// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/ComponentInterfaces.h"
#include "Components/ComponentInterfaceIterator.h"
#include "HAL/IConsoleManager.h"
#include "UnrealEngine.h"

TArray<FComponentInterfaceImplementation> IPrimitiveComponent::Implementers;

void IPrimitiveComponent::AddImplementer(const FComponentInterfaceImplementation& Implementer)
{	
	Implementers.Add(Implementer);

}

TArray<FComponentInterfaceImplementation> IStaticMeshComponent::Implementers;

void IStaticMeshComponent::AddImplementer(const FComponentInterfaceImplementation& Implementer)
{
	Implementers.Add(Implementer);
}