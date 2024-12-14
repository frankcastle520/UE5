// Copyright Epic Games, Inc. All Rights Reserved.

#include "FabBrowserApi.h"
#include "FabAuthentication.h"
#include "FabBrowser.h"
#include "FabSettings.h"
#include "FabLog.h"
#include "Interfaces/IPluginManager.h"
#include "Workflows/GenericImportWorkflow.h"
#include "Workflows/PackImportWorkflow.h"
#include "Workflows/QuixelImportWorkflow.h"
#include "Misc/EngineVersion.h"
#include "Workflows/GenericDragDropWorkflow.h"
#include "Workflows/QuixelDragDropWorkflow.h"

void UFabBrowserApi::CompleteWorkflow(const FString& Id)
{
	TSharedPtr<IFabWorkflow> CompletedWorkflow;
	for (const TSharedPtr<IFabWorkflow>& ActiveWorkflow : this->ActiveWorkflows)
	{
		if (ActiveWorkflow->AssetId == Id)
		{
			CompletedWorkflow = ActiveWorkflow;
			break;
		}
	}

	this->ActiveWorkflows.Remove(CompletedWorkflow);
}

void UFabBrowserApi::AddToProject(const FString& DownloadUrl, const FFabAssetMetadata& AssetMetadata)
{
	// Check if the listing is already being downloaded
	for (const TSharedPtr<IFabWorkflow>& ActiveWorkflow : this->ActiveWorkflows)
	{
		if (ActiveWorkflow->AssetId == AssetMetadata.AssetId)
		{
			FAB_LOG("The listing with Id %s is already being processed.", *AssetMetadata.AssetId);
			return;
		}
	}

	FAB_LOG("Asset Type = %s", *AssetMetadata.AssetType);
	FAB_LOG("Is Quixel = %d", AssetMetadata.IsQuixel);

	if (AssetMetadata.AssetType == "unreal-engine")
	{
		const FString BaseUrls = FString::Join(AssetMetadata.DistributionPointBaseUrls, TEXT(","));
		FAB_LOG("Base Url %s", *BaseUrls);

		const TSharedPtr<FPackImportWorkflow> PackImportWorkflow = MakeShared<FPackImportWorkflow>(AssetMetadata.AssetId, AssetMetadata.AssetName, DownloadUrl, BaseUrls);
		PackImportWorkflow->OnFabWorkflowComplete().BindLambda(
			[this, AssetId = AssetMetadata.AssetId]()
			{
				CompleteWorkflow(AssetId);
			}
		);
		PackImportWorkflow->OnFabWorkflowCancel().BindLambda(
			[this, AssetId = AssetMetadata.AssetId]()
			{
				CompleteWorkflow(AssetId);
			}
		);
		PackImportWorkflow->Execute();
		this->ActiveWorkflows.Add(PackImportWorkflow);
		return;
	}
	
	if (AssetMetadata.IsQuixel)
	{
		const TSharedPtr<FQuixelImportWorkflow> QuixelImportWorkflow = MakeShared<FQuixelImportWorkflow>(AssetMetadata.AssetId, AssetMetadata.AssetName, DownloadUrl);
		QuixelImportWorkflow->Execute();
		QuixelImportWorkflow->OnFabWorkflowComplete().BindLambda(
			[this, AssetId = AssetMetadata.AssetId]()
			{
				CompleteWorkflow(AssetId);
			}
		);
		QuixelImportWorkflow->OnFabWorkflowCancel().BindLambda(
			[this, AssetId = AssetMetadata.AssetId]()
			{
				CompleteWorkflow(AssetId);
			}
		);
		this->ActiveWorkflows.Add(QuixelImportWorkflow);
		return;
	}

	if (AssetMetadata.AssetType == "gltf" || AssetMetadata.AssetType == "glb" || AssetMetadata.AssetType == "fbx")
	{
		const TSharedPtr<FGenericImportWorkflow> InterchangeImportWorkflow = MakeShared<FGenericImportWorkflow>(AssetMetadata.AssetId, AssetMetadata.AssetName, DownloadUrl);
		InterchangeImportWorkflow->OnFabWorkflowComplete().BindLambda(
			[this, AssetId = AssetMetadata.AssetId]()
			{
				CompleteWorkflow(AssetId);
			}
		);
		InterchangeImportWorkflow->OnFabWorkflowCancel().BindLambda(
			[this, AssetId = AssetMetadata.AssetId]()
			{
				CompleteWorkflow(AssetId);
			}
		);
		InterchangeImportWorkflow->Execute();
		this->ActiveWorkflows.Add(InterchangeImportWorkflow);
		return;
	}

	FAB_LOG_ERROR("Asset type not handled %s", *AssetMetadata.AssetType);
}

