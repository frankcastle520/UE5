// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MetaHumanValidationTests.h"

// Core
#include "Algo/Transform.h"
#include "Misc/FileHelper.h"
#include "Misc/AutomationTest.h"

// CoreUObject
#include "UObject/MetaData.h"
#include "Misc/PackageName.h"

// Engine
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMeshLODSettings.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Animation/PoseAsset.h"
#include "EditorFramework/AssetImportData.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/LODSyncComponent.h"

// AssetRegistry
#include "AssetRegistry/IAssetRegistry.h"

// UnrealEd
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Editor/AssetGuideline.h"

// HairStrandsCore
#include "GroomComponent.h"
#include "GroomBindingAsset.h"

// Json
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// RigLogicModule
#include "DNAAsset.h"
#include "DNAReader.h"

// ControlRig
#include "ControlRig.h"

// ControlRigDeveloper
#include "ControlRigBlueprint.h"

// PhysicsCore
#include "PhysicalMaterials/PhysicalMaterial.h"

// MetaHumanProjectUtilites
#include "MetaHumanProjectUtilities.h"

// Operator needed to use FIniStringValue in UTEST macros. There is no default equality operator for this type
static bool operator== (const FIniStringValue& Value1, const FIniStringValue& Value2)
{
	return Value1.Section == Value2.Section &&
		Value1.Key == Value2.Key &&
		Value1.Value == Value2.Value &&
		Value1.Filename == Value2.Filename;
}

namespace UE::MetaHuman
{
namespace TestUtils
{
	static const FString GetGamePath()
	{
		return TEXT("/Game");
	}

	static const FString GetMetaHumansPath()
	{
		return GetGamePath() / "MetaHumans";
	}

	static FString GetMetaHumanCommonPath()
	{
		return GetMetaHumansPath() / TEXT("Common");
	}

	static FString GetMetaHumanContentDir()
	{
		return FPaths::ProjectContentDir() / TEXT("MetaHumans");
	}

	static FString GetMHAssetVersionFileName()
	{
		return TEXT("MHAssetVersions.txt");
	}

	static FString GetMHAssetVersionFilePath()
	{
		return GetMetaHumanContentDir() / GetMHAssetVersionFileName();
	}

	static FString GetExportManifestFileName()
	{
		return TEXT("ExportManifest.txt");
	}

	static FString GetExportManifestFilePath()
	{
		return FPaths::ProjectContentDir() / GetExportManifestFileName();
	}

	static bool ParseTestName(const FString& InTestName, FString& OutBaseTestName, FString& OutMetaHumanName)
	{
		int32 LastDotIndex = INDEX_NONE;
		if (InTestName.FindLastChar(TEXT('.'), LastDotIndex))
		{
			OutBaseTestName = InTestName.Left(LastDotIndex);
			OutMetaHumanName = InTestName.RightChop(LastDotIndex + 1);
			return true;
		}

		return false;
	}

	static TSharedPtr<FJsonObject> ReadJsonFromFile(const FString& InFilePath)
	{
		FString FileContents;
		if (FFileHelper::LoadFileToString(FileContents, *InFilePath))
		{
			const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(FileContents);

			TSharedPtr<FJsonObject> RootObject;
			if (FJsonSerializer::Deserialize(JsonReader, RootObject))
			{
				return RootObject;
			}
		}

		return nullptr;
	}

	static const TSet<FString>& GetExportAssetKindValues()
	{
		static TSet<FString> ExportAssetKindValues =
		{
			TEXT("UE"),
			TEXT("UEFN"),
			TEXT("Source")
		};
		return ExportAssetKindValues;
	}

	static const TSet<FString>& GetExportQualityLevels()
	{
		static TSet<FString> QualityLevelValues =
		{
			TEXT("Cinematic"),
			TEXT("High"),
			TEXT("Medium"),
			TEXT("Low")
		};
		return QualityLevelValues;
	}

	static FString GetMetaHumanBlueprintPackageName(const FString& InMetaHumanName)
	{
		return GetMetaHumansPath() / InMetaHumanName / TEXT("BP_") + InMetaHumanName;
	}

	template<typename T>
	static T* GetAssetByPackageName(const FString& InPackageName)
	{
		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		TArray<FAssetData> AssetList;
		AssetRegistry.GetAssetsByPackageName(FName{ *InPackageName }, AssetList);
		if (!AssetList.IsEmpty())
		{
			return Cast<T>(AssetList[0].GetAsset());
		}

		return nullptr;
	}

	template<typename T>
	static T* GetComponentByName(AActor* InActor, FName InComponentName)
	{
		if (InActor != nullptr)
		{
			TInlineComponentArray<T*> Components{ InActor };
			T** FoundComponent = Components.FindByPredicate([InComponentName](const T* Component)
			{
				return Component->GetFName() == InComponentName;
			});

			if (FoundComponent)
			{
				return *FoundComponent;
			}
		}

		return nullptr;
	}

	static int32 GetNumLODsForQuality(FStringView InQuality, FStringView InPartName)
	{
		const TSortedMap<FStringView, int32> FaceLODs =
		{
			{ TEXTVIEW("Cinematic"), 8 },
			{ TEXTVIEW("High"),		 4 },
			{ TEXTVIEW("Medium"),	 3 },
			{ TEXTVIEW("Low"),		 2 },
		};

		const TSortedMap<FStringView, int32> BodyLODs =
		{
			{ TEXTVIEW("Cinematic"), 4 },
			{ TEXTVIEW("High"),		 4 },
			{ TEXTVIEW("Medium"),	 3 },
			{ TEXTVIEW("Low"),		 2 },
		};

		static const TSortedMap<FStringView, TSortedMap<FStringView, int32>> LODsForQuality =
		{
			{ TEXTVIEW("Face"), FaceLODs },
			{ TEXTVIEW("Body"), BodyLODs },
			{ TEXTVIEW("Torso"), BodyLODs },
			{ TEXTVIEW("Legs"), BodyLODs },
			{ TEXTVIEW("Feet"), BodyLODs }
		};

		return LODsForQuality[InPartName][InQuality];
	}

	static int32 GetTextureResolutionForQuality(FStringView InPartName, FStringView InQuality, FStringView InTextureName)
	{
		static const TSortedMap<FStringView, TSortedMap<FStringView, TSortedMap<FStringView, int32>>> TextureResolutions =
		{
			{
				TEXTVIEW("Body"),
				{
					{
						TEXTVIEW("High"),
						{
							{ TEXTVIEW("BaseColor"), 1024 },
							{ TEXTVIEW("Normal"), 1024 },
							{ TEXTVIEW("Specular"), 1024 }
						}
					},
					{
						TEXTVIEW("Medium"),
						{
							{ TEXTVIEW("BaseColor"), 1024 },
							{ TEXTVIEW("Normal"), 1024 },
							{ TEXTVIEW("Specular"), 1024 }
						}
					},
					{
						TEXTVIEW("Low"),
						{
							{ TEXTVIEW("BaseColor"), 512 },
							{ TEXTVIEW("Normal"), 512 },
							{ TEXTVIEW("Specular"), 512 }
						}
					}
				}
			},
			{
				TEXTVIEW("Torso"),
				{
					{
						TEXTVIEW("High"),
						{
							{ TEXTVIEW("BaseColor"), 2048 },
							{ TEXTVIEW("Normal"), 2048 },
							{ TEXTVIEW("Specular"), 1024 }
						}
					},
					{
						TEXTVIEW("Medium"),
						{
							{ TEXTVIEW("BaseColor"), 1024 },
							{ TEXTVIEW("Normal"), 1024 },
							{ TEXTVIEW("Specular"), 1024 }
						}
					},
					{
						TEXTVIEW("Low"),
						{
							{ TEXTVIEW("BaseColor"), 512 },
							{ TEXTVIEW("Normal"), 512 },
							{ TEXTVIEW("Specular"), 512 }
						}
					}
				}
			},
			{
				TEXTVIEW("Legs"),
				{
					{
						TEXTVIEW("High"),
						{
							{ TEXTVIEW("BaseColor"), 1024 },
							{ TEXTVIEW("Normal"), 2048 },
							{ TEXTVIEW("Specular"), 1024 }
						}
					},
					{
						TEXTVIEW("Medium"),
						{
							{ TEXTVIEW("BaseColor"), 1024 },
							{ TEXTVIEW("Normal"), 1024 },
							{ TEXTVIEW("Specular"), 1024 }
						}
					},
					{
						TEXTVIEW("Low"),
						{
							{ TEXTVIEW("BaseColor"), 512 },
							{ TEXTVIEW("Normal"), 512 },
							{ TEXTVIEW("Specular"), 512 }
						}
					}
				}
			},
			{
				TEXTVIEW("Feet"),
				{
					{
						TEXTVIEW("High"),
						{
							{ TEXTVIEW("BaseColor"), 512 },
							{ TEXTVIEW("Normal"), 512 },
							{ TEXTVIEW("Specular"), 512 }
						}
					},
					{
						TEXTVIEW("Medium"),
						{
							{ TEXTVIEW("BaseColor"), 512 },
							{ TEXTVIEW("Normal"), 512 },
							{ TEXTVIEW("Specular"), 512 }
						}
					},
					{
						TEXTVIEW("Low"),
						{
							{ TEXTVIEW("BaseColor"), 256 },
							{ TEXTVIEW("Normal"), 256 },
							{ TEXTVIEW("Specular"), 256 }
						}
					}
				}
			}
		};

		return TextureResolutions[InPartName][InQuality][InTextureName];
	};

	static bool IsUEExport(const TArray<FString>& InExportAssetKind)
	{
		return InExportAssetKind.Contains(TEXT("UE"));
	}

	static bool IsUEFNExport(const TArray<FString>& InExportAssetKind)
	{
		return InExportAssetKind.Contains(TEXT("UEFN"));
	}

	static bool IsOptimizedExport(FStringView InQuality)
	{
		return InQuality != TEXTVIEW("Cinematic");
	}

	static FString GetBodyMaterialName(FStringView InQuality, FStringView InMetaHumanName)
	{
		FStringView MaterialNameBase = IsOptimizedExport(InQuality) ? TEXTVIEW("MI_BodySynthesized_Simplified") : TEXTVIEW("MI_BodySynthesized");
		// return FString::Format(TEXT("{0}_{1}"), { MaterialNameBase, InMetaHumanName });
		return FString{ MaterialNameBase };
	}

	struct FLODSyncSettings
	{
		int32 NumComponentsToSync = INDEX_NONE;
		int32 NumCustomLODMapping = INDEX_NONE;
	};

	const FLODSyncSettings& GetLODSyncSettings(FStringView InQuality)
	{
		static const TSortedMap<FStringView, FLODSyncSettings> LODSyncSettings =
		{
			{
				TEXTVIEW("Cinematic"), FLODSyncSettings
				{
					.NumComponentsToSync = 11,
					.NumCustomLODMapping = 4
				}
			},
			{
				TEXTVIEW("High"), FLODSyncSettings
				{
					.NumComponentsToSync = 10,
					.NumCustomLODMapping = 5
				}
			},
			{
				TEXTVIEW("Medium"), FLODSyncSettings
				{
					.NumComponentsToSync = 9,
					.NumCustomLODMapping = 4
				}
			},
			{
				TEXTVIEW("Low"), FLODSyncSettings
				{
					.NumComponentsToSync = 8,
					.NumCustomLODMapping = 3
				}
			}
		};

		return LODSyncSettings[InQuality];
	}

	struct FFacePostProcessAnimBPSettings
	{
		int32 LODThreshold = INDEX_NONE;
		int32 RigLogicLODTheshold = INDEX_NONE;
		bool bEnableNeckCorrectives = false;
		int32 NeckCorrectivesLODThreshold = INDEX_NONE;
		bool bEnableNeckProceduralControlRig = false;
		int32 NeckProceduralControlRigLODThreshold = INDEX_NONE;
		bool bEnableHeadMovementIK = false;
	};

	static const FFacePostProcessAnimBPSettings& GetFacePostProcessAnimBPSettings(FStringView InQuality)
	{
		static const TSortedMap<FStringView, FFacePostProcessAnimBPSettings> FacePostProcessAnimBPSettings =
		{
			{
				TEXTVIEW("Cinematic"), FFacePostProcessAnimBPSettings
				{
					.LODThreshold = INDEX_NONE,
					.RigLogicLODTheshold = INDEX_NONE,
					.bEnableNeckCorrectives = true,
					.NeckCorrectivesLODThreshold = INDEX_NONE,
					.bEnableNeckProceduralControlRig = true,
					.NeckProceduralControlRigLODThreshold = INDEX_NONE,
					.bEnableHeadMovementIK = false,
				}
			},
			{
				TEXTVIEW("High"), FFacePostProcessAnimBPSettings
				{
					.LODThreshold = INDEX_NONE,
					.RigLogicLODTheshold = 2,
					.bEnableNeckCorrectives = true,
					.NeckCorrectivesLODThreshold = 0,
					.bEnableNeckProceduralControlRig = true,
					.NeckProceduralControlRigLODThreshold = 0,
					.bEnableHeadMovementIK = false,
				}
			},
			{
				TEXTVIEW("Medium"), FFacePostProcessAnimBPSettings
				{
					.LODThreshold = INDEX_NONE,
					.RigLogicLODTheshold = 1,
					.bEnableNeckCorrectives = false,
					.NeckCorrectivesLODThreshold = INDEX_NONE,
					.bEnableNeckProceduralControlRig = false,
					.NeckProceduralControlRigLODThreshold = INDEX_NONE,
					.bEnableHeadMovementIK = false,
				}
			},
			{
				TEXTVIEW("Low"), FFacePostProcessAnimBPSettings
				{
					.LODThreshold = INDEX_NONE,
					.RigLogicLODTheshold = 0,
					.bEnableNeckCorrectives = false,
					.NeckCorrectivesLODThreshold = INDEX_NONE,
					.bEnableNeckProceduralControlRig = false,
					.NeckProceduralControlRigLODThreshold = INDEX_NONE,
					.bEnableHeadMovementIK = false,
				}
			}
		};

		return FacePostProcessAnimBPSettings[InQuality];
	}

	struct FBodyPostProcessAnimBPSettings
	{
		int32 LODThreshold = INDEX_NONE;
		bool bEnableBodyCorrectives = true;
		bool bEnableHeadMovementIK = true;
	};

	static const FBodyPostProcessAnimBPSettings GetBodyPostProcessAnimBPSettings(FStringView InQuality)
	{
		static const TSortedMap<FStringView, FBodyPostProcessAnimBPSettings> BodyPostProcessAnimBPSettings =
		{
			{
				TEXTVIEW("Cinematic"), FBodyPostProcessAnimBPSettings
				{
					.LODThreshold = INDEX_NONE,
					.bEnableBodyCorrectives = true,
					.bEnableHeadMovementIK = true
				}
			},
			{
				TEXTVIEW("High"), FBodyPostProcessAnimBPSettings
				{
					.LODThreshold = 0,
					.bEnableBodyCorrectives = true,
					.bEnableHeadMovementIK = true
				}
			},
			{
				TEXTVIEW("Medium"), FBodyPostProcessAnimBPSettings
				{
					.LODThreshold = INDEX_NONE,
					.bEnableBodyCorrectives = false,
					.bEnableHeadMovementIK = true
				}
			},
			{
				TEXTVIEW("Low"), FBodyPostProcessAnimBPSettings
				{
					.LODThreshold = INDEX_NONE,
					.bEnableBodyCorrectives = false,
					.bEnableHeadMovementIK = true
				}
			}
		};

		return BodyPostProcessAnimBPSettings[InQuality];
	}

