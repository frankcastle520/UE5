// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Common/TargetPlatformSettingsBase.h"
#include "Misc/ConfigCacheIni.h"
#if WITH_ENGINE
	#include "StaticMeshResources.h"
	#include "RHI.h"
	#include "DataDrivenShaderPlatformInfo.h"
#endif // WITH_ENGINE
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "TGenericWindowsTargetPlatformSettings"

/**
 * Template for Windows target platforms settings
 */

template<typename TProperties>
class TGenericWindowsTargetPlatformSettings : public TTargetPlatformSettingsBase<TProperties>
{
public:
	typedef TTargetPlatformSettingsBase<TProperties> TSuper;

	/**
	 * Default constructor.
	 */
	TGenericWindowsTargetPlatformSettings( )
	{

	#if WITH_ENGINE
		TextureLODSettings = nullptr; // These are registered by the device profile system.
		StaticMeshLODSettings.Initialize(this);


		// Get the Target RHIs for this platform, we do not always want all those that are supported.
		TArray<FName> TargetedShaderFormats;
		TGenericWindowsTargetPlatformSettings::GetAllTargetedShaderFormats(TargetedShaderFormats);

		static FName NAME_PCD3D_SM6(TEXT("PCD3D_SM6"));
		static FName NAME_PCD3D_SM5(TEXT("PCD3D_SM5"));
		static FName NAME_VULKAN_SM5(TEXT("SF_VULKAN_SM5"));
		static FName NAME_VULKAN_SM6(TEXT("SF_VULKAN_SM6"));
		static FName NAME_PCD3D_ES3_1(TEXT("PCD3D_ES31"));
		static FName NAME_SF_VULKAN_ES31(TEXT("SF_VULKAN_ES31"));
		static FName NAME_OPENGL_150_ES3_1(TEXT("GLSL_150_ES31"));

		// If we are targeting ES3.1, we also must cook encoded HDR reflection captures
		bRequiresEncodedHDRReflectionCaptures =	TargetedShaderFormats.Contains(NAME_SF_VULKAN_ES31)
												|| TargetedShaderFormats.Contains(NAME_OPENGL_150_ES3_1)
												|| TargetedShaderFormats.Contains(NAME_PCD3D_ES3_1);

	#endif
	}

public:

	//~ Begin ITargetPlatform Interface

	virtual bool SupportsFeature( ETargetPlatformFeatures Feature ) const override
	{
		// we currently do not have a build target for WindowsServer
		if (Feature == ETargetPlatformFeatures::Packaging)
		{
			return (TProperties::HasEditorOnlyData() || !TProperties::IsServerOnly());
		}

		if ( Feature == ETargetPlatformFeatures::ShouldSplitPaksIntoSmallerSizes )
		{
			return TProperties::IsClientOnly();
		}

		if (Feature == ETargetPlatformFeatures::CanCookPackages)
		{
			return TProperties::HasEditorOnlyData();
		}

		if (Feature == ETargetPlatformFeatures::MobileRendering)
		{
			static bool bCachedSupportsMobileRendering = false;
#if WITH_ENGINE
			static bool bHasCachedValue = false;
			if (!bHasCachedValue)
			{
				TArray<FName> TargetedShaderFormats;
				GetAllTargetedShaderFormats(TargetedShaderFormats);

				for (const FName& Format : TargetedShaderFormats)
				{
					if (IsMobilePlatform(ShaderFormatToLegacyShaderPlatform(Format)))
					{
						bCachedSupportsMobileRendering = true;
						break;
					}
				}
				bHasCachedValue = true;
			}
#endif

			return bCachedSupportsMobileRendering;
		}

		return TSuper::SupportsFeature(Feature);
	}

	virtual void GetAllPossibleShaderFormats( TArray<FName>& OutFormats ) const override
	{
		// no shaders needed for dedicated server target
		if (!TProperties::IsServerOnly())
		{
			static FName NAME_PCD3D_SM6(TEXT("PCD3D_SM6"));
			static FName NAME_PCD3D_SM5(TEXT("PCD3D_SM5"));
			static FName NAME_VULKAN_ES31(TEXT("SF_VULKAN_ES31"));
			static FName NAME_OPENGL_150_ES3_1(TEXT("GLSL_150_ES31"));
			static FName NAME_VULKAN_SM5(TEXT("SF_VULKAN_SM5"));
			static FName NAME_VULKAN_SM6(TEXT("SF_VULKAN_SM6"));
			static FName NAME_PCD3D_ES3_1(TEXT("PCD3D_ES31"));

			OutFormats.AddUnique(NAME_PCD3D_SM5);
			OutFormats.AddUnique(NAME_PCD3D_SM6);
			OutFormats.AddUnique(NAME_VULKAN_ES31);
			OutFormats.AddUnique(NAME_OPENGL_150_ES3_1);
			OutFormats.AddUnique(NAME_VULKAN_SM5);
			OutFormats.AddUnique(NAME_VULKAN_SM6);
			OutFormats.AddUnique(NAME_PCD3D_ES3_1);
		}
	}

