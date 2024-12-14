// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserInitializer.h"

#include "AnimNode_ChooserPlayer.h"
#include "Chooser.h"
#include "ChooserPropertyAccess.h"
#include "ObjectChooserWidgetFactories.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimInstance.h"

#define LOCTEXT_NAMESPACE "ChooserInitializer"


void FGenericChooserInitializer::Initialize(UChooserTable* Chooser) const
{
	Chooser->ContextData = ContextData;
	Chooser->ResultType = ResultType;
	Chooser->OutputObjectType = OutputObjectType;
}

void FChooserPlayerInitializer::Initialize(UChooserTable* Chooser) const
{
	Chooser->ContextData.SetNum(2);
	Chooser->ContextData[0].InitializeAs(FContextObjectTypeClass::StaticStruct());
	FContextObjectTypeClass& ClassData = Chooser->ContextData[0].GetMutable<FContextObjectTypeClass>();
	if (AnimClass)
	{
		ClassData.Class = AnimClass;		
	}
	else
	{
		ClassData.Class = UAnimInstance::StaticClass();
	}
	ClassData.Direction = EContextObjectDirection::ReadWrite;
	
	Chooser->ContextData[1].InitializeAs(FContextObjectTypeStruct::StaticStruct());
	FContextObjectTypeStruct& StructData = Chooser->ContextData[1].GetMutable<FContextObjectTypeStruct>();
	StructData.Struct = FChooserPlayerSettings::StaticStruct();
	StructData.Direction = EContextObjectDirection::Write;
	
	Chooser->OutputObjectType = UAnimationAsset::StaticClass();
}


#undef LOCTEXT_NAMESPACE
