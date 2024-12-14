// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundSettings.h"

#include "Algo/Count.h"
#include "HAL/IConsoleManager.h"
#include "MetasoundFrontendDocument.h"

#define LOCTEXT_NAMESPACE "MetaSound"


namespace Metasound::SettingsPrivate
{
#if WITH_EDITOR
	template<typename SettingsStructType>
	TSet<FName> GetStructNames(const TArray<SettingsStructType>& InSettings, int32 IgnoreIndex = INDEX_NONE)
	{
		TSet<FName> Names;
		for (int32 Index = 0; Index < InSettings.Num(); ++Index)
		{
			if (Index != IgnoreIndex)
			{
				Names.Add(InSettings[Index].Name);
			}
		}
		return Names;
	}

	/** Generate new name for the item. **/
	static FName GenerateUniqueName(const TSet<FName>& Names, const TCHAR* InBaseName)
	{
		FString NewName = InBaseName;
		for (int32 Postfix = 1; Names.Contains(*NewName); ++Postfix)
		{
			NewName = FString::Format(TEXT("{0}_{1}"), { InBaseName, Postfix });
		}
		return FName(*NewName);
	}

	template<typename SettingsStructType>
	void OnCreateNewSettingsStruct(const TArray<SettingsStructType>& InSettings, const FString& InBaseName, SettingsStructType& OutNewItem)
	{
		const TSet<FName> Names = GetStructNames(InSettings);
		OutNewItem.Name = GenerateUniqueName(Names, *InBaseName);
		OutNewItem.UniqueId = FGuid::NewGuid();
	}

	template<typename SettingsStructType>
	void OnRenameSettingsStruct(const TArray<SettingsStructType>& InSettings, int32 Index, const FString& InBaseName, SettingsStructType& OutRenamed)
	{
		if (OutRenamed.Name.IsNone())
		{
			const TSet<FName> Names = GetStructNames(InSettings);
			OutRenamed.Name = GenerateUniqueName(Names, *InBaseName);
		}
		else
		{
			const TSet<FName> Names = GetStructNames(InSettings, Index);
			if (Names.Contains(OutRenamed.Name))
			{
				OutRenamed.Name = GenerateUniqueName(Names, *OutRenamed.Name.ToString());
			}
		}
	}
#endif // WITH_EDITOR

	template<typename SettingsStructType>
	const SettingsStructType* FindSettingsStruct(const TArray<SettingsStructType>& Settings, const FGuid& InUniqueID)
	{
		auto MatchesIDPredicate = [&InUniqueID](const SettingsStructType& Struct) { return Struct.UniqueId == InUniqueID; };
		return Settings.FindByPredicate(MatchesIDPredicate);
	}