void UFabBrowserApi::DragStart(const FFabAssetMetadata& AssetMetadata)
{
	// Check if the listing is already being downloaded
	for (const TSharedPtr<IFabWorkflow>& ActiveWorkflow : this->ActiveWorkflows)
	{
		if (ActiveWorkflow->AssetId == AssetMetadata.AssetId)
		{
			FAB_LOG("The listing with Id %s is already being processed.", *AssetMetadata.AssetId);
			return;
		}
	}

	FAB_LOG("Listing Type = %s", *AssetMetadata.ListingType);
	FAB_LOG("Is Quixel = %d", AssetMetadata.IsQuixel);
	if (AssetMetadata.IsQuixel)
	{
		const TSharedPtr<FQuixelDragDropWorkflow> QuixelDragDropWorkflow = MakeShared<FQuixelDragDropWorkflow>(
			AssetMetadata.AssetId,
			AssetMetadata.AssetName,
			AssetMetadata.ListingType
		);
		QuixelDragDropWorkflow->OnFabWorkflowComplete().BindLambda(
			[this, AssetId = AssetMetadata.AssetId]()
			{
				FAB_LOG("Quixel Drag workflow completed!");
				CompleteWorkflow(AssetId);
			}
		);
		QuixelDragDropWorkflow->OnFabWorkflowCancel().BindLambda(
			[this, AssetId = AssetMetadata.AssetId]()
			{
				FAB_LOG("Quixel Drag workflow cancelled!");
				CompleteWorkflow(AssetId);
			}
		);
		this->ActiveWorkflows.Add(QuixelDragDropWorkflow);
		QuixelDragDropWorkflow->Execute();
	}
	else
	{
		const TSharedPtr<FGenericDragDropWorkflow> DragDropWorkflow = MakeShared<FGenericDragDropWorkflow>(AssetMetadata.AssetId, AssetMetadata.AssetName);
		DragDropWorkflow->OnFabWorkflowComplete().BindLambda(
			[this, AssetId = AssetMetadata.AssetId]()
			{
				FAB_LOG("Drag workflow completed!");
				CompleteWorkflow(AssetId);
			}
		);
		DragDropWorkflow->OnFabWorkflowCancel().BindLambda(
			[this, AssetId = AssetMetadata.AssetId]()
			{
				FAB_LOG("Drag workflow cancelled!");
				CompleteWorkflow(AssetId);
			}
		);
		this->ActiveWorkflows.Add(DragDropWorkflow);
		DragDropWorkflow->Execute();
	}
}

void UFabBrowserApi::OnDragInfoSuccess(const FString& DownloadUrl, const FFabAssetMetadata& AssetMetadata)
{
	OnSignedUrlGenerated().Broadcast(DownloadUrl, AssetMetadata);
}

void UFabBrowserApi::OnDragInfoFailure(const FString& AssetId)
{
	FAB_LOG_ERROR("Drag drop failure for asset id %s", *AssetId);
	// Get the drag workflow
	FFabAssetMetadata Metadata;
	Metadata.AssetId = AssetId;
	OnSignedUrlGenerated().Broadcast("", Metadata);
}

FDelegateHandle UFabBrowserApi::AddSignedUrlCallback(TFunction<void(const FString&, const FFabAssetMetadata&)> Callback)
{
	return OnSignedUrlGenerated().AddLambda(Callback);
}