	struct FClothingPostProcessAnimBPSettings
	{
		static constexpr FStringView bEnableRigidBodySimulationPropertyName = TEXTVIEW("Enable Rigid Body Simulation");
		static constexpr FStringView RigidBodyLODThresholdPropertyName = TEXTVIEW("Rigid Body LOD Threshold");
		static constexpr FStringView bEnableControlRigPropertyName = TEXTVIEW("Enable Control Rig");
		static constexpr FStringView ControlRigLODThresholdPropertyName = TEXTVIEW("Control Rig LOD Threshold");
		static constexpr FStringView OverridePhysicsAssetPropertyName = TEXTVIEW("Override Physics Asset");
		static constexpr FStringView ControlRigClassPropertyName = TEXTVIEW("Control Rig Class");

		int32 LODThreshold = INDEX_NONE;
		bool bEnableRigidBodySimulation = true;
		int32 RigidBodyLODThreshold = INDEX_NONE;
		bool bEnableControlRig = true;
		int32 ControlRigLODThreshold = INDEX_NONE;
	};

	static const FClothingPostProcessAnimBPSettings GetClothingPostProcessAnimBPSettings(FStringView InPartName, FStringView InQuality)
	{
		static const TSortedMap<FStringView, TSortedMap<FStringView, FClothingPostProcessAnimBPSettings>> PostProcessAnimBPSettings =
		{
			{
				TEXTVIEW("Torso"),
				{
					{
						TEXTVIEW("Cinematic"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = true,
							.RigidBodyLODThreshold = 1,
							.bEnableControlRig = true,
							.ControlRigLODThreshold = 3
						}
					},
					{
						TEXTVIEW("High"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = true,
							.RigidBodyLODThreshold = 1,
							.bEnableControlRig = true,
							.ControlRigLODThreshold = 0
						}
					},
					{
						TEXTVIEW("Medium"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = true,
							.RigidBodyLODThreshold = 0,
							.bEnableControlRig = false,
							.ControlRigLODThreshold = INDEX_NONE
						}
					},
					{
						TEXTVIEW("Low"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = false,
							.RigidBodyLODThreshold = INDEX_NONE,
							.bEnableControlRig = false,
							.ControlRigLODThreshold = INDEX_NONE
						}
					}
				}
			},
			{
				TEXTVIEW("Legs"),
				{
					{
						TEXTVIEW("Cinematic"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = true,
							.RigidBodyLODThreshold = 3,
							.bEnableControlRig = true,
							.ControlRigLODThreshold = 1
						}
					},
					{
						TEXTVIEW("High"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = true,
							.RigidBodyLODThreshold = 1,
							.bEnableControlRig = true,
							.ControlRigLODThreshold = 0
						}
					},
					{
						TEXTVIEW("Medium"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = false,
							.RigidBodyLODThreshold = INDEX_NONE,
							.bEnableControlRig = false,
							.ControlRigLODThreshold = INDEX_NONE
						}
					},
					{
						TEXTVIEW("Low"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = false,
							.RigidBodyLODThreshold = INDEX_NONE,
							.bEnableControlRig = false,
							.ControlRigLODThreshold = INDEX_NONE
						}
					}
				}
			},
			{
				TEXTVIEW("Feet"),
				{
					{
						TEXTVIEW("Cinematic"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = true,
							.RigidBodyLODThreshold = 1,
							.bEnableControlRig = true,
							.ControlRigLODThreshold = 3
						}
					},
					{
						TEXTVIEW("High"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = true,
							.RigidBodyLODThreshold = 1,
							.bEnableControlRig = true,
							.ControlRigLODThreshold = 0
						}
					},
					{
						TEXTVIEW("Medium"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = true,
							.RigidBodyLODThreshold = 0,
							.bEnableControlRig = false,
							.ControlRigLODThreshold = INDEX_NONE
						}
					},
					{
						TEXTVIEW("Low"), FClothingPostProcessAnimBPSettings
						{
							.LODThreshold = INDEX_NONE,
							.bEnableRigidBodySimulation = false,
							.RigidBodyLODThreshold = INDEX_NONE,
							.bEnableControlRig = false,
							.ControlRigLODThreshold = INDEX_NONE
						}
					}
				}
			}
		};

		return PostProcessAnimBPSettings[InPartName][InQuality];
	}

	static bool GetBodyTypeNameFromMeshName(const FString& InBodyMeshName, FString& OutBodyTypeName)
	{
		static FRegexPattern Pattern{ TEXT("^(m|f)_(med|tal|srt)_(nrw|ovw|unw)") };
		FRegexMatcher Matcher{ Pattern, InBodyMeshName };
		if (Matcher.FindNext())
		{
			OutBodyTypeName = Matcher.GetCaptureGroup(0);

			// const FString Gender = Matcher.GetCaptureGroup(1);
			// const FString Height = Matcher.GetCaptureGroup(2);
			// const FString Weight = Matcher.GetCaptureGroup(3);

			return true;
		}

		return false;
	}

	static FStringView GetBodyTypeNameFromIndex(int32 BodyTypeIndex)
	{
		static const TStaticArray<FStringView, 18> BodyTypeStrings =
		{
			TEXTVIEW("f_med_nrw"),
			TEXTVIEW("f_med_ovw"),
			TEXTVIEW("f_med_unw"),
			TEXTVIEW("f_srt_nrw"),
			TEXTVIEW("f_srt_ovw"),
			TEXTVIEW("f_srt_unw"),
			TEXTVIEW("f_tal_nrw"),
			TEXTVIEW("f_tal_ovw"),
			TEXTVIEW("f_tal_unw"),
			TEXTVIEW("m_med_nrw"),
			TEXTVIEW("m_med_ovw"),
			TEXTVIEW("m_med_unw"),
			TEXTVIEW("m_srt_nrw"),
			TEXTVIEW("m_srt_ovw"),
			TEXTVIEW("m_srt_unw"),
			TEXTVIEW("m_tal_nrw"),
			TEXTVIEW("m_tal_ovw"),
			TEXTVIEW("m_tal_unw"),
		};

		return BodyTypeStrings[BodyTypeIndex];
	}

	static EGender GetGenderFromIndex(int32 BodyTypeIndex)
	{
		FStringView BodyTypeName = GetBodyTypeNameFromIndex(BodyTypeIndex);
		if (BodyTypeName[0] == TEXT('m'))
		{
			return EGender::Male;
		}

		if (BodyTypeName[0] == TEXT('f'))
		{
			return EGender::Female;
		}

		return EGender::Other;
	}

	template<typename T>
	static bool GetPropertyValue(UObject* InObject, FStringView InPropertyName, T& OutPropertyValue)
	{
		if (FProperty* Property = InObject->GetClass()->FindPropertyByName(FName{ InPropertyName }))
		{
			Property->GetValue_InContainer(InObject, &OutPropertyValue);
			return true;
		}

		return false;
	}

	template<typename StructType, typename PropertyType>
	static bool GetStructPropertyValue(const StructType& InStruct, FStringView InPropertyName, PropertyType& OutPropertyValue)
	{
		if (FProperty* Property = StructType::StaticStruct()->FindPropertyByName(FName{ InPropertyName }))
		{
			Property->GetValue_InContainer(&InStruct, &OutPropertyValue);
			return true;
		}
		return false;
	}

	struct FMetaHumanAssetVersion
	{
		FString AssetFilePath;
		FString Version;

		FPackagePath GetPackagePath() const
		{
			return FPackagePath::FromLocalPath(FPaths::ProjectContentDir() / AssetFilePath);
		}
	};

	static bool GetTextureFromMaterial(UMaterialInterface* InMaterial, FName InTextureParameterName, UTexture2D*& OutTexture)
	{
		UTexture* Texture = nullptr;

		const bool bOverridenOnly = false;
		if (InMaterial->GetTextureParameterValue(InTextureParameterName, Texture, bOverridenOnly))
		{
			Texture->WaitForStreaming();
			if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
			{
				OutTexture = Texture2D;
				return true;
			}
		}

		return false;
	}

	static int32 GetTextureResolution(UMaterialInterface* InMaterial, FName InTextureParameterName)
	{
		UTexture2D* Texture2D = nullptr;
		if (GetTextureFromMaterial(InMaterial, InTextureParameterName, Texture2D))
		{
			if (Texture2D->GetImportedSize().X == Texture2D->GetImportedSize().Y)
			{
				return Texture2D->GetImportedSize().X;
			}
		}

		return INDEX_NONE;
	}

	static bool GetStaticSwitchFromMaterial(UMaterialInterface* InMaterial, FName InSwitchParameterName, bool& OutValue)
	{
		FGuid Guid;
		return InMaterial->GetStaticSwitchParameterValue(InSwitchParameterName, OutValue, Guid);
	}

	static bool ConvertGUIHandleToControlRigAxis(FStringView InHandleName, ERigControlAxis& OutControlRigAxis)
	{
		if (InHandleName == TEXTVIEW("tx"))
		{
			OutControlRigAxis = ERigControlAxis::X;
			return true;
		}

		if (InHandleName == TEXTVIEW("ty"))
		{
			OutControlRigAxis = ERigControlAxis::Y;
			return true;
		}

		return false;
	}

	static FString ToString(const FName& InName)
	{
		return InName.ToString();
	}

	static const TArray<FStringView>& GetBaseTestNames()
	{
		static TArray<FStringView> BaseTestNames =
		{
			TEXTVIEW("MetaHuman.Root"),
			TEXTVIEW("MetaHuman.Body"),
			TEXTVIEW("MetaHuman.Face"),
			TEXTVIEW("MetaHuman.Torso"),
			TEXTVIEW("MetaHuman.Legs"),
			TEXTVIEW("MetaHuman.Feet"),
			TEXTVIEW("MetaHuman.Grooms.Hair"),
			TEXTVIEW("MetaHuman.Grooms.Beard"),
			TEXTVIEW("MetaHuman.Grooms.Mustache"),
			TEXTVIEW("MetaHuman.Grooms.Eyelashes"),
			TEXTVIEW("MetaHuman.Grooms.Eyebrows"),
			TEXTVIEW("MetaHuman.Grooms.Fuzz"),
			TEXTVIEW("MetaHuman.Grooms.LODSync"),
		};

		return BaseTestNames;
	}