	template<typename SettingsStructType>
	const SettingsStructType* FindSettingsStruct(const TArray<SettingsStructType>& Settings, FName Name)
	{
		auto MatchesNamePredicate = [Name](const SettingsStructType& Struct) { return Struct.Name == Name; };
		return Settings.FindByPredicate(MatchesNamePredicate);
	}

#if WITH_EDITOR
	template<typename SettingsStructType>
	void PostEditChainChangedStructMember(FPropertyChangedChainEvent& PostEditChangeChainProperty, TArray<SettingsStructType>& StructSettings, FName PropertyName, const FString& NewItemName)
	{
		const int32 ItemIndex = PostEditChangeChainProperty.GetArrayIndex(PropertyName.ToString());

		if (TDoubleLinkedList<FProperty*>::TDoubleLinkedListNode* HeadNode = PostEditChangeChainProperty.PropertyChain.GetHead())
		{
			const FProperty* Prop = HeadNode->GetValue();
			if (Prop->GetName() != PropertyName)
			{
				return;
			}
		}

		// Item changed..
		if (ItemIndex != INDEX_NONE && StructSettings.IsValidIndex(ItemIndex))
		{
			SettingsStructType& Item = StructSettings[ItemIndex];
			if (PostEditChangeChainProperty.GetPropertyName() == "Name")
			{
				OnRenameSettingsStruct<SettingsStructType>(StructSettings, ItemIndex, NewItemName, Item);
			}
			else if (PostEditChangeChainProperty.GetPropertyName() == PropertyName)
			{
				// Array change add or duplicate
				if (PostEditChangeChainProperty.ChangeType == EPropertyChangeType::ArrayAdd
					|| PostEditChangeChainProperty.ChangeType == EPropertyChangeType::Duplicate)
				{
					OnCreateNewSettingsStruct<SettingsStructType>(StructSettings, NewItemName, Item);
				}
			}
		}

		// Handle pasting separately as we might not have a valid index in the case of pasting when array is empty.
		if (PostEditChangeChainProperty.GetPropertyName() == PropertyName)
		{
			// Paste...
			if (PostEditChangeChainProperty.ChangeType == EPropertyChangeType::ValueSet)
			{
				const int32 IndexOfPastedItem = ItemIndex != INDEX_NONE ? ItemIndex : 0;
				if (StructSettings.IsValidIndex(IndexOfPastedItem))
				{
					SettingsStructType& Item = StructSettings[IndexOfPastedItem];
					OnCreateNewSettingsStruct<SettingsStructType>(StructSettings, NewItemName, Item);
				}
			}
		}
	}
#endif // WITH_EDITOR
} // namespace Metasound::SettingsPrivate

#if WITH_EDITOR
bool FMetaSoundPageSettings::GetExcludeFromCook(FName PlatformName) const
{
	if (PlatformCanTargetPage(PlatformName))
	{
		return false;
	}

	return ExcludeFromCook.GetValueForPlatform(PlatformName);
}

TArray<FName> FMetaSoundPageSettings::GetTargetPlatforms() const
{
	TArray<FName> PlatformNames;
	Algo::TransformIf(CanTarget.PerPlatform, PlatformNames,
		[](const TPair<FName, bool>& Pair) { return Pair.Value; },
		[](const TPair<FName, bool>& Pair) { return Pair.Key; });
	return PlatformNames;
}

bool FMetaSoundPageSettings::PlatformCanTargetPage(FName PlatformName) const
{
	const bool bIsTargeted = CanTarget.GetValueForPlatform(PlatformName);
	return bIsTargeted;
}

void UMetaSoundSettings::ConformPageSettings(bool bNotifyDefaultRenamed)
{
	using namespace Metasound;

	DefaultPageSettings.UniqueId = Metasound::Frontend::DefaultPageID;
	DefaultPageSettings.Name = Metasound::Frontend::DefaultPageName;
	DefaultPageSettings.bIsDefaultPage = true;
	DefaultPageSettings.ExcludeFromCook = false;

	bool bInvalidDefaultRenamed = false;
	TMap<FName, bool> PlatformHasTarget;
	auto GatherPlatformTargets = [&PlatformHasTarget](const FMetaSoundPageSettings& Page)
	{
		PlatformHasTarget.FindOrAdd({ }) |= Page.CanTarget.Default;
		for (const TPair<FName, bool>& Pair : Page.CanTarget.PerPlatform)
		{
			PlatformHasTarget.FindOrAdd(Pair.Key) |= Pair.Value;
		}
	};

	GatherPlatformTargets(DefaultPageSettings);
	for (FMetaSoundPageSettings& Page : PageSettings)
	{
		const bool bIsDefaultName = Page.Name == Frontend::DefaultPageName;
		if (bIsDefaultName)
		{
			const TSet<FName> PageNames(GetPageNames());
			Page.Name = SettingsPrivate::GenerateUniqueName(PageNames, *Page.Name.ToString());
			bInvalidDefaultRenamed = true;
		}

		GatherPlatformTargets(Page);

		Page.bIsDefaultPage = false;
	}

	// Forces each platform to target at least one page setting.
 	for (const TPair<FName, bool>& Pair : PlatformHasTarget)
 	{
 		if (!Pair.Value)
 		{
 			if (Pair.Key.IsNone())
 			{
 				DefaultPageSettings.CanTarget.Default = true;
 			}
 			else
 			{
 				DefaultPageSettings.CanTarget.PerPlatform.FindOrAdd(Pair.Key) = true;
 			}
 		}
 	}

#if WITH_EDITORONLY_DATA
	{
		FScopeLock Lock(&CookPlatformTargetCritSec);
		CookPlatformTargetPageIDs.Reset();
		CookPlatformTargetPage = { };
	}
#endif // WITH_EDITORONLY_DATA

	TargetPageNameOverride.Reset();

	for (int32 Index = PageSettings.Num() - 1; Index >= 0; --Index)
	{
		FMetaSoundPageSettings& PageSetting = PageSettings[Index];
		if (PageSetting.UniqueId == Metasound::Frontend::DefaultPageID || PageSetting.Name == Metasound::Frontend::DefaultPageName)
		{
			PageSettings.RemoveAt(Index);
		}
	}

	if (bNotifyDefaultRenamed && bInvalidDefaultRenamed)
	{
		OnDefaultRenamed.Broadcast();
	}
}
#endif // WITH_EDITOR

