// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeActorFactoryNode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeActorFactoryNode)

#if WITH_ENGINE
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#endif

UClass* UInterchangeActorFactoryNode::GetObjectClass() const
{
#if WITH_ENGINE
	FString ActorClassName;
	if (GetCustomActorClassName(ActorClassName))
	{
		UClass* ActorClass = FindObject<UClass>(nullptr, *ActorClassName);
		if (ActorClass->IsChildOf<AActor>())
		{
			return ActorClass;
		}
	}

	return AActor::StaticClass();
#else
	return nullptr;
#endif
}

bool UInterchangeActorFactoryNode::GetCustomGlobalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(GlobalTransform, FTransform);
}

bool UInterchangeActorFactoryNode::SetCustomGlobalTransform(const FTransform& AttributeValue, bool bAddApplyDelegate)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_WITH_CUSTOM_DELEGATE_WITH_CUSTOM_CLASS(UInterchangeActorFactoryNode, GlobalTransform, FTransform, USceneComponent);
}

bool UInterchangeActorFactoryNode::GetCustomLocalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(GlobalTransform, FTransform);
}

bool UInterchangeActorFactoryNode::SetCustomLocalTransform(const FTransform& AttributeValue, bool bAddApplyDelegate)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_WITH_CUSTOM_DELEGATE_WITH_CUSTOM_CLASS(UInterchangeActorFactoryNode, GlobalTransform, FTransform, USceneComponent);
}

bool UInterchangeActorFactoryNode::ApplyCustomGlobalTransformToAsset(UObject* Asset) const
{
	FTransform LocalTransform;
	if (GetCustomLocalTransform(LocalTransform))
	{
		if (USceneComponent* Component = Cast<USceneComponent>(Asset))
		{
			Component->SetRelativeTransform(LocalTransform);
			return true;
		}
	}

	FTransform GlobalTransform;
	if (GetCustomGlobalTransform(GlobalTransform))
	{
		if (USceneComponent* Component = Cast<USceneComponent>(Asset))
		{
			Component->SetWorldTransform(GlobalTransform);
			return true;
		}
	}

	return false;
}

bool UInterchangeActorFactoryNode::FillCustomGlobalTransformFromAsset(UObject* Asset)
{
	if (const USceneComponent* Component = Cast<USceneComponent>(Asset))
	{
		FTransform LocalTransform = Component->GetRelativeTransform();
		FTransform GlobalTransform = Component->GetComponentToWorld();

		return this->SetCustomLocalTransform(LocalTransform, false) || this->SetCustomGlobalTransform(GlobalTransform, false);
	}

	return false;
}

void UInterchangeActorFactoryNode::CopyWithObject(const UInterchangeFactoryBaseNode* SourceNode, UObject* Object)
{
	Super::CopyWithObject(SourceNode, Object);

	if (const UInterchangeActorFactoryNode* ActorFactoryNode = Cast<UInterchangeActorFactoryNode>(SourceNode))
	{
		COPY_NODE_DELEGATES_WITH_CUSTOM_DELEGATE(ActorFactoryNode, UInterchangeActorFactoryNode, GlobalTransform, FTransform, USceneComponent::StaticClass())
	}
}

UInterchangeActorFactoryNode::UInterchangeActorFactoryNode()
{
	LayerNames.Initialize(Attributes.ToSharedRef(), TEXT("__LayerNames__"));
	Tags.Initialize(Attributes.ToSharedRef(), TEXT("__Tags__"));
}

void UInterchangeActorFactoryNode::GetLayerNames(TArray<FString>& OutLayerNames) const
{
	LayerNames.GetItems(OutLayerNames);
}

bool UInterchangeActorFactoryNode::AddLayerName(const FString& LayerName)
{
	return LayerNames.AddItem(LayerName);
}

bool UInterchangeActorFactoryNode::AddLayerNames(const TArray<FString>& InLayerNames)
{
	bool bSuccess = true;
	for (const FString& LayerName : InLayerNames)
	{
		bSuccess &= LayerNames.AddItem(LayerName);
	}

	return bSuccess;
}

bool UInterchangeActorFactoryNode::RemoveLayerName(const FString& LayerName)
{
	return LayerNames.RemoveItem(LayerName);
}

void UInterchangeActorFactoryNode::GetTags(TArray<FString>& OutTags) const
{
	Tags.GetItems(OutTags);
}

bool UInterchangeActorFactoryNode::AddTag(const FString& Tag)
{
	return Tags.AddItem(Tag);
}

bool UInterchangeActorFactoryNode::AddTags(const TArray<FString>& InTags)
{
	bool bSuccess = true;
	for (const FString& Tag : InTags)
	{
		bSuccess &= Tags.AddItem(Tag);
	}

	return bSuccess;
}

bool UInterchangeActorFactoryNode::RemoveTag(const FString& Tag)
{
	return Tags.RemoveItem(Tag);
}