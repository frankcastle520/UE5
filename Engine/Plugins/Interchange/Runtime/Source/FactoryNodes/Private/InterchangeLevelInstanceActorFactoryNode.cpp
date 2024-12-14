// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeLevelInstanceActorFactoryNode.h"

bool UInterchangeLevelInstanceActorFactoryNode::GetCustomLevelReference(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(LevelReference, FString);
}

bool UInterchangeLevelInstanceActorFactoryNode::SetCustomLevelReference(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(LevelReference, FString);
}