const FMetaSoundPageSettings* UMetaSoundSettings::FindPageSettings(FName Name) const
{
	if (Name == Metasound::Frontend::DefaultPageName)
	{
		return &GetDefaultPageSettings();
	}
	return Metasound::SettingsPrivate::FindSettingsStruct(PageSettings, Name);
}

const FMetaSoundPageSettings* UMetaSoundSettings::FindPageSettings(const FGuid& InPageID) const
{
	if (InPageID == Metasound::Frontend::DefaultPageID)
	{
		return &GetDefaultPageSettings();
	}
	return Metasound::SettingsPrivate::FindSettingsStruct(PageSettings, InPageID);
}

const FMetaSoundQualitySettings* UMetaSoundSettings::FindQualitySettings(FName Name) const
{
	return Metasound::SettingsPrivate::FindSettingsStruct(QualitySettings, Name);
}

const FMetaSoundQualitySettings* UMetaSoundSettings::FindQualitySettings(const FGuid& InQualityID) const
{
	return Metasound::SettingsPrivate::FindSettingsStruct(QualitySettings, InQualityID);
}

const FMetaSoundPageSettings& UMetaSoundSettings::GetDefaultPageSettings() const
{
	return DefaultPageSettings;
}

#if WITH_EDITOR
TArray<FName> UMetaSoundSettings::GetAllPlatformNamesImplementingTargets() const
{
	TSet<FName> PlatformNames;
	IteratePageSettings([&](const FMetaSoundPageSettings& PageSetting)
	{
		TArray<FName> PagePlatforms = PageSetting.GetTargetPlatforms();
		PlatformNames.Append(MoveTemp(PagePlatforms));
	});
	return PlatformNames.Array();
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
TArray<FGuid> UMetaSoundSettings::GetCookedTargetPageIDs(FName PlatformName) const
{
	FScopeLock Lock(&CookPlatformTargetCritSec);
	return GetCookedTargetPageIDsInternal(PlatformName);
}

const TArray<FGuid>& UMetaSoundSettings::GetCookedTargetPageIDsInternal(FName PlatformName) const
{
	if (PlatformName != CookPlatformTargetPage)
	{
		CookPlatformTargetPage = PlatformName;
		CookPlatformTargetPageIDs.Reset();
		auto CanTargetPage = [&PlatformName](const FMetaSoundPageSettings& PageSetting)
		{
#if WITH_EDITOR
			return PageSetting.PlatformCanTargetPage(PlatformName);
#else // !WITH_EDITOR
			return true;
#endif // !WITH_EDITOR
		};

		auto GetID = [](const FMetaSoundPageSettings& PageSetting)
		{
			return PageSetting.UniqueId;
		};

		if (CanTargetPage(DefaultPageSettings))
		{
			CookPlatformTargetPageIDs.Add(DefaultPageSettings.UniqueId);
		}

		Algo::TransformIf(PageSettings, CookPlatformTargetPageIDs, CanTargetPage, GetID);


		if (CookPlatformTargetPageIDs.IsEmpty())
		{
#if WITH_EDITOR
			const bool bCanTargetDefault = DefaultPageSettings.CanTarget.GetValueForPlatform(PlatformName);
#else // !WITH_EDITOR
			const bool bCanTargetDefault = DefaultPageSettings.CanTarget.GetValue();
#endif // !WITH_EDITOR

			if (!PageSettings.IsEmpty() && !bCanTargetDefault)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("No pages set to be targeted for platform '%s', forcing 'Default' page as target"), *PlatformName.ToString());
			}

			CookPlatformTargetPageIDs.Add(Metasound::Frontend::DefaultPageID);
		}
	}

	return CookPlatformTargetPageIDs;
}

