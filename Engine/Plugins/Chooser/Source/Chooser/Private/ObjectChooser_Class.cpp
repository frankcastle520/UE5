// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectChooser_Class.h"

UObject* FClassChooser::ChooseObject(FChooserEvaluationContext& Context) const
{
	return Class;
}

FObjectChooserBase::EIteratorStatus FClassChooser::IterateObjects(FObjectChooserIteratorCallback Callback) const
{
	return Callback.Execute(Class);
}
