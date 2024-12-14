// Copyright Epic Games, Inc. All Rights Reserved.

#include "TickableEditorObject.h"
#include "Tickable.h"

FTickableObjectBase::FTickableStatics& FTickableEditorObject::GetStatics()
{
	static FTickableStatics Singleton;
	return Singleton;
}

FTickableObjectBase::FTickableStatics& FTickableCookObject::GetStatics()
{
	static FTickableStatics Singleton;
	return Singleton;
}

FTickableObjectBase* FTickableEditorObject::ObjectBeingTicked = nullptr;
FTickableObjectBase* FTickableCookObject::ObjectBeingTicked = nullptr;