void UMetaSoundSettings::IterateCookedTargetPageIDs(FName PlatformName, TFunctionRef<void(const FGuid&)> Iter) const
{
	FScopeLock Lock(&CookPlatformTargetCritSec);
	const TArray<FGuid>& CookPlatforms = GetCookedTargetPageIDsInternal(PlatformName);
	for (const FGuid& CookPlatform : CookPlatforms)
	{
		Iter(CookPlatform);
	}
}
#endif // WITH_EDITORONLY_DATA

const FMetaSoundPageSettings& UMetaSoundSettings::GetTargetPageSettings() const
{
	const FName TargetPage = TargetPageNameOverride.IsSet() ? *TargetPageNameOverride : TargetPageName;

#if !NO_LOGGING
	auto WarnIfUninitialized = [this](const FMetaSoundPageSettings& SettingsSet)
	{
		if (bWarnAccessBeforeInit)
		{
			UE_LOG(LogMetaSound, Display, TEXT("Target Page Settings accessed prior to 'PostInitProperties' being called.  Uninitialized PageSettings '%s' being returned."), *SettingsSet.Name.ToString());
			bWarnAccessBeforeInit = false;
		}
	};
#endif // !NO_LOGGING

	if (const FMetaSoundPageSettings* TargetSettings = FindPageSettings(TargetPage))
	{
#if !NO_LOGGING
		WarnIfUninitialized(*TargetSettings);
#endif // !NO_LOGGING
		return *TargetSettings;
	}

	// Shouldn't hit this, but if for some reason the target page is in a bad state,
	// try and return any page setting set as a valid target.
	for (const FMetaSoundPageSettings& Setting : PageSettings)
	{
#if !NO_LOGGING
		WarnIfUninitialized(Setting);
#endif // !NO_LOGGING
		return Setting;
	}

#if !NO_LOGGING
	WarnIfUninitialized(DefaultPageSettings);
#endif // !NO_LOGGING
	return DefaultPageSettings;
}

#if WITH_EDITOR
void UMetaSoundSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PostEditChangeChainProperty)
{
	using namespace Metasound::SettingsPrivate;

	PostEditChainChangedStructMember(PostEditChangeChainProperty, PageSettings, GetPageSettingPropertyName(), TEXT("New Page"));
	PostEditChainChangedStructMember(PostEditChangeChainProperty, QualitySettings, GetQualitySettingPropertyName(), TEXT("New Quality"));

	constexpr bool bNotifyDefaultRenamed = true;
	ConformPageSettings(bNotifyDefaultRenamed);

	Super::PostEditChangeChainProperty(PostEditChangeChainProperty);
}

void UMetaSoundSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{	
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	constexpr bool bNotifyDefaultRenamed = true;
	ConformPageSettings(bNotifyDefaultRenamed);

	if (PropertyChangedEvent.MemberProperty->GetName() == GetPageSettingPropertyName())
	{
		OnPageSettingsUpdated.Broadcast();
	}

	DenyListCacheChangeID++;
}
#endif // WITH_EDITOR