	static TArray<FString> GenerateTestNames(FStringView InMetaHumanName)
	{
		TArray<FString> TestNames =
		{
			TEXT("MHAssetVersion.Metadata"),
			TEXT("CommonDependencies"),
		};
		Algo::Transform(GetBaseTestNames(), TestNames, [InMetaHumanName](FStringView BaseTestName)
		{
			return FString::Format(TEXT("{0}.{1}"), { BaseTestName, InMetaHumanName });
		});
		return TestNames;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FValidateMetaHumanCommand, FString, Params, FMetaHumanAssetImportDescription, ImportDescription);
bool FValidateMetaHumanCommand::Update()
{
	FAutomationTestFramework& TestFramework = FAutomationTestFramework::Get();
	FAutomationTestBase* Test = TestFramework.GetCurrentTest();
	check(Test);

	// Define convenience macros that can be used inside this command.
	// UTEST_ macros are not available here since this is a latent command and not a base test
	// Also, they must return true to indicate that the test is done.
	// The error messages, if any, will be added by the Test functions and will be displayed in the Message Log window
	#define TEST_TRUE(What, Expr) \
		if (!Test->TestTrue(What, Expr)) { return true; }

	#define TEST_FALSE(What, Expr) \
		if (!Test->TestFalse(What, Expr)) { return true; }

	#define TEST_EQUAL(What, Actual, Expected) \
		if (!Test->TestEqual(What, Actual, Expected)) { return true; }

	#define TEST_GREATER(What, Actual, Expected) \
		if (!Test->TestGreaterThan(What, Actual, Expected)) { return true; }

	#define TEST_GREATER_EQUAL(What, Actual, Expected) \
		if (!Test->TestGreaterEqual(What, Actual, Expected)) { return true; }

	#define TEST_NOT_NULL(What, Expr) \
		if (!Test->TestNotNull(What, Expr)) { return true; }

	#define TEST_NULL(What, Pointer) \
		if (!Test->TestNull(What, Pointer)) { return true; }

	#define TEST_SAME_PTR(What, Actual, Expected) \
		if (!Test->TestSamePtr(What, Actual, Expected)) { return true; }

	#define TEST_VALID(What, Value) \
		if (!Test->TestValid(What, Value)) { return true; }

	#define TEST_INVALID(What, Value) \
		if (!Test->TestInvalid(What, Value)) { return true; }

	FString ExportQuality;

	{
		// The Manifest file test is always executed because parameters read from the Manifest file are used in subsequent tests

		// If ImportDescription.CharacterPath is empty it means we are running from FMetaHumanProjectUtilsExporterTest, if not then
		// this is a full end-to-end test and we can rely on ImportDescription
		FString ManifestFilePath;
		if (ImportDescription.CharacterPath.IsEmpty())
		{
			ManifestFilePath = TestUtils::GetExportManifestFilePath();
		}
		else
		{
			ManifestFilePath = ImportDescription.CharacterPath / TEXT("../..") / TestUtils::GetExportManifestFileName();
		}

		// Test if the Manifest file is valid
		TEST_TRUE("Manifest Exists", FPaths::FileExists(ManifestFilePath));

		TSharedPtr<FJsonObject> ManifestJson = TestUtils::ReadJsonFromFile(ManifestFilePath);
		TEST_TRUE("Read Manifest Json", ManifestJson.IsValid());

		TEST_TRUE("Manifest has metaHumanNames field", ManifestJson->HasTypedField<EJson::Array>(TEXTVIEW("metaHumanNames")));
		TEST_TRUE("Manifest has exportToolVersion field", ManifestJson->HasTypedField<EJson::String>(TEXTVIEW("exportToolVersion")));
		TEST_TRUE("Manifest has exportAssetsKind field", ManifestJson->HasTypedField<EJson::Array>(TEXTVIEW("exportAssetsKind")));
		TEST_TRUE("Manifest has exportQuality field", ManifestJson->HasTypedField<EJson::String>(TEXTVIEW("exportQuality")));
		TEST_TRUE("Manifest has exportedAt field", ManifestJson->HasTypedField<EJson::String>(TEXTVIEW("exportedAt")));

		// Get the list of export asset kind
		const TArray<TSharedPtr<FJsonValue>> ExportAssetKindArray = ManifestJson->GetArrayField(TEXTVIEW("exportAssetsKind"));
		for (TSharedPtr<FJsonValue> ExportAssetKindValue : ExportAssetKindArray)
		{
			FString ExportAssetKindEntry;
			TEST_TRUE("Manifest Export Asset Kind Entry is String", ExportAssetKindValue->TryGetString(ExportAssetKindEntry));
			TEST_TRUE("Manifest Export Asset Kind is valid", TestUtils::GetExportAssetKindValues().Contains(ExportAssetKindEntry));
		}

		// Get the export quality from the manifest
		ExportQuality = ManifestJson->GetStringField(TEXTVIEW("exportQuality"));
		TEST_TRUE("Manifest Export Quality is valid", TestUtils::GetExportQualityLevels().Contains(ExportQuality));
	}

	if (Params.StartsWith("MHAssetVersion."))
	{
		// If ImportDescription.CharacterPath is empty it means we are running from FMetaHumanProjectUtilsExporterTest, if not then
		// this is a full end-to-end test and we can rely on ImportDescription
		FString MHAssetVersionFilePath;
		if (ImportDescription.CharacterName.IsEmpty())
		{
			MHAssetVersionFilePath = TestUtils::GetMHAssetVersionFilePath();
		}
		else
		{
			MHAssetVersionFilePath = ImportDescription.CharacterPath / TEXT("..") / TestUtils::GetMHAssetVersionFileName();
		}

		// Test if the MHAssetVersion is valid
		TEST_TRUE("MHAssetVersion exists", FPaths::FileExists(MHAssetVersionFilePath));

		TSharedPtr<FJsonObject> MHAssetVersionJson = TestUtils::ReadJsonFromFile(MHAssetVersionFilePath);
		TEST_TRUE("Read MHAssetVersion json", MHAssetVersionJson.IsValid());

		const TArray<TSharedPtr<FJsonValue>>* AssetVersionArray;
		TEST_TRUE("MHAssetVersion has assets field", MHAssetVersionJson->TryGetArrayField(TEXTVIEW("assets"), AssetVersionArray))

		TArray<TestUtils::FMetaHumanAssetVersion> MHAssetVersions;

		// Get the list of exported assets from the MHAssetVersion file
		for (TSharedPtr<FJsonValue> AssetVersionValue : *AssetVersionArray)
		{
			const TSharedPtr<FJsonObject>* AssetVersionObject;
			TEST_TRUE("Asset Version is object", AssetVersionValue->TryGetObject(AssetVersionObject));

			TestUtils::FMetaHumanAssetVersion& AssetVersion = MHAssetVersions.AddDefaulted_GetRef();

			TEST_TRUE("Path is valid", (*AssetVersionObject)->TryGetStringField(TEXTVIEW("path"), AssetVersion.AssetFilePath));
			TEST_TRUE("Version is valid", (*AssetVersionObject)->TryGetStringField(TEXTVIEW("version"), AssetVersion.Version));
			TEST_TRUE("Asset file exists", FPaths::FileExists(FPaths::ProjectContentDir() / AssetVersion.AssetFilePath));
		}

		if (Params == TEXT("MHAssetVersion.Metadata"))
		{
			// Check if we can load all assets from MHAssetVersion
			for (const TestUtils::FMetaHumanAssetVersion& MHAssetVersion : MHAssetVersions)
			{
				UObject* Asset = TestUtils::GetAssetByPackageName<UObject>(MHAssetVersion.GetPackagePath().GetPackageName());
				TEST_NOT_NULL("Asset", Asset);

				const FName MHAssetVersionTagName = TEXT("MHAssetVersion");

				const TMap<FName, FString>* MetadataMap = UMetaData::GetMapForObject(Asset);
				TEST_NOT_NULL("Asset Metadata", MetadataMap);
				TEST_TRUE("Asset Metadata contains MHAssetVersion Tag", MetadataMap->Contains(MHAssetVersionTagName));

				const FString& MHAssetVersionTag = (*MetadataMap)[MHAssetVersionTagName];
				TEST_EQUAL("MHVersion Metadata", MHAssetVersion.Version, MHAssetVersionTag);
			}
		}
	}

	if (Params == TEXT("CommonDependencies"))
	{
		// Test if there are any references from Common to MetaHuman assets
		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

		TArray<FAssetData> CommonAssets;
		const bool bRecursive = true;
		AssetRegistry.GetAssetsByPath(FName{ *TestUtils::GetMetaHumanCommonPath() }, CommonAssets, bRecursive);

		// Sanity check to fail if there are no common assets
		TEST_FALSE("Has common assets", CommonAssets.IsEmpty());

		for (const FAssetData& CommonAssetData : CommonAssets)
		{
			TArray<FName> DependencyNames;
			AssetRegistry.GetDependencies(CommonAssetData.PackageName, DependencyNames);

			TArray<FString> Dependencies;
			Dependencies.Reserve(DependencyNames.Num());
			Algo::Transform(DependencyNames, Dependencies, &TestUtils::ToString);

			for (const FString& DependencyName : Dependencies)
			{
				const FString DependencyTestName = FString::Format(TEXT("Common Asset '{0}' shouldn't depend on '{1}'"), { CommonAssetData.GetFullName(), DependencyName });

				// TEXT("/Game/MetaHumans/Common")
				if (DependencyName.StartsWith(TestUtils::GetMetaHumansPath()))
				{
					// if the dependency starts with /Game/MetaHumans, it has to be a common asset
					const FString CommonDependencyTestName = FString::Format(TEXT("Common Asset '{0}' depends on '{1}'"), { CommonAssetData.GetFullName(), DependencyName });
					TEST_TRUE(CommonDependencyTestName, DependencyName.StartsWith(TestUtils::GetMetaHumanCommonPath()));
				}
			}
		}
	}

	// TODO: Replace this with a temp world?
	UWorld* World = GCurrentLevelEditingViewportClient->GetWorld();

	// Sanity check, should never be null
	TEST_NOT_NULL("World is valid", World);

	if (Params.StartsWith("MetaHuman."))
	{
		FString BaseTestName, MetaHumanName;
		TEST_TRUE(TEXT("Get MetaHuman Name"), TestUtils::ParseTestName(Params, BaseTestName, MetaHumanName));

		if (!ImportDescription.CharacterName.IsEmpty())
		{
			TEST_EQUAL(TEXT("MetaHuman Name"), MetaHumanName, ImportDescription.CharacterName);
		}

		auto GetTestName = [&ExportQuality, &MetaHumanName](const FString& InTestName)
		{
			return FString::Format(TEXT("{0}: {1} {2}"), { MetaHumanName, ExportQuality, InTestName });
		};

		for (const FString& Quality : TestUtils::GetExportQualityLevels())
		{
			// If the MetaHuman name has the Quality suffix use it as the current export quality
			if (MetaHumanName.EndsWith(Quality))
			{
				ExportQuality = Quality;
				break;
			}
		}

		const FString MetaHumanBlueprintPackageName = TestUtils::GetMetaHumanBlueprintPackageName(MetaHumanName);
		UBlueprint* MetaHumanBlueprint = TestUtils::GetAssetByPackageName<UBlueprint>(MetaHumanBlueprintPackageName);
		TEST_NOT_NULL(GetTestName(TEXT("MetaHuman blueprint is valid")), MetaHumanBlueprint);

		// Check the export quality Metadata
		UPackage* MetaHumanBlueprintPackage = MetaHumanBlueprint->GetPackage();
		TEST_NOT_NULL(GetTestName(TEXT("MetaHuman blueprint package is valid")), MetaHumanBlueprintPackage);

		const TMap<FName, FString>* MetadataMap = UMetaData::GetMapForObject(MetaHumanBlueprint);
		TEST_NOT_NULL(GetTestName(TEXT("MetaHuman blueprint MetaData is valid")), MetadataMap);

		const FName ExportQualityTagName = TEXT("MHExportQuality");
		TEST_TRUE(GetTestName(TEXT("MetaHuman blueprint contains MHExportQuality Metadata")), MetadataMap->Contains(ExportQualityTagName));

		const FString& ExportQualityMetadataTag = (*MetadataMap)[ExportQualityTagName];
		TEST_EQUAL(GetTestName(TEXT("MetaHuman Export Quality")), ExportQuality, ExportQualityMetadataTag);

		AActor* MetaHumanActor = World->SpawnActor<AActor>(MetaHumanBlueprint->GeneratedClass, FTransform::Identity);
		TEST_NOT_NULL(GetTestName(TEXT("MetaHuman Actor")), MetaHumanActor);

		ON_SCOPE_EXIT
		{
			// Destroy the actor when the scope ends to get the project to its original state
			const bool bNetForce = false;
			const bool bModifyLevel = false;
			Test->TestTrue(GetTestName(TEXT("Destroy")), MetaHumanActor->Destroy(bNetForce, bModifyLevel));
		};

		if (BaseTestName == TEXT("MetaHumans.RootComponent"))
		{
			USceneComponent* RootComponent = TestUtils::GetComponentByName<USceneComponent>(MetaHumanActor, TEXT("Root"));
			TEST_NOT_NULL(GetTestName(TEXT("Root Component")), RootComponent);

			TEST_EQUAL(GetTestName(TEXT("Root Component Tick Group")), RootComponent->PrimaryComponentTick.TickGroup, ETickingGroup::TG_PrePhysics);

			UAssetGuideline* RootAssetGuideline = RootComponent->GetAssetUserData<UAssetGuideline>();
			TEST_NOT_NULL(GetTestName(TEXT("Root Component has Asset Guideline")), RootAssetGuideline);
			TEST_EQUAL(GetTestName(TEXT("Root Component Asset Guideline Name")), RootAssetGuideline->GuidelineName, FName{ TEXT("MH_Groom") });
			TEST_FALSE(GetTestName(TEXT("Root Component Asset Guideline has plugins")), RootAssetGuideline->Plugins.IsEmpty());
			TEST_TRUE(GetTestName(TEXT("Root Component Asset Guideline has HairStrands Plugin")), RootAssetGuideline->Plugins.Contains(TEXT("HairStrands")));
			TEST_TRUE(GetTestName(TEXT("Root Component Asset Guideline has no Project Settings")), RootAssetGuideline->ProjectSettings.IsEmpty());
		}

		// Get the Body component here to test if the other components have it set as the leader pose component
		USkeletalMeshComponent* BodyComponent = TestUtils::GetComponentByName<USkeletalMeshComponent>(MetaHumanActor, TEXT("Body"));

		if (BaseTestName == TEXT("MetaHuman.Body"))
		{
			TEST_NOT_NULL(GetTestName(TEXT("Body Component")), BodyComponent);
			TEST_EQUAL(GetTestName(TEXT("Body Only Tick When Rendered")), BodyComponent->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);

			USkeletalMesh* BodySkeletalMesh = BodyComponent->GetSkeletalMeshAsset();
			TEST_NOT_NULL(GetTestName(TEXT("Body Skeletal Mesh")), BodySkeletalMesh);
			TEST_EQUAL(GetTestName(TEXT("Body Num LODs")), BodySkeletalMesh->GetLODNum(), TestUtils::GetNumLODsForQuality(ExportQuality, BodyComponent->GetName()));

			UMaterialInterface* BodyMaterial = BodyComponent->GetMaterial(0);
			TEST_NOT_NULL(GetTestName(TEXT("Body Material")), BodyMaterial);
			TEST_EQUAL(GetTestName(TEXT("Body Material Name")), BodyMaterial->GetName(), TestUtils::GetBodyMaterialName(ExportQuality, MetaHumanName));

			FString BodyTypeName;
			TEST_TRUE(GetTestName(TEXT("Body type name")), TestUtils::GetBodyTypeNameFromMeshName(BodySkeletalMesh->GetName(), BodyTypeName));

			TEST_NOT_NULL(GetTestName(TEXT("Body Skeleton")), BodySkeletalMesh->GetSkeleton());
			TEST_EQUAL(GetTestName(TEXT("Body Skeleton Name")), BodySkeletalMesh->GetSkeleton()->GetName(), TEXT("metahuman_base_skel"));
			TEST_FALSE(GetTestName(TEXT("Body Default Animating Rig")), BodySkeletalMesh->GetDefaultAnimatingRig().IsNull());
			TEST_EQUAL(GetTestName(TEXT("Body Default Animating Rig Name")), BodySkeletalMesh->GetDefaultAnimatingRig().GetAssetName(), TEXT("MetaHuman_ControlRig"));

			UPhysicsAsset* BodyPhysicsAsset = BodySkeletalMesh->GetPhysicsAsset();
			TEST_NOT_NULL(GetTestName(TEXT("Body Physics Asset")), BodyPhysicsAsset);
			TEST_TRUE(GetTestName(TEXT("Body Physics Asset name")), BodyPhysicsAsset->GetName().StartsWith(BodySkeletalMesh->GetName()));

			auto GetBodySetupTestName = [&GetTestName](const FString& BaseTestName, const USkeletalBodySetup* BodySetup)
			{
				return GetTestName(FString::Format(TEXT("Body Physics Asset {0} for bone {1}"), { BaseTestName, BodySetup->BoneName.ToString() }));
			};

			for (const USkeletalBodySetup* BodySetup : BodyPhysicsAsset->SkeletalBodySetups)
			{
				const EPhysicsType ExpectedPhysicsType = BodySetup->BoneName == TEXT("root") ? EPhysicsType::PhysType_Kinematic : EPhysicsType::PhysType_Default;

				TEST_EQUAL(GetBodySetupTestName(TEXT("Collision Complexity"), BodySetup), BodySetup->CollisionTraceFlag, ECollisionTraceFlag::CTF_UseSimpleAsComplex);
				TEST_EQUAL(GetBodySetupTestName(TEXT("Physics Type"), BodySetup), BodySetup->PhysicsType, ExpectedPhysicsType);
				TEST_EQUAL(GetBodySetupTestName(TEXT("Collision Response"), BodySetup), BodySetup->CollisionReponse, EBodyCollisionResponse::BodyCollision_Enabled);
				TEST_NULL(GetBodySetupTestName(TEXT("Has no physics material"), BodySetup), BodySetup->PhysMaterial);
			}

			const FString RagDollPhysicaAssetPackageName = FPaths::GetPath(BodyPhysicsAsset->GetPackage()->GetName()) / FString::Format(TEXT("{0}_ragdoll"), { BodyTypeName });
			UPhysicsAsset* RagDollPhysicsAsset = TestUtils::GetAssetByPackageName<UPhysicsAsset>(RagDollPhysicaAssetPackageName);
			TEST_NOT_NULL(GetTestName("Body RagDoll Physics Asset"), RagDollPhysicsAsset);

			for (const USkeletalBodySetup* RagdollBodySetup : RagDollPhysicsAsset->SkeletalBodySetups)
			{
				EPhysicsType ExpectedPhysicsType = EPhysicsType::PhysType_Simulated;
				EBodyCollisionResponse::Type ExpectedCollisionResponse = EBodyCollisionResponse::BodyCollision_Enabled;
				float ExpectedAngularDamping = 1.0f;

				if (RagdollBodySetup->BoneName == TEXT("root"))
				{
					ExpectedPhysicsType = PhysType_Kinematic;
					ExpectedCollisionResponse = EBodyCollisionResponse::BodyCollision_Disabled;
					ExpectedAngularDamping = 0.0f;
				}

				TEST_EQUAL(GetBodySetupTestName(TEXT("Ragdoll Collision Complexity"), RagdollBodySetup), RagdollBodySetup->CollisionTraceFlag, ECollisionTraceFlag::CTF_UseSimpleAsComplex);
				TEST_EQUAL(GetBodySetupTestName(TEXT("Ragdoll Physics Type"), RagdollBodySetup), RagdollBodySetup->PhysicsType, ExpectedPhysicsType);
				TEST_EQUAL(GetBodySetupTestName(TEXT("Ragdoll Physics Collision Response"), RagdollBodySetup), RagdollBodySetup->CollisionReponse, ExpectedCollisionResponse);
				TEST_EQUAL(GetBodySetupTestName(TEXT("Ragdoll Angular Damping"), RagdollBodySetup), RagdollBodySetup->DefaultInstance.AngularDamping, ExpectedAngularDamping);

				UPhysicalMaterial* PhysMaterial = RagdollBodySetup->PhysMaterial;
				UPhysicalMaterial* PhysMaterialOverride = nullptr;

				TEST_TRUE(GetBodySetupTestName(TEXT("Ragdoll Get PhysMaterialOverride"), RagdollBodySetup), TestUtils::GetStructPropertyValue(RagdollBodySetup->DefaultInstance, TEXTVIEW("PhysMaterialOverride"), PhysMaterialOverride));

				if (ExpectedCollisionResponse == EBodyCollisionResponse::BodyCollision_Disabled)
				{
					TEST_NULL(GetBodySetupTestName(TEXT("Ragdoll PhysMaterial is valid"), RagdollBodySetup), PhysMaterial);
				}
				else
				{
					TEST_NOT_NULL(GetBodySetupTestName(TEXT("Ragdoll PhysMaterial is valid"), RagdollBodySetup), PhysMaterial);
					TEST_SAME_PTR(GetBodySetupTestName(TEXT("Ragdoll PhysMaterial is same as PhysMaterialOverride"), RagdollBodySetup), PhysMaterial, PhysMaterialOverride);
				}
			}

			const TestUtils::FBodyPostProcessAnimBPSettings& BodyPostProcessAnimBPSettings = TestUtils::GetBodyPostProcessAnimBPSettings(ExportQuality);

			TEST_EQUAL(GetTestName(TEXT("Body Post Process AnimBP LOD Threshold")), BodySkeletalMesh->GetPostProcessAnimBPLODThreshold(), BodyPostProcessAnimBPSettings.LODThreshold);

			USkeletalMeshLODSettings* BodyLODSettings = BodySkeletalMesh->GetLODSettings();
			TEST_NOT_NULL(GetTestName(TEXT("Body LOD Settings")), BodyLODSettings);

			TEST_TRUE("Body LOD Settings Name Has Export Quality Suffix", BodyLODSettings->GetName().EndsWith(ExportQuality));

			TEST_TRUE(GetTestName(TEXT("Body LOD Settings Has Valid Settings")), BodyLODSettings->HasValidSettings());
			TEST_EQUAL(GetTestName(TEXT("Body LOD Settings Num Settings")), BodyLODSettings->GetNumberOfSettings(), BodySkeletalMesh->GetLODNum());

			TEST_TRUE("Body Asset Import Data is Empty", BodySkeletalMesh->GetAssetImportData()->SourceData.SourceFiles.IsEmpty());

			TSubclassOf<UAnimInstance> BodyPostProcessAnimBPClass = BodySkeletalMesh->GetPostProcessAnimBlueprint();
			TEST_NOT_NULL(GetTestName(TEXT("Body Post Process AnimBP Class")), BodyPostProcessAnimBPClass.Get());

			UAnimBlueprint* BodyPostProcessAnimBP = Cast<UAnimBlueprint>(BodyPostProcessAnimBPClass->ClassGeneratedBy);
			TEST_NOT_NULL(GetTestName(TEXT("Body Post Process AnimBP")), BodyPostProcessAnimBP);
			TEST_EQUAL(GetTestName(TEXT("Body Post Process AnimBP name")), BodyPostProcessAnimBP->GetName(), FString::Format(TEXT("{0}_animbp_{1}"), { BodyTypeName, ExportQuality }));
			TEST_NOT_NULL(GetTestName(TEXT("Body Post Process AnimBP Target Skeleton")), BodyPostProcessAnimBP->TargetSkeleton.Get());
			TEST_SAME_PTR(GetTestName(TEXT("Body Post Process AnimBP Target Skeleton is same as Body Skeleton")), BodyPostProcessAnimBP->TargetSkeleton.Get(), BodySkeletalMesh->GetSkeleton());

			UAnimInstance* BodyPostProcessAnimInstance = BodyPostProcessAnimBPClass->GetDefaultObject<UAnimInstance>();
			TEST_NOT_NULL(GetTestName(TEXT("Body Post Process Anim Instance")), BodyPostProcessAnimInstance);

			bool bEnableBodyCorrectives = false;
			TEST_TRUE(GetTestName(TEXT("Body Post Process AnimBP Enable Body Correctives Property")), TestUtils::GetPropertyValue(BodyPostProcessAnimInstance, TEXT("Enable Body Correctives"), bEnableBodyCorrectives));
			TEST_EQUAL(GetTestName(TEXT("Body Post Process AnimBP Enable Body Correctives")), bEnableBodyCorrectives, BodyPostProcessAnimBPSettings.bEnableBodyCorrectives);

			bool bEnableHeadMovementIK = false;
			TEST_TRUE(GetTestName(TEXT("Body Post Process AnimBP Enable Head Movement IK Property")), TestUtils::GetPropertyValue(BodyPostProcessAnimInstance, TEXT("Enable Head Movement IK"), bEnableHeadMovementIK));
			TEST_EQUAL(GetTestName(TEXT("Body Post Process AnimBP Enable Head Movement IK")), bEnableHeadMovementIK, BodyPostProcessAnimBPSettings.bEnableHeadMovementIK);

			if (TestUtils::IsOptimizedExport(ExportQuality))
			{
				TEST_EQUAL(GetTestName(TEXT("Body BaseColor")), TestUtils::GetTextureResolution(BodyMaterial, TEXT("BaseColor")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Body"), ExportQuality, TEXTVIEW("BaseColor")));
				TEST_EQUAL(GetTestName(TEXT("Body Normal")), TestUtils::GetTextureResolution(BodyMaterial, TEXT("Normal")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Body"), ExportQuality, TEXTVIEW("Normal")));
				TEST_EQUAL(GetTestName(TEXT("Body Specular")), TestUtils::GetTextureResolution(BodyMaterial, TEXT("Specular")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Body"), ExportQuality, TEXTVIEW("Specular")));
			}
			else
			{
				TEST_EQUAL(GetTestName(TEXT("Body Color_MAIN Resolution")), TestUtils::GetTextureResolution(BodyMaterial, TEXT("Color_MAIN")), 4096);
				TEST_EQUAL(GetTestName(TEXT("Body Color_UNDERWEAR")), TestUtils::GetTextureResolution(BodyMaterial, TEXT("Color_UNDERWEAR")), 8192);
				TEST_EQUAL(GetTestName(TEXT("Body UnderwearMask")), TestUtils::GetTextureResolution(BodyMaterial, TEXT("UnderwearMask")), 8192);
				TEST_EQUAL(GetTestName(TEXT("Body Normal_MAIN")), TestUtils::GetTextureResolution(BodyMaterial, TEXT("Normal_MAIN")), 8192);
				TEST_EQUAL(GetTestName(TEXT("Body Roughness_MAIN")), TestUtils::GetTextureResolution(BodyMaterial, TEXT("Roughness_MAIN")), 8192);
				TEST_EQUAL(GetTestName(TEXT("Body Cavity_MAIN")), TestUtils::GetTextureResolution(BodyMaterial, TEXT("Cavity_MAIN")), 8192);
			}
		}

		if (BaseTestName == TEXT("MetaHuman.Face"))
		{
			USkeletalMeshComponent* FaceComponent = TestUtils::GetComponentByName<USkeletalMeshComponent>(MetaHumanActor, TEXT("Face"));
			TEST_NOT_NULL(GetTestName(TEXT("Face Component is valid")), FaceComponent);
			TEST_EQUAL(GetTestName(TEXT("Face Only Tick when Rendered")), FaceComponent->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
			TEST_EQUAL(GetTestName(TEXT("Face is a child of Body")), FaceComponent->GetAttachParent()->GetName(), TEXT("Body"));
			TEST_NOT_NULL(GetTestName(TEXT("Face Anim Class")), FaceComponent->AnimClass.Get());
			TEST_EQUAL(GetTestName(TEXT("Face Anim Class Name")), FaceComponent->AnimClass->GetName(), TEXT("Face_AnimBP_C"));

			bool bEnableMaterialParameterCaching = false;
			TEST_TRUE(GetTestName(TEXT("Face Enable Material Parameter Caching Property")), TestUtils::GetPropertyValue(FaceComponent, TEXT("bEnableMaterialParameterCaching"), bEnableMaterialParameterCaching));

			TEST_TRUE(GetTestName(TEXT("Enable Material Parameter Caching is enabled in Face")), bEnableMaterialParameterCaching);

			// Check the face skeletal mesh
			USkeletalMesh* FaceSkelMesh = FaceComponent->GetSkeletalMeshAsset();
			TEST_NOT_NULL(GetTestName(TEXT("Face Skeletal Mesh is valid")), FaceSkelMesh);

			USkeleton* FaceSkeleton = FaceSkelMesh->GetSkeleton();
			TEST_NOT_NULL(GetTestName(TEXT("Face Skeleton")), FaceSkeleton);
			TEST_EQUAL(GetTestName(TEXT("Face Skeleton Name")), FaceSkeleton->GetName(), TEXT("Face_Archetype_Skeleton"));

			TEST_FALSE(GetTestName(TEXT("Face Default Animating Rig")), FaceSkelMesh->GetDefaultAnimatingRig().IsNull());

			UControlRigBlueprint* FaceBoardControlRigBlueprint = Cast<UControlRigBlueprint>(FaceSkelMesh->GetDefaultAnimatingRig().LoadSynchronous());
			TEST_NOT_NULL(GetTestName("Face Default Animating Rig is a ControlRig Blueprint"), FaceBoardControlRigBlueprint);
			TEST_EQUAL(GetTestName(TEXT("Face Default Animating Rig name")), FaceBoardControlRigBlueprint->GetName(), TEXT("Face_ControlBoard_CtrlRig"));

			TEST_NOT_NULL(GetTestName(TEXT("Face Physics Asset")), FaceSkelMesh->GetPhysicsAsset());
			TEST_EQUAL(GetTestName(TEXT("Face Physics Asset Name")), FaceSkelMesh->GetPhysicsAsset()->GetName(), TEXT("Face_Archetype_Physics"));
			TEST_TRUE(GetTestName(TEXT("Face Asset Import Data is Empty")), FaceSkelMesh->GetAssetImportData()->SourceData.SourceFiles.IsEmpty());
			TEST_EQUAL(GetTestName(TEXT("Face Num LODs")), FaceSkelMesh->GetLODNum(), TestUtils::GetNumLODsForQuality(ExportQuality, FaceComponent->GetName()));

			// Basic DNA Asset tests

			UDNAAsset* DNAAsset = Cast<UDNAAsset>(FaceSkelMesh->GetAssetUserData<UDNAAsset>());
			TEST_NOT_NULL(GetTestName(TEXT("DNA Asset")), DNAAsset);
			TEST_TRUE(GetTestName(TEXT("DNA Asset Filename")), DNAAsset->DnaFileName.IsEmpty());
			TEST_NULL(GetTestName(TEXT("DNA Asset Asset Import Data")), DNAAsset->AssetImportData.Get());
			TEST_FALSE(GetTestName(TEXT("DNA Asset bKeepDNAAfterInitialization")), DNAAsset->bKeepDNAAfterInitialization);
			TEST_TRUE(GetTestName(TEXT("DNA Asset Has Body Type Index")), DNAAsset->MetaData.Contains(TEXT("BodyTypeIndex")));

			const int32 BodyTypeIndex = FCString::Atoi(*DNAAsset->MetaData[TEXT("BodyTypeIndex")]);

			TSharedPtr<IDNAReader> BehaviourReader = DNAAsset->GetBehaviorReader();
			TSharedPtr<IDNAReader> GeometryReader = DNAAsset->GetGeometryReader();

			// Verify fields in the DNA's Descriptor Header
			TEST_EQUAL(GetTestName(TEXT("DNA Asset Descriptor Name")), BehaviourReader->GetName(), MetaHumanName);
			TEST_EQUAL(GetTestName(TEXT("DNA Asset Descriptor Age")), BehaviourReader->GetAge(), 0);
			TEST_EQUAL(GetTestName(TEXT("DNA Asset Descriptor Archetype")), BehaviourReader->GetArchetype(), EArchetype::Other);
			TEST_EQUAL(GetTestName(TEXT("DNA Asset Descriptor Gender")), BehaviourReader->GetGender(), TestUtils::GetGenderFromIndex(BodyTypeIndex));
			TEST_EQUAL(GetTestName(TEXT("DNA Asset Descriptor")), BehaviourReader->GetMetaDataCount(), 0);

			// Both readers should be valid, but the Geometry part should be mostly empty
			TEST_NOT_NULL(GetTestName(TEXT("DNA Asset Behaviour")), BehaviourReader.Get());
			TEST_NOT_NULL(GetTestName(TEXT("DNA Asset Geometry")), GeometryReader.Get());

			TEST_EQUAL(GetTestName(TEXT("DNA Asset Behaviour Num LODs")), BehaviourReader->GetLODCount(), FaceSkelMesh->GetLODNum());
			TEST_EQUAL(GetTestName(TEXT("DNA Asset Geometry Num LODs")), GeometryReader->GetLODCount(), 0);

			TEST_EQUAL(GetTestName(TEXT("DNA Asset File Behaviour Generation")), BehaviourReader->GetFileFormatGeneration(), 2);
			TEST_EQUAL(GetTestName(TEXT("DNA Asset File Behaviour Version")), BehaviourReader->GetFileFormatVersion(), 1);

			// TODO: See if Greater Equal is a valid test here: https://jira.it.epicgames.com/browse/MH-12329
			const TArray<UMorphTarget*>& FaceSkelMeshMorphTargets = FaceSkelMesh->GetMorphTargets();
			TEST_GREATER_EQUAL(GetTestName(TEXT("DNA Asset File Behaviour Blend Shapes")), BehaviourReader->GetMeshBlendShapeChannelMappingCount(), FaceSkelMeshMorphTargets.Num());

			if (!FaceSkelMeshMorphTargets.IsEmpty())
			{
				// If we have Morph Targets in the Face Mesh, make sure they are also in the DNA

				// Collect the DNA blend shape names in a set for quick access
				TSet<FString> DNABlendShapes;
				for (uint16 BlendShapeMappingIndex = 0; BlendShapeMappingIndex < BehaviourReader->GetMeshBlendShapeChannelMappingCount(); ++BlendShapeMappingIndex)
				{
					const FMeshBlendShapeChannelMapping BlendShapeChannelMapping = BehaviourReader->GetMeshBlendShapeChannelMapping(BlendShapeMappingIndex);
					const FString MeshName = BehaviourReader->GetMeshName(BlendShapeChannelMapping.MeshIndex);
					const FString BlendShapeName = BehaviourReader->GetBlendShapeChannelName(BlendShapeChannelMapping.BlendShapeChannelIndex);
					DNABlendShapes.Add(FString::Format(TEXT("{0}__{1}"), { MeshName, BlendShapeName }));
				}

				// Now make sure all MorphTargets from the Skeletal Mesh are in the DNA
				for (UMorphTarget* MorphTarget : FaceSkelMeshMorphTargets)
				{
					const FString MorphTargetTestName = FString::Format(TEXT("Morph Target '{0}' is in DNA"), { MorphTarget->GetName() });
					TEST_TRUE(GetTestName(MorphTargetTestName), DNABlendShapes.Contains(MorphTarget->GetName()));

					auto GetMorphTargetTestName = [&GetTestName, FaceSkeleton, MorphTarget](const FString& InBaseTestName)
						{
							return FString::Format(TEXT("Face Skeleton '{0}' Morph Target '{1}' {2}"), { FaceSkeleton->GetName(), MorphTarget->GetName(), InBaseTestName });
						};

					// Check if the skeleton has the morph target as a curve
					const FCurveMetaData* FaceSkeletonCurveMetadata = FaceSkeleton->GetCurveMetaData(MorphTarget->GetFName());
					TEST_NOT_NULL(GetMorphTargetTestName(TEXT("Has Curve Metadata")), FaceSkeletonCurveMetadata);
					TEST_FALSE(GetMorphTargetTestName(TEXT("Curve is not of Material Type")), FaceSkeletonCurveMetadata->Type.bMaterial);
					TEST_TRUE(GetMorphTargetTestName(TEXT("Curve is of Morph Target Type")), FaceSkeletonCurveMetadata->Type.bMorphtarget);
				}
			}

			// Advanced DNA Tests

			TEST_EQUAL(GetTestName(TEXT("DNA Asset File Geometry Blend Shapes")), GeometryReader->GetMeshBlendShapeChannelMappingCount(), 0);

			TEST_GREATER(GetTestName(TEXT("DNA has GUI Controls")), BehaviourReader->GetGUIControlCount(), 0);
			TEST_GREATER(GetTestName(TEXT("DNA has Raw Controls")), BehaviourReader->GetRawControlCount(), 0);

			for (uint16 RawControlIndex = 0; RawControlIndex < BehaviourReader->GetRawControlCount(); ++RawControlIndex)
			{
				FString RawControlName = BehaviourReader->GetRawControlName(RawControlIndex);
				RawControlName.ReplaceCharInline(TEXT('.'), TEXT('_'));

				auto GetRawControlTestName = [&GetTestName, FaceSkeleton, &RawControlName](const FString& InBaseTestName)
					{
						return GetTestName(FString::Format(TEXT("Face Skeleton '{0}' Raw Control '{1}' {2}"), { FaceSkeleton->GetName(), RawControlName, InBaseTestName }));
					};

				const FCurveMetaData* FaceSkeletonCurveMetadata = FaceSkeleton->GetCurveMetaData(FName{ *RawControlName });
				TEST_NOT_NULL(GetRawControlTestName("Has Curve Metadata"), FaceSkeletonCurveMetadata);
				TEST_FALSE(GetRawControlTestName(TEXT("Curve is not of Material Type")), FaceSkeletonCurveMetadata->Type.bMaterial);
				TEST_FALSE(GetRawControlTestName(TEXT("Curve is not of Morph Target Type")), FaceSkeletonCurveMetadata->Type.bMorphtarget);
			}

			UControlRig* FaceBoardControlRig = FaceBoardControlRigBlueprint->CreateControlRig();
			TEST_NOT_NULL(GetTestName(TEXT("Face Board ControlRig is valid")), FaceBoardControlRig);

			URigHierarchy* FaceBoardRigHierarchy = FaceBoardControlRig->GetHierarchy();
			TEST_NOT_NULL(GetTestName(TEXT("Face Board ControlRig RigHierarchy is valid")), FaceBoardRigHierarchy);

			const TArray<FRigControlElement*> FaceBoardControls = FaceBoardRigHierarchy->GetControls();
			TEST_FALSE(GetTestName(TEXT("Face Board ControlRig has controls")), FaceBoardControls.IsEmpty());

			TSet<FString> GUIControls;
			for (uint16 GUIControlIndex = 0; GUIControlIndex < BehaviourReader->GetGUIControlCount(); ++GUIControlIndex)
			{
				GUIControls.Add(BehaviourReader->GetGUIControlName(GUIControlIndex));
			}

			for (uint16 GUIControlIndex = 0; GUIControlIndex < BehaviourReader->GetGUIControlCount(); ++GUIControlIndex)
			{
				FString GUIControlFullName = BehaviourReader->GetGUIControlName(GUIControlIndex);

				FString GUIControlName;
				FString GUIControlHandle;
				auto GetControlTestName = [&GetTestName, &GUIControlName](const FString& ControlTestName)
					{
						return GetTestName(FString::Format(TEXT("Face Board ControlRig Control '{0}' {1}"), { GUIControlName, ControlTestName }));
					};

				TEST_TRUE(GetControlTestName(TEXT("Split GUIControlFullName")), GUIControlFullName.Split(TEXT("."), &GUIControlName, &GUIControlHandle));

				const FRigControlElement* const* FoundControlElement = FaceBoardControls.FindByPredicate([&GUIControlName](const FRigControlElement* CandidateControl)
																										 {
																											 return CandidateControl->GetName() == GUIControlName;
																										 });
				const FString FoundControlInControlRigTestName = FString::Format(TEXT("Face Board ControlRig has DNA Control '{0}'"), { GUIControlName });
				TEST_NOT_NULL(GetControlTestName(TEXT("Has DNA Control")), FoundControlElement);

				ERigControlAxis ExpectedPrimaryAxis;
				const FString PrimaryAxisTestName = FString::Format(TEXT("Face Board Control Rig Convert Primary Axis for Control '{0}'"), { GUIControlName });
				TEST_TRUE(GetControlTestName(TEXT("Convert Primary Axis")), TestUtils::ConvertGUIHandleToControlRigAxis(GUIControlHandle, ExpectedPrimaryAxis));

				const FRigControlElement* RigControlElement = *FoundControlElement;
				if (RigControlElement->Settings.ControlType == ERigControlType::Float)
				{
					TEST_EQUAL(GetControlTestName(TEXT("Primary Axis for Float Control")), ExpectedPrimaryAxis, RigControlElement->Settings.PrimaryAxis);
				}
				else if (RigControlElement->Settings.ControlType == ERigControlType::Vector2D)
				{
					TEST_EQUAL(GetControlTestName(TEXT("Primary Axis for Vector2D Control")), RigControlElement->Settings.PrimaryAxis, ERigControlAxis::Z);

					// if the control is a Vector2D, there must be another control in the DNA following this one with the handle .tx
					const FString NextGUIControlFullName = FString::Format(TEXT("{0}.tx"), { GUIControlName });
					TEST_TRUE(GetControlTestName(FString::Format(TEXT("DNA has Next GUI Control '{0}'"), { NextGUIControlFullName })), GUIControls.Contains(NextGUIControlFullName));
				}
				else
				{
					// All DNA GUI Controls are expected to be Float or Vector2D so fail the test here
					TEST_TRUE(GetTestName(TEXT("Invalid Control Type")), false);
				}
			}

			for (uint16 AnimatedMapIndex = 0; AnimatedMapIndex < BehaviourReader->GetAnimatedMapCount(); ++AnimatedMapIndex)
			{
				FString AnimatedMapName = BehaviourReader->GetAnimatedMapName(AnimatedMapIndex);
				AnimatedMapName.ReplaceCharInline(TEXT('.'), TEXT('_'));

				auto GetAnimatedMapTestName = [&GetTestName, FaceSkeleton, &AnimatedMapName](const FString& InBaseTestName)
					{
						return GetTestName(FString::Format(TEXT("Face Skeleton '{0}' Animated Map '{1}' {2}"), { FaceSkeleton->GetName(), AnimatedMapName, InBaseTestName }));
					};

				const FCurveMetaData* FaceSkeletonCurveMetadata = FaceSkeleton->GetCurveMetaData(FName{ *AnimatedMapName });
				TEST_NOT_NULL(GetAnimatedMapTestName(TEXT("Has Curve Metadata")), FaceSkeletonCurveMetadata);
				TEST_TRUE(GetAnimatedMapTestName(TEXT("Curve is of Material Type")), FaceSkeletonCurveMetadata->Type.bMaterial);
				TEST_FALSE(GetAnimatedMapTestName(TEXT("Curve is not of type Morph Target")), FaceSkeletonCurveMetadata->Type.bMorphtarget);
			}

			for (uint16 JointIndex = 0; JointIndex < BehaviourReader->GetJointCount(); ++JointIndex)
			{
				const uint16 ParentJointIndex = BehaviourReader->GetJointParentIndex(JointIndex);

				const FString JointName = BehaviourReader->GetJointName(JointIndex);
				const FString ParentJointName = BehaviourReader->GetJointName(ParentJointIndex);

				const FReferenceSkeleton& RefSkeleton = FaceSkeleton->GetReferenceSkeleton();
				const TArray<FMeshBoneInfo>& RawMeshBoneInfo = RefSkeleton.GetRawRefBoneInfo();

				const FMeshBoneInfo* FoundMeshBoneInfo = RawMeshBoneInfo.FindByPredicate([&JointName](const FMeshBoneInfo& CandidateMeshBoneInfo)
																						 {
																							 return CandidateMeshBoneInfo.Name.ToString() == JointName;
																						 });

				auto GetJointTestName = [&GetTestName, FaceSkeleton, &JointName](const FString& InBaseTestName)
				{
					return FString::Format(TEXT("Face Skeleton '{0}' Bone Name '{1}' {2}"), { FaceSkeleton->GetName(), JointName, InBaseTestName });
				};

				TEST_NOT_NULL(GetJointTestName(TEXT("Found Bone in Skeleton")), FoundMeshBoneInfo);

				// In the DNA, if the parent joint index is the same it means its the root joint
				if (ParentJointIndex != JointIndex)
				{
					TEST_EQUAL(GetJointTestName(TEXT("Parent Bone")), RawMeshBoneInfo[FoundMeshBoneInfo->ParentIndex].Name.ToString(), ParentJointName);
				}
			}

			const TestUtils::FFacePostProcessAnimBPSettings& FacePostProcessAnimBPSettings = TestUtils::GetFacePostProcessAnimBPSettings(ExportQuality);

			TEST_EQUAL(GetTestName(TEXT("Face Post Process AnimBP LOD Threshold")), FaceSkelMesh->GetPostProcessAnimBPLODThreshold(), FacePostProcessAnimBPSettings.LODThreshold);

			TSubclassOf<UAnimInstance> FacePostProcessAnimBPClass = FaceSkelMesh->GetPostProcessAnimBlueprint();
			TEST_NOT_NULL(GetTestName(TEXT("Face Post Process AnimBP Class")), FacePostProcessAnimBPClass.Get());

			UAnimBlueprint* FacePostProcessAnimBP = Cast<UAnimBlueprint>(FacePostProcessAnimBPClass->ClassGeneratedBy);
			TEST_NOT_NULL(GetTestName(TEXT("Face Post Process AnimBP")), FacePostProcessAnimBP);
			TEST_EQUAL(GetTestName(TEXT("Face Post Process Anim name")), FacePostProcessAnimBP->GetName(), FString::Format(TEXT("ABP_{0}_FaceMesh_PostProcess"), { MetaHumanName }));
			TEST_EQUAL(GetTestName(TEXT("Face Post Process AnimBP Parent Class")), UAnimBlueprint::GetParentAnimBlueprint(FacePostProcessAnimBP)->GetName(), TEXT("Face_PostProcess_AnimBP"));
			TEST_NOT_NULL(GetTestName(TEXT("Face Post Process AnimBP Target Skeleton")), FacePostProcessAnimBP->TargetSkeleton.Get());
			TEST_SAME_PTR(GetTestName(TEXT("Face Post Process AnimBP Target Skeleton is same as Face Skeleton")), FacePostProcessAnimBP->TargetSkeleton.Get(), FaceSkelMesh->GetSkeleton());

			UAnimInstance* FacePostProcessAnimInstance = FacePostProcessAnimBPClass->GetDefaultObject<UAnimInstance>();
			TEST_NOT_NULL(GetTestName(TEXT("Face Post Process Anim Instance")), FacePostProcessAnimInstance);

			int32 RigLogicLODThreshold = INDEX_NONE;
			TEST_TRUE(GetTestName(TEXT("Rig Logic LOD Threshold Property")), TestUtils::GetPropertyValue(FacePostProcessAnimInstance, TEXT("Rig Logic LOD Threshold"), RigLogicLODThreshold));
			TEST_EQUAL(GetTestName(TEXT("Rig Logic LOD Threshold")), RigLogicLODThreshold, FacePostProcessAnimBPSettings.RigLogicLODTheshold);

			bool bEnableNeckCorrectives = false;
			TEST_TRUE(GetTestName(TEXT("Enable Neck Correctives Property")), TestUtils::GetPropertyValue(FacePostProcessAnimInstance, TEXT("Enable Neck Correctives"), bEnableNeckCorrectives));
			TEST_EQUAL(GetTestName(TEXT("Enable Neck Correctives")), bEnableNeckCorrectives, FacePostProcessAnimBPSettings.bEnableNeckCorrectives);

			int32 NeckCorrectivesLODThreshold = INDEX_NONE;
			TEST_TRUE(GetTestName(TEXT("Neck Correctives LOD Threshold Property")), TestUtils::GetPropertyValue(FacePostProcessAnimInstance, TEXT("Neck Correctives LOD Threshold"), NeckCorrectivesLODThreshold));
			TEST_EQUAL(GetTestName(TEXT("Neck Correctives LOD Threshold")), NeckCorrectivesLODThreshold, FacePostProcessAnimBPSettings.NeckCorrectivesLODThreshold);

			bool bEnableNeckProceduralControlRig = false;
			TEST_TRUE(GetTestName(TEXT("Enable Neck Procedural Control Rig Property")), TestUtils::GetPropertyValue(FacePostProcessAnimInstance, TEXT("Enable Neck Procedural Control Rig"), bEnableNeckProceduralControlRig));
			TEST_EQUAL(GetTestName(TEXT("Enable Neck Procedural Control Rig")), bEnableNeckProceduralControlRig, FacePostProcessAnimBPSettings.bEnableNeckProceduralControlRig);

			int32 NeckProceduralControlRigLODThreshold = INDEX_NONE;
			TEST_TRUE(GetTestName(TEXT("Neck Procedural Control Rig LOD Threshold Property")), TestUtils::GetPropertyValue(FacePostProcessAnimInstance, TEXT("Neck Procedural Control Rig LOD Threshold"), NeckProceduralControlRigLODThreshold));
			TEST_EQUAL(GetTestName(TEXT("Neck Procedural Control Rig LOD Threshold")), NeckProceduralControlRigLODThreshold, FacePostProcessAnimBPSettings.NeckProceduralControlRigLODThreshold);

			if (bEnableNeckCorrectives)
			{
				// If neck correctives are enabled by the exporter there should be a pose asset set
				UPoseAsset* NeckCorrectivePoseAsset = nullptr;
				TEST_TRUE(GetTestName(TEXT("Neck Corrective Pose Asset Property")), TestUtils::GetPropertyValue(FacePostProcessAnimInstance, TEXT("Neck Corrective Pose Asset"), NeckCorrectivePoseAsset));
				TEST_NOT_NULL(GetTestName(TEXT("Neck Corrective Pose Asset")), NeckCorrectivePoseAsset);
				TEST_EQUAL(GetTestName(TEXT("Neck Corrective Pose Asset Name")), NeckCorrectivePoseAsset->GetName(), FString::Format(TEXT("neckCorr_{0}_RBFSolver_pose"), { TestUtils::GetBodyTypeNameFromIndex(BodyTypeIndex) }));
			}

			for (int32 LODInfoIndex = 0; LODInfoIndex < FaceSkelMesh->GetLODNum(); ++LODInfoIndex)
			{
				const FSkeletalMeshLODInfo* LODInfo = FaceSkelMesh->GetLODInfo(LODInfoIndex);
				TEST_NOT_NULL(GetTestName(FString::Format(TEXT("LOD {0}"), { LODInfoIndex })), LODInfo);
				TEST_EQUAL(GetTestName(FString::Format(TEXT("LOD {0} has Skin Cache Enabled"), { LODInfoIndex })), LODInfo->SkinCacheUsage, ESkinCacheUsage::Enabled);
			}

			USkeletalMeshLODSettings* FaceLODSettings = FaceSkelMesh->GetLODSettings();
			TEST_NOT_NULL(GetTestName(TEXT("Face LOD Settings")), FaceLODSettings);

			TEST_TRUE("Face LOD Settings Name Has Export Quality Suffix", FaceLODSettings->GetName().EndsWith(ExportQuality));

			TEST_TRUE(GetTestName(TEXT("Face LOD Settings Has Valid Settings")), FaceLODSettings->HasValidSettings());
			TEST_EQUAL(GetTestName(TEXT("Face LOD Settings Num Settings")), FaceLODSettings->GetNumberOfSettings(), FaceSkelMesh->GetLODNum());

			// Check the Face Asset Guidelines
			UAssetGuideline* AssetGuideline = FaceSkelMesh->GetAssetUserData<UAssetGuideline>();
			TEST_NOT_NULL(GetTestName(TEXT("Face Skeletal Mesh Asset Guideline")), AssetGuideline);
			TEST_EQUAL(GetTestName(TEXT("Face Skeletal Mesh Asset Guideline Name")), AssetGuideline->GuidelineName, FName{ TEXT("MH_LOD_012_SkelMesh") });
			TEST_FALSE(GetTestName(TEXT("Face Skeletal Mesh Asset Guideline has plugins")), AssetGuideline->Plugins.IsEmpty());
			TEST_TRUE(GetTestName(TEXT("Face Skeletal Mesh Asset Guideline has RigLogic plugin")), AssetGuideline->Plugins.Contains(TEXT("RigLogic")));
			TEST_GREATER_EQUAL(GetTestName(TEXT("Face Skeletal Mesh Asset Guideline has Project Settings")), AssetGuideline->ProjectSettings.Num(), 5);

			const FIniStringValue Support16BitBoneIndexGuideline =
			{
				.Section = TEXT("/Script/Engine.RendererSettings"),
				.Key = TEXT("r.GPUSkin.Support16BitBoneIndex"),
				.Value = TEXT("True"),
				.Filename = TEXT("/Config/DefaultEngine.ini")
			};

			const FIniStringValue UnlimitedBoneInfluencesGuideline =
			{
				.Section = TEXT("/Script/Engine.RendererSettings"),
				.Key = TEXT("r.GPUSkin.UnlimitedBoneInfluences"),
				.Value = TEXT("True"),
				.Filename = TEXT("/Config/DefaultEngine.ini")
			};

			const FIniStringValue BlendUsingVertexColorForRecomputeTangentsGuideline =
			{
				.Section = TEXT("/Script/Engine.RendererSettings"),
				.Key = TEXT("r.SkinCache.BlendUsingVertexColorForRecomputeTangents"),
				.Value = TEXT("2"),
				.Filename = TEXT("/Config/DefaultEngine.ini")
			};

			const FIniStringValue SkinCacheCompileShadersGuideline =
			{
				.Section = TEXT("/Script/Engine.RendererSettings"),
				.Key = TEXT("r.SkinCache.CompileShaders"),
				.Value = TEXT("True"),
				.Filename = TEXT("/Config/DefaultEngine.ini")
			};

			const FIniStringValue UseExperimentalChunkingGuideline =
			{
				.Section = TEXT("/Script/Engine.RendererSettings"),
				.Key = TEXT("SkeletalMesh.UseExperimentalChunking"),
				.Value = TEXT("1"),
				.Filename = TEXT("/Config/DefaultEngine.ini")
			};

			TEST_TRUE(GetTestName(TEXT("Face Skeletal Mesh Asset Guideline has r.GPUSkin.Support16BitBoneIndex")), AssetGuideline->ProjectSettings.Contains(Support16BitBoneIndexGuideline));
			TEST_TRUE(GetTestName(TEXT("Face Skeletal Mesh Asset Guideline has r.GPUSkin.UnlimitedBoneInfluences")), AssetGuideline->ProjectSettings.Contains(UnlimitedBoneInfluencesGuideline));
			TEST_TRUE(GetTestName(TEXT("Face Skeletal Mesh Asset Guideline has r.SkinCache.BlendUsingVertexColorForRecomputeTangents")), AssetGuideline->ProjectSettings.Contains(BlendUsingVertexColorForRecomputeTangentsGuideline));
			TEST_TRUE(GetTestName(TEXT("Face Skeletal Mesh Asset Guideline has r.SkinCache.CompileShaers")), AssetGuideline->ProjectSettings.Contains(SkinCacheCompileShadersGuideline));
			TEST_TRUE(GetTestName(TEXT("Face Skeletal Mesh Asset Guideline has SkeletalMesh.UseExperimentalChuncking")), AssetGuideline->ProjectSettings.Contains(UseExperimentalChunkingGuideline));

			// Check if texture resolutions match the export quality
			if (TestUtils::IsOptimizedExport(ExportQuality))
			{
				if (ExportQuality == TEXT("High"))
				{
					UMaterialInterface* HeadMaterialLOD1 = FaceComponent->GetMaterialByName(TEXT("head_LOD1_shader_shader"));
					TEST_NOT_NULL(GetTestName(TEXT("Head LOD1 Material")), HeadMaterialLOD1);

					bool bUseAnimatedBaseColor = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD1 bUseAnimatedBaseColor Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD1, TEXT("bUseAnimatedBaseColor"), bUseAnimatedBaseColor));
					TEST_TRUE(GetTestName(TEXT("Head LOD1 bUseAnimatedBaseColor")), bUseAnimatedBaseColor);

					bool bUseAnimatedNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD1 bUseAnimatedNormals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD1, TEXT("bUseAnimatedNormals"), bUseAnimatedNormals));
					TEST_TRUE(GetTestName(TEXT("Head LOD1 bUseAnimatedNormals")), bUseAnimatedNormals);

					TEST_EQUAL(GetTestName(TEXT("Head LOD1 BaseColor Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("BaseColor")), 1024);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 BaseColor_CM1 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("BaseColor_CM1")), 512);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 BaseColor_CM2 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("BaseColor_CM2")), 512);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 BaseColor_CM3 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("BaseColor_CM3")), 512);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Normal Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Normal")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Normal_WM1 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Normal_WM1")), 512);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Normal_WM2 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Normal_WM2")), 512);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Normal_WM3 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Normal_WM3")), 512);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Specular Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Specular")), 1024);
				}

				if (ExportQuality == TEXT("High") || ExportQuality == TEXT("Medium"))
				{
					UMaterialInterface* HeadMaterialLOD3 = FaceComponent->GetMaterialByName(TEXT("head_LOD3_shader_shader"));
					TEST_NOT_NULL(GetTestName(TEXT("Head LOD3 Material")), HeadMaterialLOD3);

					bool bUseAnimatedBaseColor = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD3 bUseAnimatedBaseColor Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD3, TEXT("bUseAnimatedBaseColor"), bUseAnimatedBaseColor));
					TEST_FALSE(GetTestName(TEXT("Head LOD3 bUseAnimatedBaseColor")), bUseAnimatedBaseColor);

					bool bUseAnimatedNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD3 bUseAnimatedNormals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD3, TEXT("bUseAnimatedNormals"), bUseAnimatedNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD3 bUseAnimatedNormals")), bUseAnimatedNormals);

					TEST_EQUAL(GetTestName(TEXT("Head LOD3 BaseColor Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD3, TEXT("BaseColor")), 1024);
					TEST_EQUAL(GetTestName(TEXT("Head LOD3 Normal Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD3, TEXT("Normal")), 1024);
					TEST_EQUAL(GetTestName(TEXT("Head LOD3 Specular Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD3, TEXT("Specular")), 1024);
				}

				if (ExportQuality == TEXT("High") || ExportQuality == TEXT("Medium") || ExportQuality == TEXT("Low"))
				{
					UMaterialInterface* HeadMaterialLOD5 = FaceComponent->GetMaterialByName(TEXT("head_LOD57_shader_shader"));
					TEST_NOT_NULL(GetTestName(TEXT("Head LOD57 Material")), HeadMaterialLOD5);

					bool bUseAnimatedBaseColor = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD57 bUseAnimatedBaseColor Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD5, TEXT("bUseAnimatedBaseColor"), bUseAnimatedBaseColor));
					TEST_FALSE(GetTestName(TEXT("Head LOD57 bUseAnimatedBaseColor")), bUseAnimatedBaseColor);

					bool bUseAnimatedNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD57 bUseAnimatedNormals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD5, TEXT("bUseAnimatedNormals"), bUseAnimatedNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD57 bUseAnimatedNormals")), bUseAnimatedNormals);

					TEST_EQUAL(GetTestName(TEXT("Head LOD57 BaseColor Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD5, TEXT("BaseColor")), 512);
					TEST_EQUAL(GetTestName(TEXT("Head LOD57 Normal Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD5, TEXT("Normal")), 512);
					TEST_EQUAL(GetTestName(TEXT("Head LOD57 Specular Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD5, TEXT("Specular")), 512);
				}
			}
			else
			{
				{
					UMaterialInterface* HeadMaterialLOD0 = FaceComponent->GetMaterialByName(TEXT("head_shader_shader"));
					TEST_NOT_NULL(GetTestName(TEXT("Head LOD0 Material")), HeadMaterialLOD0);

					bool bAnimatedAlbedo = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD0 Animated Albedo Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD0, TEXT("Animated Albedo"), bAnimatedAlbedo));
					TEST_TRUE(GetTestName(TEXT("Head LOD0 Animated Albedo")), bAnimatedAlbedo);

					bool bAnimatedNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD0 Animated Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD0, TEXT("Animated Normals"), bAnimatedNormals));
					TEST_TRUE(GetTestName(TEXT("Head LOD0 Animated Normals")), bAnimatedNormals);

					bool bDetailNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD0 Detail Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD0, TEXT("Detail Normals"), bDetailNormals));
					TEST_TRUE(GetTestName(TEXT("Head LOD0 Detail Normals")), bDetailNormals);

					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Color_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Color_MAIN")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Color_CM1 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Color_CM1")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Color_CM2 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Color_CM2")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Color_CM3 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Color_CM3")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Normal_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Normal_MAIN")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Normal_WM1 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Normal_WM1")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Normal_WM2 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Normal_WM2")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Normal_WM3 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Normal_WM3")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Normal_BAKED Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Normal_BAKED")), 256);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Normal_MICRO Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Normal_MICRO")), 1024);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Roughness_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Roughness_MAIN")), 4096);
					TEST_EQUAL(GetTestName(TEXT("Head LOD0 Cavity_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD0, TEXT("Cavity_MAIN")), 8192);
				}

				{
					UMaterialInterface* HeadMaterialLOD1 = FaceComponent->GetMaterialByName(TEXT("head_LOD1_shader_shader"));
					TEST_NOT_NULL(GetTestName(TEXT("Head LOD1 Material")), HeadMaterialLOD1);

					bool bAnimatedAlbedo = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD1 Animated Albedo Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD1, TEXT("Animated Albedo"), bAnimatedAlbedo));
					TEST_TRUE(GetTestName(TEXT("Head LOD1 Animated Albedo")), bAnimatedAlbedo);

					bool bAnimatedNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD1 Animated Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD1, TEXT("Animated Normals"), bAnimatedNormals));
					TEST_TRUE(GetTestName(TEXT("Head LOD1 Animated Normals")), bAnimatedNormals);

					bool bDetailNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD1 Detail Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD1, TEXT("Detail Normals"), bDetailNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD1 Detail Normals")), bDetailNormals);

					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Color_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Color_MAIN")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Color_CM1 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Color_CM1")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Color_CM2 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Color_CM2")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Color_CM3 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Color_CM3")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Normal_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Normal_MAIN")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Normal_WM1 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Normal_WM1")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Normal_WM2 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Normal_WM2")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Normal_WM3 Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Normal_WM3")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Normal_BAKED Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Normal_BAKED")), 256);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Roughness_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Roughness_MAIN")), 4096);
					TEST_EQUAL(GetTestName(TEXT("Head LOD1 Cavity_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD1, TEXT("Cavity_MAIN")), 8192);
				}

				{
					UMaterialInterface* HeadMaterialLOD2 = FaceComponent->GetMaterialByName(TEXT("head_LOD2_shader_shader"));
					TEST_NOT_NULL(GetTestName(TEXT("Head LOD2 Material")), HeadMaterialLOD2);

					bool bAnimatedAlbedo = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD2 Animated Albedo Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD2, TEXT("Animated Albedo"), bAnimatedAlbedo));
					TEST_FALSE(GetTestName(TEXT("Head LOD2 Animated Albedo")), bAnimatedAlbedo);

					bool bAnimatedNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD2 Animated Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD2, TEXT("Animated Normals"), bAnimatedNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD2 Animated Normals")), bAnimatedNormals);

					bool bDetailNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD2 Detail Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD2, TEXT("Detail Normals"), bDetailNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD2 Detail Normals")), bDetailNormals);

					TEST_EQUAL(GetTestName(TEXT("Head LOD2 Color_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD2, TEXT("Color_MAIN")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD2 Normal_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD2, TEXT("Normal_MAIN")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD2 Normal_BAKED Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD2, TEXT("Normal_BAKED")), 256);
					TEST_EQUAL(GetTestName(TEXT("Head LOD2 Roughness_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD2, TEXT("Roughness_MAIN")), 4096);
					TEST_EQUAL(GetTestName(TEXT("Head LOD2 Cavity_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD2, TEXT("Cavity_MAIN")), 8192);
				}

				{
					UMaterialInterface* HeadMaterialLOD3 = FaceComponent->GetMaterialByName(TEXT("head_LOD3_shader_shader"));
					TEST_NOT_NULL(GetTestName(TEXT("Cinematic Head LOD3 Material")), HeadMaterialLOD3);

					bool bAnimatedAlbedo = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD 0 Animated Albedo Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD3, TEXT("Animated Albedo"), bAnimatedAlbedo));
					TEST_FALSE(GetTestName(TEXT("Head LOD 0 Animated Albedo")), bAnimatedAlbedo);

					bool bAnimatedNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD 0 Animated Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD3, TEXT("Animated Normals"), bAnimatedNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD 0 Animated Normals")), bAnimatedNormals);

					bool bDetailNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD 0 Detail Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD3, TEXT("Detail Normals"), bDetailNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD 0 Detail Normals")), bDetailNormals);

					TEST_EQUAL(GetTestName(TEXT("Head LOD3 Color_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD3, TEXT("Color_MAIN")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD3 Normal_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD3, TEXT("Normal_MAIN")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD3 Normal_BAKED Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD3, TEXT("Normal_BAKED")), 256);
					TEST_EQUAL(GetTestName(TEXT("Head LOD3 Roughness_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD3, TEXT("Roughness_MAIN")), 4096);
					TEST_EQUAL(GetTestName(TEXT("Head LOD3 Cavity_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD3, TEXT("Cavity_MAIN")), 8192);
				}

				{
					UMaterialInterface* HeadMaterialLOD4 = FaceComponent->GetMaterialByName(TEXT("head_LOD3_shader_shader"));
					TEST_NOT_NULL(GetTestName(TEXT("Head LOD4 Material")), HeadMaterialLOD4);

					bool bAnimatedAlbedo = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD4 Animated Albedo Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD4, TEXT("Animated Albedo"), bAnimatedAlbedo));
					TEST_FALSE(GetTestName(TEXT("Head LOD4 Animated Albedo")), bAnimatedAlbedo);

					bool bAnimatedNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD4 Animated Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD4, TEXT("Animated Normals"), bAnimatedNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD4 Animated Normals")), bAnimatedNormals);

					bool bDetailNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD4 Detail Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD4, TEXT("Detail Normals"), bDetailNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD4 Detail Normals")), bDetailNormals);

					TEST_EQUAL(GetTestName(TEXT("Head LOD4 Color_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD4, TEXT("Color_MAIN")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD4 Normal_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD4, TEXT("Normal_MAIN")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD4 Normal_BAKED Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD4, TEXT("Normal_BAKED")), 256);
					TEST_EQUAL(GetTestName(TEXT("Head LOD4 Roughness_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD4, TEXT("Roughness_MAIN")), 4096);
					TEST_EQUAL(GetTestName(TEXT("Head LOD4 Cavity_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD4, TEXT("Cavity_MAIN")), 8192);
				}

				{
					UMaterialInterface* HeadMaterialLOD57 = FaceComponent->GetMaterialByName(TEXT("head_LOD57_shader_shader"));
					TEST_NOT_NULL(GetTestName(TEXT("Head LOD57 Material")), HeadMaterialLOD57);

					bool bAnimatedAlbedo = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD57 Animated Albedo Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD57, TEXT("Animated Albedo"), bAnimatedAlbedo));
					TEST_FALSE(GetTestName(TEXT("Head LOD57 Animated Albedo")), bAnimatedAlbedo);

					bool bAnimatedNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD57 Animated Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD57, TEXT("Animated Normals"), bAnimatedNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD57 Animated Normals")), bAnimatedNormals);

					bool bDetailNormals = false;
					TEST_TRUE(GetTestName(TEXT("Head LOD57 Detail Normals Parameter")), TestUtils::GetStaticSwitchFromMaterial(HeadMaterialLOD57, TEXT("Detail Normals"), bDetailNormals));
					TEST_FALSE(GetTestName(TEXT("Head LOD57 Detail Normals")), bDetailNormals);

					TEST_EQUAL(GetTestName(TEXT("Head LOD57 Color_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD57, TEXT("Color_MAIN")), 2048);
					TEST_EQUAL(GetTestName(TEXT("Head LOD57 Normal_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD57, TEXT("Normal_MAIN")), 8192);
					TEST_EQUAL(GetTestName(TEXT("Head LOD57 Normal_BAKED Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD57, TEXT("Normal_BAKED")), 256);
					TEST_EQUAL(GetTestName(TEXT("Head LOD57 Roughness_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD57, TEXT("Roughness_MAIN")), 4096);
					TEST_EQUAL(GetTestName(TEXT("Head LOD57 Cavity_MAIN Resolution")), TestUtils::GetTextureResolution(HeadMaterialLOD57, TEXT("Cavity_MAIN")), 8192);
				}
			}
		}

		auto TestClothingPostProcessAnimBP = [this, Test, &ExportQuality, &GetTestName](USkeletalMesh* InSkeletalMesh, FStringView InPartName, const TestUtils::FClothingPostProcessAnimBPSettings& PostProcessAnimBPSettings)
		{
			auto GetPostProcessTestName = [InPartName, &GetTestName](const FString& InTest)
			{
				return GetTestName(FString::Format(TEXT("{0} Post Process AnimBP {1}"), { InPartName, InTest }));
			};

			TEST_EQUAL(GetPostProcessTestName(TEXT("LOD Threshold")), InSkeletalMesh->GetPostProcessAnimBPLODThreshold(), PostProcessAnimBPSettings.LODThreshold);

			if (TSubclassOf<UAnimInstance> PostProcessAnimBPClass = InSkeletalMesh->GetPostProcessAnimBlueprint())
			{
				// This is an optional asset so only test if its set
				UAnimBlueprint* PostProcessAnimBP = Cast<UAnimBlueprint>(PostProcessAnimBPClass->ClassGeneratedBy);
				TEST_NOT_NULL(GetPostProcessTestName(TEXT("Valid")), PostProcessAnimBP);

				FString BodyTypeName;
				TEST_TRUE(GetPostProcessTestName(TEXT("Get Body Type Name")), TestUtils::GetBodyTypeNameFromMeshName(InSkeletalMesh->GetName(), BodyTypeName));

				TEST_EQUAL(GetPostProcessTestName(TEXT("Parent Class")), UAnimBlueprint::GetParentAnimBlueprint(PostProcessAnimBP)->GetName(), TEXT("ABP_Clothing_PostProcess"));

				TEST_NOT_NULL(GetPostProcessTestName(TEXT("Target Skeleton ")), PostProcessAnimBP->TargetSkeleton.Get());
				TEST_SAME_PTR(GetPostProcessTestName(TEXT("Target Skeleton is same as Skeleton")), PostProcessAnimBP->TargetSkeleton.Get(), InSkeletalMesh->GetSkeleton());

				UAnimInstance* PostProcessAnimInstance = PostProcessAnimBPClass->GetDefaultObject<UAnimInstance>();
				TEST_NOT_NULL(GetPostProcessTestName(TEXT("Instance")), PostProcessAnimInstance);

				bool bEnableRigidBodySimulation = false;
				TEST_TRUE(GetPostProcessTestName(TEXT("Enable Rigid Body Property")), TestUtils::GetPropertyValue(PostProcessAnimInstance, TestUtils::FClothingPostProcessAnimBPSettings::bEnableRigidBodySimulationPropertyName, bEnableRigidBodySimulation));
				TEST_EQUAL(GetPostProcessTestName(TEXT("Enable Rigid Body")), bEnableRigidBodySimulation, PostProcessAnimBPSettings.bEnableRigidBodySimulation);

				int32 RigidBodyLODThreshold = INDEX_NONE;
				TEST_TRUE(GetPostProcessTestName(TEXT("Rigid Body LOD Threshold Property")), TestUtils::GetPropertyValue(PostProcessAnimInstance, TestUtils::FClothingPostProcessAnimBPSettings::RigidBodyLODThresholdPropertyName, RigidBodyLODThreshold));
				TEST_EQUAL(GetPostProcessTestName(TEXT("Rigid Body LOD Threshold")), RigidBodyLODThreshold, PostProcessAnimBPSettings.RigidBodyLODThreshold);

				bool bEnableControlRig = false;
				TEST_TRUE(GetPostProcessTestName(TEXT("Enable Control Rig Property")), TestUtils::GetPropertyValue(PostProcessAnimInstance, TestUtils::FClothingPostProcessAnimBPSettings::bEnableControlRigPropertyName, bEnableControlRig));
				TEST_EQUAL(GetPostProcessTestName(TEXT("Enable Control Rig")), bEnableControlRig, PostProcessAnimBPSettings.bEnableControlRig);

				int32 ControlRigLODThreshold = INDEX_NONE;
				TEST_TRUE(GetPostProcessTestName(TEXT("Control Rig LOD Threshold Property")), TestUtils::GetPropertyValue(PostProcessAnimInstance, TestUtils::FClothingPostProcessAnimBPSettings::ControlRigLODThresholdPropertyName, ControlRigLODThreshold));
				TEST_EQUAL(GetPostProcessTestName(TEXT("Control Rig LOD Threshold")), ControlRigLODThreshold, PostProcessAnimBPSettings.ControlRigLODThreshold);

				TSubclassOf<UControlRig> ControlRigClass = nullptr;
				TEST_TRUE(GetPostProcessTestName(TEXT("Control Rig Property")), TestUtils::GetPropertyValue(PostProcessAnimInstance, TestUtils::FClothingPostProcessAnimBPSettings::ControlRigClassPropertyName, ControlRigClass));

				UPhysicsAsset* OverridePhysicsAsset = nullptr;
				TEST_TRUE(GetPostProcessTestName(TEXT("Override Physics Asset Property")), TestUtils::GetPropertyValue(PostProcessAnimInstance, TestUtils::FClothingPostProcessAnimBPSettings::OverridePhysicsAssetPropertyName, OverridePhysicsAsset));

				if (ControlRigClass != nullptr)
				{
					// A ControlRig is optional, so only test if there is one
					TEST_TRUE(GetPostProcessTestName(TEXT("Control Rig Class Name Suffix")), ControlRigClass->GetName().EndsWith(TEXT("_CtrlRig_C")));
				}

				if (OverridePhysicsAsset != nullptr)
				{
					// A physics asset is optional, so only test if there is one
					TEST_TRUE(GetPostProcessTestName(TEXT("Override Physics Asset Name Prefix")), OverridePhysicsAsset->GetName().StartsWith(BodyTypeName));
					TEST_TRUE(GetPostProcessTestName(TEXT("Override Physics Asset Name Suffix")), OverridePhysicsAsset->GetName().EndsWith(TEXT("_Physics")));
				}
			}

			return true;
		};

		if (BaseTestName == TEXT("MetaHuman.Torso"))
		{
			USkeletalMeshComponent* TorsoComponent = TestUtils::GetComponentByName<USkeletalMeshComponent>(MetaHumanActor, TEXT("Torso"));
			TEST_NOT_NULL(GetTestName(TEXT("Torso Component")), TorsoComponent);
			TEST_EQUAL(GetTestName(TEXT("Torso Component Only Tick when Rendered")), TorsoComponent->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
			TEST_EQUAL(GetTestName(TEXT("Torso Component is a child of Body")), TorsoComponent->GetAttachParent()->GetName(), TEXT("Body"));

			if (USkeletalMesh* TorsoSkelMesh = TorsoComponent->GetSkeletalMeshAsset())
			{
				// Torso mesh is optional
				TEST_EQUAL(GetTestName(TEXT("Torso Skeletal Mesh Num LODs")), TorsoSkelMesh->GetLODNum(), TestUtils::GetNumLODsForQuality(ExportQuality, TorsoComponent->GetName()));
				TEST_NULL(GetTestName(TEXT("Torso Skeletal Mesh Default Animating Rig")), TorsoSkelMesh->GetDefaultAnimatingRig().Get());

				TEST_TRUE(GetTestName(TEXT("Torso Post Process AnimBP")), TestClothingPostProcessAnimBP(TorsoSkelMesh, TEXTVIEW("Torso"), TestUtils::GetClothingPostProcessAnimBPSettings(TEXTVIEW("Torso"), ExportQuality)));

				if (TorsoSkelMesh->GetPostProcessAnimBlueprint() == nullptr && TorsoComponent->GetAnimClass() == nullptr)
				{
					// TODO: MH The Construction script only set the leader pose component in this condition. Figure out if this is really expected
					TEST_VALID(GetTestName(TEXT("Torso Leader Pose Component is valid")), TorsoComponent->LeaderPoseComponent);
					TEST_SAME_PTR(GetTestName(TEXT("Torso Component follows Body")), TorsoComponent->LeaderPoseComponent.Get(), Cast<USkinnedMeshComponent>(BodyComponent));
				}
				else
				{
					TEST_INVALID(GetTestName(TEXT("Torso Leader Pose Component is not set")), TorsoComponent->LeaderPoseComponent);
				}

				UMaterialInterface* TorsoMaterial = TorsoComponent->GetMaterial(0);
				TEST_NOT_NULL(GetTestName(TEXT("Torso Material")), TorsoMaterial);

				if (TestUtils::IsOptimizedExport(ExportQuality))
				{
					TEST_EQUAL(GetTestName(TEXT("Torso BaseColor")), TestUtils::GetTextureResolution(TorsoMaterial, TEXT("BaseColor")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Torso"), ExportQuality, TEXTVIEW("BaseColor")));
					TEST_EQUAL(GetTestName(TEXT("Torso Normal")), TestUtils::GetTextureResolution(TorsoMaterial, TEXT("Normal")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Torso"), ExportQuality, TEXTVIEW("Normal")));
					TEST_EQUAL(GetTestName(TEXT("Torso Specular")), TestUtils::GetTextureResolution(TorsoMaterial, TEXT("Specular")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Torso"), ExportQuality, TEXTVIEW("Specular")));
				}
				else
				{
					TEST_GREATER_EQUAL(GetTestName(TEXT("Torso AO Resolution")), TestUtils::GetTextureResolution(TorsoMaterial, TEXT("AO")), 2048);
					TEST_GREATER_EQUAL(GetTestName(TEXT("Torso Masks Resolution")), TestUtils::GetTextureResolution(TorsoMaterial, TEXT("Masks")), 1024);
					TEST_GREATER_EQUAL(GetTestName(TEXT("Torso normalmap Resolution")), TestUtils::GetTextureResolution(TorsoMaterial, TEXT("normalmap")), 4096);
				}
			}
		}

		if (BaseTestName == TEXT("MetaHuman.Legs"))
		{
			USkeletalMeshComponent* LegsComponent = TestUtils::GetComponentByName<USkeletalMeshComponent>(MetaHumanActor, TEXT("Legs"));
			TEST_NOT_NULL(GetTestName(TEXT("Legs Component")), LegsComponent);
			TEST_EQUAL(GetTestName(TEXT("Legs Only Tick when Rendered")), LegsComponent->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
			TEST_EQUAL(GetTestName(TEXT("Legs are child of Body")), LegsComponent->GetAttachParent()->GetName(), TEXT("Body"));

			if (USkeletalMesh* LegsSkelMesh = LegsComponent->GetSkeletalMeshAsset())
			{
				// Legs mesh is optional
				TEST_EQUAL(GetTestName(TEXT("Legs Skeletal Mesh Num LODs")), LegsSkelMesh->GetLODNum(), TestUtils::GetNumLODsForQuality(ExportQuality, LegsComponent->GetName()));
				TEST_NULL(GetTestName(TEXT("Legs Skeletal Mesh Default Animating Rig")), LegsSkelMesh->GetDefaultAnimatingRig().Get());

				TEST_TRUE(GetTestName(TEXT("Legs Post Process AnimBP")), TestClothingPostProcessAnimBP(LegsSkelMesh, TEXTVIEW("Legs"), TestUtils::GetClothingPostProcessAnimBPSettings(TEXTVIEW("Legs"), ExportQuality)));

				if (LegsSkelMesh->GetPostProcessAnimBlueprint() == nullptr && LegsComponent->GetAnimClass() == nullptr)
				{
					// TODO: MH The Construction script only set the leader pose component in this condition. Figure out if this is really expected
					TEST_VALID(GetTestName(TEXT("Legs Leader Pose Component is valid")), LegsComponent->LeaderPoseComponent);
					TEST_SAME_PTR(GetTestName(TEXT("Legs Component follows Body")), LegsComponent->LeaderPoseComponent.Get(), Cast<USkinnedMeshComponent>(BodyComponent));
				}
				else
				{
					TEST_INVALID(GetTestName(TEXT("Legs Leader Pose Component is not set")), LegsComponent->LeaderPoseComponent);
				}

				UMaterialInterface* LegsMaterial = LegsComponent->GetMaterial(0);
				TEST_NOT_NULL(GetTestName(TEXT("Legs Material")), LegsMaterial);

				if (TestUtils::IsOptimizedExport(ExportQuality))
				{
					TEST_EQUAL(GetTestName(TEXT("Legs BaseColor")), TestUtils::GetTextureResolution(LegsMaterial, TEXT("BaseColor")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Legs"), ExportQuality, TEXTVIEW("BaseColor")));
					TEST_EQUAL(GetTestName(TEXT("Legs Normal")), TestUtils::GetTextureResolution(LegsMaterial, TEXT("Normal")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Legs"), ExportQuality, TEXTVIEW("Normal")));
					TEST_EQUAL(GetTestName(TEXT("Legs Specular")), TestUtils::GetTextureResolution(LegsMaterial, TEXT("Specular")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Legs"), ExportQuality, TEXTVIEW("Specular")));
				}
				else
				{
					if (LegsMaterial->GetName() == TEXT("M_btm_jeans_nrm"))
					{
						TEST_EQUAL(GetTestName(TEXT("Legs Diffuse Resolution")), TestUtils::GetTextureResolution(LegsMaterial, TEXT("Diffuse")), 4096);
						TEST_EQUAL(GetTestName(TEXT("Legs AO Resolution")), TestUtils::GetTextureResolution(LegsMaterial, TEXT("AO")), 2048);
						TEST_EQUAL(GetTestName(TEXT("Legs Mask Resolution")), TestUtils::GetTextureResolution(LegsMaterial, TEXT("Mask")), 4096);
						TEST_EQUAL(GetTestName(TEXT("Legs Normal Resolution")), TestUtils::GetTextureResolution(LegsMaterial, TEXT("Normals")), 4096);
					}
					else
					{
						TEST_EQUAL(GetTestName(TEXT("Legs AO Resolution")), TestUtils::GetTextureResolution(LegsMaterial, TEXT("AO")), 2048);
						TEST_EQUAL(GetTestName(TEXT("Legs Masks Resolution")), TestUtils::GetTextureResolution(LegsMaterial, TEXT("Masks")), 4096);
						TEST_EQUAL(GetTestName(TEXT("Legs normalmap Resolution")), TestUtils::GetTextureResolution(LegsMaterial, TEXT("normalmap")), 8192);
					}
				}
			}
		}

		if (BaseTestName == TEXT("MetaHuman.Feet"))
		{
			USkeletalMeshComponent* FeetComponent = TestUtils::GetComponentByName<USkeletalMeshComponent>(MetaHumanActor, TEXT("Feet"));
			TEST_NOT_NULL(GetTestName(TEXT("Feet Component is valid")), FeetComponent);
			TEST_EQUAL(GetTestName(TEXT("Feet Only Tick when Rendered")), FeetComponent->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
			TEST_EQUAL(GetTestName(TEXT("Feet is a child of Body")), FeetComponent->GetAttachParent()->GetName(), TEXT("Body"));

			if (USkeletalMesh* FeetSkelMesh = FeetComponent->GetSkeletalMeshAsset())
			{
				// Feet mesh is optional
				TEST_EQUAL(GetTestName(TEXT("Feet Num LODs")), FeetSkelMesh->GetLODNum(), TestUtils::GetNumLODsForQuality(ExportQuality, FeetComponent->GetName()));
				TEST_NULL(GetTestName(TEXT("Feet Skeletal Mesh Default Animating Rig")), FeetSkelMesh->GetDefaultAnimatingRig().Get());

				TEST_TRUE(GetTestName(TEXT("Feet Post Process AnimBP")), TestClothingPostProcessAnimBP(FeetSkelMesh, TEXTVIEW("Feet"), TestUtils::GetClothingPostProcessAnimBPSettings(TEXTVIEW("Feet"), ExportQuality)));

				if (FeetSkelMesh->GetPostProcessAnimBlueprint() == nullptr && FeetComponent->GetAnimClass() == nullptr)
				{
					// TODO: MH The Construction script only set the leader pose component in this condition. Figure out if this is really expected
					TEST_VALID(GetTestName(TEXT("Feet Leader Pose Component is valid")), FeetComponent->LeaderPoseComponent);
					TEST_SAME_PTR(GetTestName(TEXT("Feet Component follows Body")), FeetComponent->LeaderPoseComponent.Get(), Cast<USkinnedMeshComponent>(BodyComponent));
				}
				else
				{
					TEST_INVALID(GetTestName(TEXT("Legs Leader Pose Component is not set")), FeetComponent->LeaderPoseComponent);
				}

				UMaterialInterface* FeetMaterial = FeetComponent->GetMaterial(0);
				TEST_NOT_NULL(GetTestName(TEXT("Feet Material")), FeetMaterial);

				if (TestUtils::IsOptimizedExport(ExportQuality))
				{
					TEST_EQUAL(GetTestName(TEXT("Feet BaseColor Resolution")), TestUtils::GetTextureResolution(FeetMaterial, TEXT("BaseColor")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Feet"), ExportQuality, TEXTVIEW("BaseColor")));
					TEST_EQUAL(GetTestName(TEXT("Feet Normal Resolution")), TestUtils::GetTextureResolution(FeetMaterial, TEXT("Normal")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Feet"), ExportQuality, TEXTVIEW("Normal")));
					TEST_EQUAL(GetTestName(TEXT("Feet Specular Resolution")), TestUtils::GetTextureResolution(FeetMaterial, TEXT("Specular")), TestUtils::GetTextureResolutionForQuality(TEXTVIEW("Feet"), ExportQuality, TEXTVIEW("Specular")));
				}
				else
				{
					TEST_EQUAL(GetTestName(TEXT("Feet AO Resolution")), TestUtils::GetTextureResolution(FeetMaterial, TEXT("AO")), 2048);
					TEST_GREATER_EQUAL(GetTestName(TEXT("Feet Masks Resolution")), TestUtils::GetTextureResolution(FeetMaterial, TEXT("Masks")), 2048);
					TEST_GREATER_EQUAL(GetTestName(TEXT("Feet normalmap Resolution")), TestUtils::GetTextureResolution(FeetMaterial, TEXT("normalmap")), 2048);
				}
			}
		}

		if (BaseTestName == TEXT("MetaHumans.Grooms.Hair"))
		{
			UGroomComponent* HairComponent = TestUtils::GetComponentByName<UGroomComponent>(MetaHumanActor, TEXT("Hair"));
			TEST_NOT_NULL(GetTestName(TEXT("Hair Component")), HairComponent);
			TEST_EQUAL(GetTestName(TEXT("Hair Component is child of Face")), HairComponent->GetAttachParent()->GetName(), TEXT("Face"));
			TEST_EQUAL(GetTestName(TEXT("Hair Component Tick Group")), HairComponent->PrimaryComponentTick.TickGroup, ETickingGroup::TG_PrePhysics);
			TEST_EQUAL(GetTestName(TEXT("Hair Component Local Bone")), HairComponent->SimulationSettings.SimulationSetup.LocalBone, TEXT("head"));
			TEST_EQUAL(GetTestName(TEXT("Hair Component Attachment Name")), HairComponent->AttachmentName, TEXT("FACIAL_C_FacialRoot"));

			if (HairComponent->GroomAsset || HairComponent->BindingAsset)
			{
				TEST_NOT_NULL(GetTestName(TEXT("Hair GroomAsset")), HairComponent->GroomAsset.Get());
				TEST_NOT_NULL(GetTestName(TEXT("Hair BindingAsset")), HairComponent->BindingAsset.Get());
				TEST_NOT_NULL(GetTestName(TEXT("Hair Groom Binding")), HairComponent->BindingAsset.Get());
				TEST_TRUE(GetTestName(TEXT("Hair Groom Binding name")), HairComponent->BindingAsset->GetName().StartsWith(HairComponent->GroomAsset->GetName()));
			}
		}

		if (BaseTestName == TEXT("MetaHuman.Grooms.Beard"))
		{
			UGroomComponent* BeardComponent = TestUtils::GetComponentByName<UGroomComponent>(MetaHumanActor, TEXT("Beard"));
			TEST_NOT_NULL(GetTestName(TEXT("Beard Component")), BeardComponent);
			TEST_EQUAL(GetTestName(TEXT("Beard Component is child of Face")), BeardComponent->GetAttachParent()->GetName(), TEXT("Face"));
			TEST_EQUAL(GetTestName(TEXT("Beard Component Tick Group")), BeardComponent->PrimaryComponentTick.TickGroup, ETickingGroup::TG_PrePhysics);
			TEST_EQUAL(GetTestName(TEXT("Beard Component Local Bone")), BeardComponent->SimulationSettings.SimulationSetup.LocalBone, TEXT("head"));
			TEST_EQUAL(GetTestName(TEXT("Beard Component Attachment Name")), BeardComponent->AttachmentName, TEXT("Facial_C_JAW"));

			if (BeardComponent->GroomAsset || BeardComponent->BindingAsset)
			{
				TEST_NOT_NULL(GetTestName(TEXT("Beard GroomAsset is valid")), BeardComponent->GroomAsset.Get());
				TEST_NOT_NULL(GetTestName(TEXT("Beard BindingAsset is valid")), BeardComponent->BindingAsset.Get());
				TEST_TRUE("Beard Groom Binding name", BeardComponent->BindingAsset->GetName().StartsWith(BeardComponent->GroomAsset->GetName()));
			}
		}

		if (BaseTestName == TEXT("MetaHumans.Grooms.Eyebrows"))
		{
			UGroomComponent* EyebrowsComponent = TestUtils::GetComponentByName<UGroomComponent>(MetaHumanActor, TEXT("Eyebrows"));
			TEST_NOT_NULL(GetTestName(TEXT("Eyebrows Component")), EyebrowsComponent);
			TEST_EQUAL(GetTestName(TEXT("Eyebrows Component is child of Face")), EyebrowsComponent->GetAttachParent()->GetName(), TEXT("Face"));
			TEST_EQUAL(GetTestName(TEXT("Eyebrows Component Tick Group")), EyebrowsComponent->PrimaryComponentTick.TickGroup, ETickingGroup::TG_PrePhysics);
			TEST_EQUAL(GetTestName(TEXT("Eyebrows Component Local Bone")), EyebrowsComponent->SimulationSettings.SimulationSetup.LocalBone, TEXT("root"));
			TEST_EQUAL(GetTestName(TEXT("Eyebrows Component Attachment Name")), EyebrowsComponent->AttachmentName, TEXT("FACIAL_C_FacialRoot"));

			if (EyebrowsComponent->GroomAsset || EyebrowsComponent->BindingAsset)
			{
				TEST_NOT_NULL(GetTestName(TEXT("Eyebrows GroomAsset")), EyebrowsComponent->GroomAsset.Get());
				TEST_NOT_NULL(GetTestName(TEXT("Eyebrows BindingAsset is valid")), EyebrowsComponent->BindingAsset.Get());
				TEST_TRUE(GetTestName(TEXT("Eyebrows Groom Binding name")), EyebrowsComponent->BindingAsset->GetName().StartsWith(EyebrowsComponent->GroomAsset->GetName()));
			}
		}

		if (BaseTestName == TEXT("MetaHuman.Grooms.Eyelahes"))
		{
			UGroomComponent* EyelashesComponent = TestUtils::GetComponentByName<UGroomComponent>(MetaHumanActor, TEXT("Eyelashes"));
			TEST_NOT_NULL(GetTestName(TEXT("Eyelashes Component")), EyelashesComponent);
			TEST_EQUAL(GetTestName(TEXT("Eyelashes Component is child of Face")), EyelashesComponent->GetAttachParent()->GetName(), TEXT("Face"));
			TEST_EQUAL(GetTestName(TEXT("Eyelashes Component Tick Group")), EyelashesComponent->PrimaryComponentTick.TickGroup, ETickingGroup::TG_PrePhysics);
			TEST_EQUAL(GetTestName(TEXT("Eyelashes Component Local Bone")), EyelashesComponent->SimulationSettings.SimulationSetup.LocalBone, TEXT("root"));
			TEST_EQUAL(GetTestName(TEXT("Eyelashes Component Attachment Name")), EyelashesComponent->AttachmentName, TEXT("FACIAL_C_FacialRoot"));

			if (EyelashesComponent->GroomAsset || EyelashesComponent->BindingAsset)
			{
				TEST_NOT_NULL(GetTestName(TEXT("Eyelashes GroomAsset")), EyelashesComponent->GroomAsset.Get());
				TEST_NOT_NULL(GetTestName(TEXT("Eyelashes BindingAsset")), EyelashesComponent->BindingAsset.Get());
				TEST_TRUE(GetTestName(TEXT("Eyelashes Groom Binding name")), EyelashesComponent->BindingAsset->GetName().StartsWith(EyelashesComponent->GroomAsset->GetName()));
			}
		}

		if (BaseTestName == TEXT("MetaHuman.Grooms.Mustache"))
		{
			UGroomComponent* MustacheComponent = TestUtils::GetComponentByName<UGroomComponent>(MetaHumanActor, TEXT("Mustache"));
			TEST_NOT_NULL(GetTestName(TEXT("Mustache Component")), MustacheComponent);
			TEST_EQUAL(GetTestName(TEXT("Mustache Component is child of Face")), MustacheComponent->GetAttachParent()->GetName(), TEXT("Face"));
			TEST_EQUAL(GetTestName(TEXT("Mustache Component Tick Group")), MustacheComponent->PrimaryComponentTick.TickGroup, ETickingGroup::TG_PrePhysics);
			TEST_EQUAL(GetTestName(TEXT("Mustache Component Local Bone")), MustacheComponent->SimulationSettings.SimulationSetup.LocalBone, TEXT("head"));
			TEST_EQUAL(GetTestName(TEXT("Mustache Component Attachment Name")), MustacheComponent->AttachmentName, TEXT("FACIAL_C_LipUpper"));

			if (MustacheComponent->GroomAsset || MustacheComponent->BindingAsset)
			{
				TEST_NOT_NULL(GetTestName(TEXT("Mustache GroomAsset")), MustacheComponent->GroomAsset.Get());
				TEST_NOT_NULL(GetTestName(TEXT("Mustache BindingAsset")), MustacheComponent->BindingAsset.Get());
				TEST_TRUE(GetTestName(TEXT("Mustache Groom Binding Name")), MustacheComponent->BindingAsset->GetName().StartsWith(MustacheComponent->GroomAsset->GetName()));
			}
		}

		if (BaseTestName == TEXT("MetaHuman.Grooms.Fuzz"))
		{
			UGroomComponent* FuzzComponent = TestUtils::GetComponentByName<UGroomComponent>(MetaHumanActor, TEXT("Fuzz"));
			TEST_NOT_NULL(GetTestName(TEXT("Fuzz Component is valid")), FuzzComponent);
			TEST_EQUAL(GetTestName(TEXT("Fuzz Component is child of Face")), FuzzComponent->GetAttachParent()->GetName(), TEXT("Face"));
			TEST_EQUAL(GetTestName(TEXT("Fuzz Component Tick Group")), FuzzComponent->PrimaryComponentTick.TickGroup, ETickingGroup::TG_PrePhysics);
			TEST_EQUAL(GetTestName(TEXT("Fuzz Component Local Bone")), FuzzComponent->SimulationSettings.SimulationSetup.LocalBone, TEXT("root"));
			TEST_EQUAL(GetTestName(TEXT("Fuzz Component Attachment Name")), FuzzComponent->AttachmentName, TEXT("FACIAL_C_FacialRoot"));

			if (FuzzComponent->GroomAsset || FuzzComponent->BindingAsset)
			{
				TEST_FALSE(GetTestName(TEXT("Fuzz should not be in Optimized MetaHuman")), TestUtils::IsOptimizedExport(ExportQuality));
				TEST_NOT_NULL(GetTestName(TEXT("Fuzz GroomAsset")), FuzzComponent->GroomAsset.Get());
				TEST_NOT_NULL(GetTestName(TEXT("Fuzz BindingAsset")), FuzzComponent->BindingAsset.Get());
				TEST_TRUE(GetTestName(TEXT("Fuzz Groom Binding Name")), FuzzComponent->BindingAsset->GetName().StartsWith(FuzzComponent->GroomAsset->GetName()));
			}
		}

		if (BaseTestName == TEXT("MetaHuman.LODSync"))
		{
			ULODSyncComponent* LODSyncComponent = MetaHumanActor->FindComponentByClass<ULODSyncComponent>();
			TEST_NOT_NULL(GetTestName(TEXT("LOD Sync Component is valid")), LODSyncComponent);

			const TestUtils::FLODSyncSettings& LODSyncSettings = TestUtils::GetLODSyncSettings(ExportQuality);

			// Should be the same as the number of Face LODs
			TEST_EQUAL(GetTestName(TEXT("LOD Sync Num LODs")), LODSyncComponent->NumLODs, TestUtils::GetNumLODsForQuality(ExportQuality, TEXT("Face")));
			TEST_EQUAL(GetTestName(TEXT("LOD Sync Forced LOD")), LODSyncComponent->ForcedLOD, INDEX_NONE);
			TEST_EQUAL(GetTestName(TEXT("LOD Sync Min LOD")), LODSyncComponent->MinLOD, 0);
			TEST_EQUAL(GetTestName(TEXT("LOD Sync Num Component to Sync")), LODSyncComponent->ComponentsToSync.Num(), LODSyncSettings.NumComponentsToSync);
			TEST_EQUAL(GetTestName(TEXT("LOD Sync Num Custom Mapping")), LODSyncComponent->CustomLODMapping.Num(), LODSyncSettings.NumCustomLODMapping);

			for (const FComponentSync& CompSync : LODSyncComponent->ComponentsToSync)
			{
				UActorComponent* Component = TestUtils::GetComponentByName<UActorComponent>(MetaHumanActor, CompSync.Name);
				TEST_NOT_NULL(GetTestName(TEXT("LOD Sync Component to sync is valid")), Component);

				ESyncOption SyncOption = ESyncOption::Passive;
				if (Component->GetName() == TEXT("Face") || Component->GetName() == TEXT("Body"))
				{
					SyncOption = ESyncOption::Drive;
				}

				TEST_EQUAL(GetTestName(TEXT("LOD Sync Option")), CompSync.SyncOption, SyncOption);
			}
		}
	}

	return true;
}

namespace TestUtils
{
	void AddValidateMetaHumanLatentCommands(const FMetaHumanAssetImportDescription& InImportDescription)
	{
		for (const FString& TestName : GenerateTestNames(InImportDescription.CharacterName))
		{
			ADD_LATENT_AUTOMATION_COMMAND(FValidateMetaHumanCommand(TestName, InImportDescription));
		}
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMetaHumanProjectUtilsExporterTest, "MetaHuman.Exporter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);
void FMetaHumanProjectUtilsExporterTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	const TArray<FInstalledMetaHuman> InstalledMetaHumans = FMetaHumanProjectUtilities::GetInstalledMetaHumans();
	for (const FInstalledMetaHuman& InstalledMetaHuman : InstalledMetaHumans)
	{
		OutTestCommands += TestUtils::GenerateTestNames(InstalledMetaHuman.GetName());
	}

	OutBeautifiedNames = OutTestCommands;
}

bool FMetaHumanProjectUtilsExporterTest::RunTest(const FString& InParams)
{
	// All testing is done in FValidateMetaHumanCommand
	ADD_LATENT_AUTOMATION_COMMAND(FValidateMetaHumanCommand(InParams, FMetaHumanAssetImportDescription{}));
	return true;
}
}

#endif // WITH_DEV_AUTOMATION_TESTS