	void GetAllTargetedShaderFormatsInternal(TArrayView<TCHAR const*> RelevantSettings, TArray<FName>& OutFormats) const
	{
		TArray<FString> TargetedShaderFormats;

		for (const TCHAR* Name : RelevantSettings)
		{
			TArray<FString> NewTargetedShaderFormats;
			GConfig->GetArray(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), Name, NewTargetedShaderFormats, GEngineIni);

			for (const FString& NewShaderFormat : NewTargetedShaderFormats)
			{
				TargetedShaderFormats.AddUnique(NewShaderFormat);
			}
		}
		
		// Gather the list of Target RHIs and filter out any that may be invalid.
		TArray<FName> PossibleShaderFormats;
		GetAllPossibleShaderFormats(PossibleShaderFormats);

		for (int32 ShaderFormatIdx = TargetedShaderFormats.Num() - 1; ShaderFormatIdx >= 0; ShaderFormatIdx--)
		{
			FString ShaderFormat = TargetedShaderFormats[ShaderFormatIdx];
			if (PossibleShaderFormats.Contains(FName(*ShaderFormat)) == false)
			{
				TargetedShaderFormats.RemoveAt(ShaderFormatIdx);
			}
		}

		for(const FString& ShaderFormat : TargetedShaderFormats)
		{
			OutFormats.AddUnique(FName(*ShaderFormat));
		}
	}

	virtual void GetAllTargetedShaderFormats( TArray<FName>& OutFormats ) const override 
	{
		// Get the Target RHIs for this platform, we do not always want all those that are supported. (reload in case user changed in the editor)

		TCHAR const* RelevantSettings[] = 
		{
			TEXT("TargetedRHIs"),
			TEXT("D3D12TargetedShaderFormats"),
			TEXT("D3D11TargetedShaderFormats"),
			TEXT("VulkanTargetedShaderFormats")
		};

		GetAllTargetedShaderFormatsInternal(RelevantSettings, OutFormats);
	}

	virtual void GetRayTracingShaderFormats( TArray<FName>& OutFormats ) const override 
	{
		if (UsesRayTracing())
		{
			TCHAR const* RelevantSettings[] = 
			{
				TEXT("VulkanTargetedShaderFormats")
			};

			GetAllTargetedShaderFormatsInternal(RelevantSettings, OutFormats);

			// We always support ray tracing shaders when cooking for D3D12 SM6, however we may skip them for SM5 based on project settings.
			OutFormats.AddUnique(FName(TEXT("PCD3D_SM6")));
		}
	}

#if WITH_ENGINE
	virtual void GetReflectionCaptureFormats(TArray<FName>& OutFormats) const override
	{
		if (bRequiresEncodedHDRReflectionCaptures)
		{
			OutFormats.Add(FName(TEXT("EncodedHDR")));
		}

		OutFormats.Add(FName(TEXT("FullHDR")));
	}

	virtual void GetShaderFormatModuleHints(TArray<FName>& OutModuleNames) const override
	{
		OutModuleNames.Add(TEXT("ShaderFormatD3D"));
		OutModuleNames.Add(TEXT("ShaderFormatOpenGL"));
		OutModuleNames.Add(TEXT("VulkanShaderFormat"));
	}

	virtual const class FStaticMeshLODSettings& GetStaticMeshLODSettings( ) const override
	{
		return StaticMeshLODSettings;
	}


	virtual const UTextureLODSettings& GetTextureLODSettings() const override
	{
		return *TextureLODSettings;
	}

	virtual void RegisterTextureLODSettings(const UTextureLODSettings* InTextureLODSettings) override
	{
		TextureLODSettings = InTextureLODSettings;
	}

#endif //WITH_ENGINE

	virtual bool UsesDistanceFields() const override
	{
		bool bEnableDistanceFields = false;
		GConfig->GetBool(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("bEnableDistanceFields"), bEnableDistanceFields, GEngineIni);

		return bEnableDistanceFields && TSuper::UsesDistanceFields();
	}

	virtual bool UsesRayTracing() const override
	{
		bool bEnableRayTracing = false;
		GConfig->GetBool(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("bEnableRayTracing"), bEnableRayTracing, GEngineIni);

		return bEnableRayTracing && TSuper::UsesRayTracing();
	}

	//~ End ITargetPlatform Interface

private:

#if WITH_ENGINE
	// Holds the texture LOD settings.
	const UTextureLODSettings* TextureLODSettings;

	// Holds static mesh LOD settings.
	FStaticMeshLODSettings StaticMeshLODSettings;

	// True if the project requires encoded HDR reflection captures
	bool bRequiresEncodedHDRReflectionCaptures;

#endif // WITH_ENGINE

};

#undef LOCTEXT_NAMESPACE