void UMetaSoundSettings::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	constexpr bool bNotifyDefaultRenamed = false;
	ConformPageSettings(bNotifyDefaultRenamed);
#endif // WITH_EDITOR

#if !NO_LOGGING
	bWarnAccessBeforeInit = false;
#endif // !NO_LOGGING

	if (const FMetaSoundPageSettings* Page = FindPageSettings(TargetPageName))
	{
		UE_LOG(LogMetaSound, Display, TEXT("MetaSound Page Target Initialized to '%s'"), *GetTargetPageSettings().Name.ToString());
	}
	else
	{
		UE_LOG(LogMetaSound, Warning, TEXT("TargetPageName '%s' at time of 'UMetaSoundSettings::PostInitProperties' did not correspond to a valid page."), *TargetPageName.ToString());
		if (PageSettings.IsEmpty())
		{
			UE_LOG(LogMetaSound, Warning, TEXT("Setting target to '%s' page settings."), *DefaultPageSettings.Name.ToString());
			TargetPageName = DefaultPageSettings.Name;
		}
		else
		{
			UE_LOG(LogMetaSound, Warning, TEXT("Setting target to highest project page settings '%s'."), *PageSettings.Last().Name.ToString());
			TargetPageName = PageSettings.Last().Name;
		}
	}
}

bool UMetaSoundSettings::SetTargetPage(FName PageName)
{
	if (const FMetaSoundPageSettings* PageSetting = FindPageSettings(PageName))
	{
		const FName TargetPage = TargetPageNameOverride.IsSet() ? *TargetPageNameOverride : TargetPageName;
		if (TargetPage != PageSetting->Name)
		{
			UE_LOG(LogMetaSound, Display, TEXT("Target page override set to '%s'."), *TargetPage.ToString());
			TargetPageNameOverride = PageSetting->Name;
			return true;
		}
	}

	return false;
}

#if WITH_EDITORONLY_DATA
Metasound::Engine::FOnSettingsDefaultConformed& UMetaSoundSettings::GetOnDefaultRenamedDelegate()
{
	return OnDefaultRenamed;
}

Metasound::Engine::FOnPageSettingsUpdated& UMetaSoundSettings::GetOnPageSettingsUpdatedDelegate()
{
	return OnPageSettingsUpdated;
}

FName UMetaSoundSettings::GetPageSettingPropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UMetaSoundSettings, PageSettings);
}

FName UMetaSoundSettings::GetQualitySettingPropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UMetaSoundSettings, QualitySettings);
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
TArray<FName> UMetaSoundSettings::GetPageNames()
{
	if (const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>())
	{
		TArray<FName> Names;
		Settings->IteratePageSettings([&Names](const FMetaSoundPageSettings& PageSetting)
		{
			Names.Add(PageSetting.Name);
		});
		return Names;
	}

	return { };
}

TArray<FName> UMetaSoundSettings::GetQualityNames()
{
	if (const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>())
	{
		TArray<FName> Names;
		auto GetName = [](const FMetaSoundQualitySettings& Quality) { return Quality.Name; };
		Algo::Transform(Settings->GetQualitySettings(), Names, GetName);
		return Names;
	}

	return { };
}
#endif // WITH_EDITOR

void UMetaSoundSettings::IteratePageSettings(TFunctionRef<void(const FMetaSoundPageSettings&)> Iter, bool bReverse) const
{
	if (bReverse)
	{
		for (int32 Index = PageSettings.Num() - 1; Index >= 0; --Index)
		{
			Iter(PageSettings[Index]);
		}
		Iter(GetDefaultPageSettings());
	}
	else
	{
		Iter(GetDefaultPageSettings());
		for (const FMetaSoundPageSettings& Setting : PageSettings)
		{
			Iter(Setting);
		}
	}
}
#undef LOCTEXT_NAMESPACE // MetaSound