void UFabBrowserApi::Login()
{
	FabAuthentication::LoginUsingAccountPortal();
}

void UFabBrowserApi::Logout()
{
	FabAuthentication::DeletePersistentAuth();
}

void UFabBrowserApi::OpenPluginSettings()
{
	FAB_LOG("Open plugin settings");
	FFabBrowser::ShowSettings();
}

FFabFrontendSettings UFabBrowserApi::GetSettings()
{
	const UFabSettings* FabSettings = GetDefault<UFabSettings>();

	FFabFrontendSettings FrontendSettings;
	if (FabSettings->PreferredDefaultFormat == EFabPreferredFormats::GLTF)
	{
		FrontendSettings.PreferredFormat = "gltf";
	}
	else if (FabSettings->PreferredDefaultFormat == EFabPreferredFormats::FBX)
	{
		FrontendSettings.PreferredFormat = "fbx";
	}
	if (FabSettings->PreferredQualityTier == EFabPreferredQualityTier::Low)
	{
		FrontendSettings.PreferredQuality = "low";
	}
	else if (FabSettings->PreferredQualityTier == EFabPreferredQualityTier::Medium)
	{
		FrontendSettings.PreferredQuality = "medium";
	}
	else if (FabSettings->PreferredQualityTier == EFabPreferredQualityTier::High)
	{
		FrontendSettings.PreferredQuality = "high";
	}
	else if (FabSettings->PreferredQualityTier == EFabPreferredQualityTier::Raw)
	{
		FrontendSettings.PreferredQuality = "raw";
	}

	return FrontendSettings;
}

void UFabBrowserApi::SetPreferredQualityTier(const FString& PreferredQuality)
{
	UFabSettings* FabSettings = GetMutableDefault<UFabSettings>();
	if (PreferredQuality == "low")
	{
		FabSettings->PreferredQualityTier = EFabPreferredQualityTier::Low;
	}
	else if (PreferredQuality == "medium")
	{
		FabSettings->PreferredQualityTier = EFabPreferredQualityTier::Medium;
	}
	else if (PreferredQuality == "high")
	{
		FabSettings->PreferredQualityTier = EFabPreferredQualityTier::High;
	}
	else if (PreferredQuality == "raw")
	{
		FabSettings->PreferredQualityTier = EFabPreferredQualityTier::Raw;
	}

	FabSettings->SaveConfig();
}

FFabApiVersion UFabBrowserApi::GetApiVersion()
{
	FFabApiVersion ApiVersion;
	const FEngineVersion EngineVersion = FEngineVersion::Current();
	const uint16 MajorVersion          = EngineVersion.GetMajor();
	const uint16 MinorVersion          = EngineVersion.GetMinor();

	ApiVersion.Ue  = FString::FromInt(MajorVersion) + "." + FString::FromInt(MinorVersion);
	ApiVersion.Api = "1.0.0";

	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin("Fab"); Plugin.IsValid())
	{
		const FPluginDescriptor& PluginDescriptor = Plugin->GetDescriptor();

		const FString PluginVersion = PluginDescriptor.VersionName;
		ApiVersion.PluginVersion = PluginVersion;
	}

	return ApiVersion;
}

void UFabBrowserApi::OpenUrlInBrowser(const FString& Url)
{
	FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
}

FString UFabBrowserApi::GetUrl()
{
	return FFabBrowser::GetUrl();
}

FString UFabBrowserApi::GetAuthToken()
{
	const UFabSettings* FabSettings = GetDefault<UFabSettings>();
	if (!FabSettings->CustomAuthToken.IsEmpty())
	{
		FAB_LOG("Returning custom auth token: %s", *FabSettings->CustomAuthToken);
		return FabSettings->CustomAuthToken;
	}

	return FabAuthentication::GetAuthToken();
}

FString UFabBrowserApi::GetRefreshToken()
{
	return FabAuthentication::GetRefreshToken();
}
