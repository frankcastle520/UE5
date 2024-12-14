// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaSceneState.h"

#include "AvaNameAttribute.h"
#include "AvaSceneSettings.h"
#include "AvaTagHandle.h"
#include "AvaTagHandleContainer.h"
#include "Tags/AvaTagAttribute.h"
#include "Tags/AvaTagAttributeBase.h"

void UAvaSceneState::Initialize(UAvaSceneSettings* InSceneSettings)
{
	SceneAttributes.Reset();
	if (InSceneSettings)
	{
		SceneAttributes = InSceneSettings->GetSceneAttributes();
	}
}

bool UAvaSceneState::AddTagAttribute(const FAvaTagHandle& InTagHandle)
{
	if (!InTagHandle.IsValid())
	{
		return false;
	}

	// Return true if already existing
	if (ContainsTagAttribute(InTagHandle))
	{
		return true;
	}

	UAvaTagAttribute* TagAttribute = NewObject<UAvaTagAttribute>(this, NAME_None, RF_Transient);
	check(TagAttribute);
	TagAttribute->Tag = InTagHandle;

	SceneAttributes.Add(TagAttribute);
	return true;
}

bool UAvaSceneState::RemoveTagAttribute(const FAvaTagHandle& InTagHandle)
{
	uint32 TagsCleared = 0;

	for (UAvaAttribute* Attribute : SceneAttributes)
	{
		UAvaTagAttributeBase* TagAttribute = Cast<UAvaTagAttributeBase>(Attribute);
		if (!TagAttribute)
		{
			continue;
		}

		// Attempt to clear the given tag handle for the attribute. This will return true if it did remove the entry
		// Do not remove the attribute itself from the list as it could still have valid tags, or later have valid tags
		if (TagAttribute->ClearTagHandle(InTagHandle))
		{
			++TagsCleared;
		}
	}

	return TagsCleared > 0;
}

bool UAvaSceneState::ContainsTagAttribute(const FAvaTagHandle& InTagHandle) const
{
	return SceneAttributes.ContainsByPredicate(
		[&InTagHandle](UAvaAttribute* InAttribute)
		{
			UAvaTagAttributeBase* TagAttribute = Cast<UAvaTagAttributeBase>(InAttribute);
			return TagAttribute && TagAttribute->ContainsTag(InTagHandle);
		});
}

bool UAvaSceneState::AddNameAttribute(FName InName)
{
	if (InName.IsNone())
	{
		return false;
	}

	// Return true if already existing
	if (ContainsNameAttribute(InName))
	{
		return true;
	}

	UAvaNameAttribute* NameAttribute = NewObject<UAvaNameAttribute>(this, NAME_None, RF_Transient);
	check(NameAttribute);
	NameAttribute->Name = InName;

	SceneAttributes.Add(NameAttribute);
	return true;
}

bool UAvaSceneState::RemoveNameAttribute(FName InName)
{
	uint32 NamesCleared = 0;

	for (TArray<TObjectPtr<UAvaAttribute>>::TIterator Iter(SceneAttributes); Iter; ++Iter)
	{
		UAvaNameAttribute* NameAttribute = Cast<UAvaNameAttribute>(*Iter);
		if (!NameAttribute || NameAttribute->Name != InName)
		{
			continue;
		}

		// Attempt to clear the given name for the attribute.
		// If this owns the attribute, remove from the Scene Attributes list
		if (NameAttribute->GetOuter() == this)
		{
			Iter.RemoveCurrent();
		}
		// Do not remove the attribute itself from the list if it's an external attribute as it could still have valid tags,
		// or later have valid tags (due to a dynamic change)
		else
		{
			NameAttribute->Name = NAME_None;
		}

		++NamesCleared;
	}

	return NamesCleared > 0;
}

bool UAvaSceneState::ContainsNameAttribute(FName InName) const
{
	return SceneAttributes.ContainsByPredicate(
		[InName](UAvaAttribute* InAttribute)
		{
			UAvaNameAttribute* NameAttribute = Cast<UAvaNameAttribute>(InAttribute);
			return NameAttribute && NameAttribute->Name == InName;
		});
}
