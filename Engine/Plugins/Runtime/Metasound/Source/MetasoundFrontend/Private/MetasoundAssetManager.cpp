// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetManager.h"
#include "MetasoundFrontendRegistryKey.h"

namespace Metasound::Frontend
{
	namespace AssetManagerPrivate
	{
		static TUniquePtr<IMetaSoundAssetManager> Instance;

		bool IsAssetClassType(EMetasoundFrontendClassType ClassType)
		{
			switch (ClassType)
			{
				case EMetasoundFrontendClassType::External:
				case EMetasoundFrontendClassType::Graph:
				case EMetasoundFrontendClassType::Invalid:
					return true;

				default:
					return false;
			}
		}
	} // AssetManagerPrivate

	namespace AssetTags
	{
		const FString ArrayDelim = TEXT(",");

		const FName AssetClassID = "AssetClassID";

#if WITH_EDITORONLY_DATA
		const FName IsPreset = "bIsPreset";
#endif // WITH_EDITORONLY_DATA

		const FName RegistryVersionMajor = "RegistryVersionMajor";
		const FName RegistryVersionMinor = "RegistryVersionMinor";

#if WITH_EDITORONLY_DATA
		const FName RegistryInputTypes = "RegistryInputTypes";
		const FName RegistryOutputTypes = "RegistryOutputTypes";
#endif // WITH_EDITORONLY_DATA
	} // namespace AssetTags

	FAssetKey::FAssetKey(const FMetasoundFrontendClassName& InClassName, const FMetasoundFrontendVersionNumber& InVersion)
		: ClassName(InClassName)
		, Version(InVersion)
	{
	}

	FAssetKey::FAssetKey(const FNodeRegistryKey& RegKey)
		: ClassName(RegKey.ClassName)
		, Version(RegKey.Version)
	{
		checkf(AssetManagerPrivate::IsAssetClassType(RegKey.Type), TEXT("Invalid ClassType '%s' for Registry Key"), LexToString(RegKey.Type));
	}

	FAssetKey::FAssetKey(const FMetasoundFrontendClassMetadata& InMetadata)
		: ClassName(InMetadata.GetClassName())
		, Version(InMetadata.GetVersion())
	{
		checkf(AssetManagerPrivate::IsAssetClassType(InMetadata.GetType()), TEXT("Invalid ClassType '%s' for Registry Key"), LexToString(InMetadata.GetType()));
	}

	FString FAssetKey::ToString() const
	{
		TStringBuilder<128> KeyStringBuilder;
		KeyStringBuilder.Append(ClassName.GetFullName().ToString());
		KeyStringBuilder.AppendChar('_');
		KeyStringBuilder.Append(FString::FromInt(Version.Major));
		KeyStringBuilder.AppendChar('.');
		KeyStringBuilder.Append(FString::FromInt(Version.Minor));
		return KeyStringBuilder.ToString();
	}

	IMetaSoundAssetManager* IMetaSoundAssetManager::Get()
	{
		return AssetManagerPrivate::Instance.Get();
	}

	IMetaSoundAssetManager& IMetaSoundAssetManager::GetChecked()
	{
		check(AssetManagerPrivate::Instance.IsValid());
		return *AssetManagerPrivate::Instance;
	}

	void IMetaSoundAssetManager::Deinitialize()
	{
		if (AssetManagerPrivate::Instance.IsValid())
		{
			AssetManagerPrivate::Instance.Reset();
		}
	}

	void IMetaSoundAssetManager::Initialize(TUniquePtr<IMetaSoundAssetManager>&& InInterface)
	{
		check(!AssetManagerPrivate::Instance.IsValid());
		AssetManagerPrivate::Instance = MoveTemp(InInterface);
	}

	bool IMetaSoundAssetManager::IsTesting() const
	{
		return false;
	}
} // namespace Metasound::Frontend
