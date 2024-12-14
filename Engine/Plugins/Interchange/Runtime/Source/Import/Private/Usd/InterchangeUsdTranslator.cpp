// Copyright Epic Games, Inc. All Rights Reserved.

#include "Usd/InterchangeUsdTranslator.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDLightConversion.h"
#include "USDLog.h"
#include "USDObjectUtils.h"
#include "USDPrimConversion.h"
#include "USDShadeConversion.h"
#include "USDSkeletalDataConversion.h"
#include "USDStageOptions.h"
#include "USDTypesConversion.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdGeomXformable.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdRelationship.h"
#include "UsdWrappers/UsdSkelAnimQuery.h"
#include "UsdWrappers/UsdSkelBinding.h"
#include "UsdWrappers/UsdSkelBlendShape.h"
#include "UsdWrappers/UsdSkelBlendShapeQuery.h"
#include "UsdWrappers/UsdSkelCache.h"
#include "UsdWrappers/UsdSkelInbetweenShape.h"
#include "UsdWrappers/UsdSkelSkeletonQuery.h"
#include "UsdWrappers/UsdSkelSkinningQuery.h"
#include "UsdWrappers/UsdStage.h"
#include "UsdWrappers/UsdTyped.h"

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "HAL/IConsoleManager.h"
#include "InterchangeCameraNode.h"
#include "InterchangeLightNode.h"
#include "InterchangeManager.h"
#include "InterchangeMaterialInstanceNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangeSceneNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTranslatorHelper.h"
#include "Mesh/InterchangeMeshPayload.h"
#include "MovieSceneSection.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "StaticMeshAttributes.h"
#include "UDIMUtilities.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdLux/tokens.h"
#include "pxr/usd/usdShade/tokens.h"
#include "USDIncludesEnd.h"
#endif	  // USE_USD_SDK

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeUsdTranslator)

#define LOCTEXT_NAMESPACE "InterchangeUSDTranslator"

static bool GInterchangeEnableUSDImport = false;
static FAutoConsoleVariableRef CVarInterchangeEnableUSDImport(
	TEXT("Interchange.FeatureFlags.Import.USD"),
	GInterchangeEnableUSDImport,
	TEXT("Whether USD support is enabled.")
);

static bool GInterchangeEnableUSDLevelImport = false;
// Import into level via USD Interchange is disabled for 5.5 as it's still a work in progress
// static FAutoConsoleVariableRef CVarInterchangeEnableUSDLevelImport(
// 	TEXT("Interchange.FeatureFlags.Import.USD.ToLevel"),
// 	GInterchangeEnableUSDLevelImport,
// 	TEXT("Whether support for USD level import is enabled.")
// );

namespace UE::InterchangeUsdTranslator::Private
{
	const static FString AnimationPrefix = TEXT("\\Animation\\");
	const static FString AnimationTrackPrefix = TEXT("\\AnimationTrack\\");
	const static FString CameraPrefix = TEXT("\\Camera\\");
	const static FString LightPrefix = TEXT("\\Light\\");
	const static FString MaterialPrefix = TEXT("\\Material\\");
	const static FString MeshPrefix = TEXT("\\Mesh\\");
	const static FString MorphTargetPrefix = TEXT("\\MorphTarget\\");
	const static FString BonePrefix = TEXT("\\Bone\\");

	// Information intended to be passed down from parent to children (by value) as we traverse the stage
	struct FTraversalInfo
	{
		UInterchangeBaseNode* ParentNode = nullptr;

		TSharedPtr<UE::FUsdSkelCache> FurthestSkelCache;
		UE::FUsdPrim ClosestParentSkelRoot;

		UE::FUsdSkelSkeletonQuery ActiveSkelQuery;
		TSharedPtr<TArray<FString>> SkelJointNames;	   // Needed for skel mesh payloads
	};

	// clang-format off
	const static TMap<FName, EInterchangePropertyTracks> PropertyNameToTrackType = {
		// Common properties
		{UnrealIdentifiers::HiddenInGamePropertyName, 			EInterchangePropertyTracks::Visibility}, // Binding visibility to the actor works better for cameras

		// Camera properties
		{UnrealIdentifiers::CurrentFocalLengthPropertyName, 	EInterchangePropertyTracks::CameraCurrentFocalLength},
		{UnrealIdentifiers::ManualFocusDistancePropertyName, 	EInterchangePropertyTracks::CameraFocusSettingsManualFocusDistance},
		{UnrealIdentifiers::CurrentAperturePropertyName, 		EInterchangePropertyTracks::CameraCurrentAperture},
		{UnrealIdentifiers::SensorWidthPropertyName, 			EInterchangePropertyTracks::CameraFilmbackSensorWidth},
		{UnrealIdentifiers::SensorHeightPropertyName, 			EInterchangePropertyTracks::CameraFilmbackSensorHeight},

		// Light properties
		{UnrealIdentifiers::LightColorPropertyName, 			EInterchangePropertyTracks::LightColor},
		{UnrealIdentifiers::TemperaturePropertyName, 			EInterchangePropertyTracks::LightTemperature},
		{UnrealIdentifiers::UseTemperaturePropertyName, 		EInterchangePropertyTracks::LightUseTemperature},
		{UnrealIdentifiers::SourceHeightPropertyName, 			EInterchangePropertyTracks::LightSourceHeight},
		{UnrealIdentifiers::SourceWidthPropertyName, 			EInterchangePropertyTracks::LightSourceWidth},
		{UnrealIdentifiers::SourceRadiusPropertyName, 			EInterchangePropertyTracks::LightSourceRadius},
		{UnrealIdentifiers::OuterConeAnglePropertyName, 		EInterchangePropertyTracks::LightOuterConeAngle},
		{UnrealIdentifiers::InnerConeAnglePropertyName, 		EInterchangePropertyTracks::LightInnerConeAngle},
		{UnrealIdentifiers::LightSourceAnglePropertyName, 		EInterchangePropertyTracks::LightSourceAngle},
		{UnrealIdentifiers::IntensityPropertyName, 				EInterchangePropertyTracks::LightIntensity},
	};
	// clang-format on

	// Small container that we can use Pimpl with so we don't have to include too many USD includes on the header file.
	//
	// It also skirts around a small complication where UInterchangeUSDTranslator::Translate is const, and yet we must
	// keep and modify some members (like UsdStage) for when the payload functions get called later... The other translators
	// use mutable or const casts, but with the Impl we don't need to!
	class UInterchangeUSDTranslatorImpl
	{
	public:

		/** Add a material instance to the node container, otherwise it will add a material if it comes from a Translator (for example coming from
		 * MaterialX which cannot handle material instances) */
		void AddMaterialNode(
			const UE::FUsdPrim& Prim,
			UInterchangeUsdTranslatorSettings* TranslatorSettings,
			UInterchangeBaseNodeContainer& NodeContainer
		);

		void AddMeshNode(const UE::FUsdPrim& Prim, UInterchangeBaseNodeContainer& NodeContainer, const FTraversalInfo& Info);

	public:
		// We have to keep a stage reference so that we can parse the payloads after Translate() complete.
		// ReleaseSource() clears this member, once translation is complete.
		UE::FUsdStage UsdStage;

#if USE_USD_SDK
		// On UInterchangeUSDTranslator::Translate we set this up based on our TranslatorSettings, and then
		// we can reuse it (otherwise we have to keep converting the FNames into Tokens all the time)
		UsdToUnreal::FUsdMeshConversionOptions CachedMeshConversionOptions;
#endif

		// When traversing we'll generate FTraversalInfo objects. If we need to (e.g. for skinned meshes),
		// we'll store the info for that translated node here, so we don't have to recompute it when returning
		// the payload data.
		// Note: We only do this when needed: This shouldn't have data for every prim in the stage.
		TMap<FString, FTraversalInfo> NodeUidToCachedTraversalInfo;
		mutable FRWLock CachedTraversalInfoLock;

		// This node eventually becomes a LevelSequence, and all track nodes are connected to it.
		// For now we only generate a single LevelSequence per stage though, so we'll keep track of this
		// here for easy access when parsing the tracks
		UInterchangeAnimationTrackSetNode* CurrentTrackSet = nullptr;

		// Array of translators that we call in the GetTexturePayload, the key has no real meaning, it's just here to avoid having duplicates and
		// calling several times the Translate function
		TMap<FString, UInterchangeTranslatorBase*> Translators;

	private:

		struct FMaterialSlotMesh
		{
			FString MaterialSlotName;
			UInterchangeMeshNode* MeshNode;
		};
		TMap<FString, FString> MaterialUidToActualNodeUid;
		TMap<FString, TArray<FMaterialSlotMesh>> PrimPathToSlotMeshNodes;
	};

#if USE_USD_SDK
	FString HashAnimPayloadQuery(const Interchange::FAnimationPayloadQuery& Query)
	{
		FSHAHash Hash;
		FSHA1 SHA1;

		// TODO: Is there a StringView alternative?
		FString SkeletonPrimPath;
		FString JointIndexStr;
		bool bSplit = Query.PayloadKey.UniqueId.Split(TEXT("\\"), &SkeletonPrimPath, &JointIndexStr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (!bSplit)
		{
			return {};
		}

		SHA1.UpdateWithString(*SkeletonPrimPath, SkeletonPrimPath.Len());

		SHA1.Update(reinterpret_cast<const uint8*>(&Query.TimeDescription.BakeFrequency), sizeof(Query.TimeDescription.BakeFrequency));
		SHA1.Update(reinterpret_cast<const uint8*>(&Query.TimeDescription.RangeStartSecond), sizeof(Query.TimeDescription.RangeStartSecond));
		SHA1.Update(reinterpret_cast<const uint8*>(&Query.TimeDescription.RangeStopSecond), sizeof(Query.TimeDescription.RangeStopSecond));

		SHA1.Final();
		SHA1.GetHash(&Hash.Hash[0]);
		return Hash.ToString();
	}

	FString GetMorphTargetMeshNodeUid(const FString& MeshPrimPath, int32 MeshBlendShapeIndex, const FString& InbetweenName = FString{})
	{
		return FString::Printf(TEXT("%s%s\\%d\\%s"), *MorphTargetPrefix, *MeshPrimPath, MeshBlendShapeIndex, *InbetweenName);
	}

	FString GetMorphTargetMeshPayloadKey(const FString& MeshPrimPath, int32 MeshBlendShapeIndex, const FString& InbetweenName = FString{})
	{
		return FString::Printf(TEXT("%s\\%d\\%s"), *MeshPrimPath, MeshBlendShapeIndex, *InbetweenName);
	}

	FString GetMorphTargetCurvePayloadKey(const FString& SkeletonPrimPath, int32 SkelAnimChannelIndex, const FString& BlendShapePath)
	{
		return FString::Printf(TEXT("%s\\%d\\%s"), *SkeletonPrimPath, SkelAnimChannelIndex, *BlendShapePath);
	}

	FString EncodeTexturePayloadKey(const UsdToUnreal::FTextureParameterValue& Value)
	{
		// Encode the compression settings onto the payload key as we need to move that into the
		// payload data within UInterchangeUSDTranslator::GetTexturePayloadData.
		//
		// This should be a temporary thing, and in the future we'll be able to store compression
		// settings directly on the texture translated node
		return Value.TextureFilePath + TEXT("\\") + LexToString((int32)Value.Group);
	}

	bool DecodeTexturePayloadKey(const FString& PayloadKey, FString& OutTextureFilePath, TextureGroup& OutTextureGroup)
	{
		// Use split from end here so that we ignore any backslashes within the file path itself
		FString FilePath;
		FString TextureGroupStr;
		bool bSplit = PayloadKey.Split(TEXT("\\"), &FilePath, &TextureGroupStr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (!bSplit)
		{
			return false;
		}

		OutTextureFilePath = FilePath;

		int32 TempInt;
		if (LexTryParseString<int32>(TempInt, *TextureGroupStr))
		{
			OutTextureGroup = (TextureGroup)TempInt;
		}

		return true;
	}

	void FixSkeletalMeshDescriptionColors(FMeshDescription& MeshDescription)
	{
		// FSkeletalMeshImportData::GetMeshDescription() will reinterpret our Wedge FColors as linear, and put those
		// sRGB values disguised as linear into the mesh description. This also seems to disagree with the patch on
		// cl 32791826, so here we have to fix that up and get our mesh description colors to be actually linear...
		//
		// This will hopefully go away once we have our own skinned mesh to FMeshDescription conversion function.
		//
		// Note: Weirdly enough skeletal meshes seem to put linear colors on VertexColor output, while static meshes
		// put sRGB colors? Maybe this is why the comment above the change on 32791826 mentions to remove the ToFColor on
		// StaticMeshBuilder? This is overall very confusing
		FStaticMeshAttributes Attributes(MeshDescription);
		TVertexInstanceAttributesRef<FVector4f> VertexColor = Attributes.GetVertexInstanceColors();
		for (FVertexInstanceID VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
		{
			const FColor ActualSRGB = FLinearColor(VertexColor[VertexInstanceID]).ToFColor(false);
			VertexColor[VertexInstanceID] = FLinearColor{ActualSRGB};
		}
	}

	void FixMaterialSlotNames(FMeshDescription& MeshDescription, const TArray<UsdUtils::FUsdPrimMaterialSlot>& MeshAssingmentSlots)
	{
		// Fixup material slot names to match the material that is assigned. For Interchange it is better to have the material
		// slot names match what is assigned into them, as it will use those names to "merge identical slots" depending on the
		// import options.
		//
		// Note: These names must also match what is set via MeshNode->SetSlotMaterialDependencyUid(SlotName, MaterialUid)
		FStaticMeshAttributes StaticMeshAttributes(MeshDescription);
		for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < StaticMeshAttributes.GetPolygonGroupMaterialSlotNames().GetNumElements();
			 ++MaterialSlotIndex)
		{
			int32 MaterialIndex = 0;
			LexFromString(MaterialIndex, *StaticMeshAttributes.GetPolygonGroupMaterialSlotNames()[MaterialSlotIndex].ToString());

			if (MeshAssingmentSlots.IsValidIndex(MaterialIndex))
			{
				const FString Source = MeshAssingmentSlots[MaterialIndex].MaterialSource;
				StaticMeshAttributes.GetPolygonGroupMaterialSlotNames()[MaterialSlotIndex] = *Source;
			}
		}
	}

	void UpdateTraversalInfo(FTraversalInfo& Info, const UE::FUsdPrim& CurrentPrim)
	{
		if (CurrentPrim.IsA(TEXT("SkelRoot")))
		{
			if (!Info.ClosestParentSkelRoot)
			{
				// The root-most skel cache should handle any nested UsdSkel prims as well
				Info.FurthestSkelCache = MakeShared<UE::FUsdSkelCache>();

				const bool bTraverseInstanceProxies = true;
				Info.FurthestSkelCache->Populate(CurrentPrim, bTraverseInstanceProxies);
			}

			Info.ClosestParentSkelRoot = CurrentPrim;
		}

		if (Info.ClosestParentSkelRoot && CurrentPrim.HasAPI(TEXT("SkelBindingAPI")))
		{
			UE::FUsdStage Stage = CurrentPrim.GetStage();

			if (UE::FUsdRelationship SkelRel = CurrentPrim.GetRelationship(TEXT("skel:skeleton")))
			{
				TArray<UE::FSdfPath> Targets;
				if (SkelRel.GetTargets(Targets) && Targets.Num() > 0)
				{
					UE::FUsdPrim TargetSkeleton = Stage.GetPrimAtPath(Targets[0]);
					if (TargetSkeleton && TargetSkeleton.IsA(TEXT("Skeleton")))
					{
						Info.ActiveSkelQuery = Info.FurthestSkelCache->GetSkelQuery(TargetSkeleton);
					}
				}
			}
		}
	}

	bool ReadBools(
		const UE::FUsdStage& UsdStage,
		const TArray<double>& UsdTimeSamples,
		const TFunction<bool(double)>& ReaderFunc,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		OutPayloadData.StepCurves.SetNum(1);
		FInterchangeStepCurve& Curve = OutPayloadData.StepCurves[0];
		TArray<float>& KeyTimes = Curve.KeyTimes;
		TArray<bool>& BooleanKeyValues = Curve.BooleanKeyValues.Emplace();

		KeyTimes.Reserve(UsdTimeSamples.Num());
		BooleanKeyValues.Reserve(UsdTimeSamples.Num());

		const FFrameRate StageFrameRate{static_cast<uint32>(UsdStage.GetTimeCodesPerSecond()), 1};

		double LastTimeSample = TNumericLimits<double>::Lowest();
		for (const double UsdTimeSample : UsdTimeSamples)
		{
			// We never want to evaluate the same time twice
			if (FMath::IsNearlyEqual(UsdTimeSample, LastTimeSample))
			{
				continue;
			}
			LastTimeSample = UsdTimeSample;

			int32 FrameNumber = FMath::FloorToInt(UsdTimeSample);
			float SubFrameNumber = UsdTimeSample - FrameNumber;

			FFrameTime FrameTime{FrameNumber, SubFrameNumber};
			double FrameTimeSeconds = static_cast<float>(StageFrameRate.AsSeconds(FrameTime));

			bool UEValue = ReaderFunc(UsdTimeSample);

			KeyTimes.Add(FrameTimeSeconds);
			BooleanKeyValues.Add(UEValue);
		}

		return true;
	}

	bool ReadFloats(
		const UE::FUsdStage& UsdStage,
		const TArray<double>& UsdTimeSamples,
		const TFunction<float(double)>& ReaderFunc,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		OutPayloadData.Curves.SetNum(1);
		FRichCurve& Curve = OutPayloadData.Curves[0];

		const FFrameRate StageFrameRate{static_cast<uint32>(UsdStage.GetTimeCodesPerSecond()), 1};
		const ERichCurveInterpMode InterpMode = (UsdStage.GetInterpolationType() == EUsdInterpolationType::Linear)
													? ERichCurveInterpMode::RCIM_Linear
													: ERichCurveInterpMode::RCIM_Constant;

		double LastTimeSample = TNumericLimits<double>::Lowest();
		for (const double UsdTimeSample : UsdTimeSamples)
		{
			// We never want to evaluate the same time twice
			if (FMath::IsNearlyEqual(UsdTimeSample, LastTimeSample))
			{
				continue;
			}
			LastTimeSample = UsdTimeSample;

			int32 FrameNumber = FMath::FloorToInt(UsdTimeSample);
			float SubFrameNumber = UsdTimeSample - FrameNumber;

			FFrameTime FrameTime{FrameNumber, SubFrameNumber};
			double FrameTimeSeconds = static_cast<float>(StageFrameRate.AsSeconds(FrameTime));

			float UEValue = ReaderFunc(UsdTimeSample);

			FKeyHandle Handle = Curve.AddKey(FrameTimeSeconds, UEValue);
			Curve.SetKeyInterpMode(Handle, InterpMode);
		}

		return true;
	}

	bool ReadColors(
		const UE::FUsdStage& UsdStage,
		const TArray<double>& UsdTimeSamples,
		const TFunction<FLinearColor(double)>& ReaderFunc,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		OutPayloadData.Curves.SetNum(4);
		FRichCurve& RCurve = OutPayloadData.Curves[0];
		FRichCurve& GCurve = OutPayloadData.Curves[1];
		FRichCurve& BCurve = OutPayloadData.Curves[2];
		FRichCurve& ACurve = OutPayloadData.Curves[3];

		const FFrameRate StageFrameRate{static_cast<uint32>(UsdStage.GetTimeCodesPerSecond()), 1};
		const ERichCurveInterpMode InterpMode = (UsdStage.GetInterpolationType() == EUsdInterpolationType::Linear)
													? ERichCurveInterpMode::RCIM_Linear
													: ERichCurveInterpMode::RCIM_Constant;

		double LastTimeSample = TNumericLimits<double>::Lowest();
		for (const double UsdTimeSample : UsdTimeSamples)
		{
			// We never want to evaluate the same time twice
			if (FMath::IsNearlyEqual(UsdTimeSample, LastTimeSample))
			{
				continue;
			}
			LastTimeSample = UsdTimeSample;

			int32 FrameNumber = FMath::FloorToInt(UsdTimeSample);
			float SubFrameNumber = UsdTimeSample - FrameNumber;

			FFrameTime FrameTime{FrameNumber, SubFrameNumber};
			double FrameTimeSeconds = static_cast<float>(StageFrameRate.AsSeconds(FrameTime));

			FLinearColor UEValue = ReaderFunc(UsdTimeSample);

			FKeyHandle RHandle = RCurve.AddKey(FrameTimeSeconds, UEValue.R);
			FKeyHandle GHandle = GCurve.AddKey(FrameTimeSeconds, UEValue.G);
			FKeyHandle BHandle = BCurve.AddKey(FrameTimeSeconds, UEValue.B);
			FKeyHandle AHandle = ACurve.AddKey(FrameTimeSeconds, UEValue.A);

			RCurve.SetKeyInterpMode(RHandle, InterpMode);
			GCurve.SetKeyInterpMode(GHandle, InterpMode);
			BCurve.SetKeyInterpMode(BHandle, InterpMode);
			ACurve.SetKeyInterpMode(AHandle, InterpMode);
		}

		return true;
	}

	bool ReadTransforms(
		const UE::FUsdStage& UsdStage,
		const TArray<double>& UsdTimeSamples,
		const TFunction<FTransform(double)>& ReaderFunc,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		OutPayloadData.Curves.SetNum(9);
		FRichCurve& TransXCurve = OutPayloadData.Curves[0];
		FRichCurve& TransYCurve = OutPayloadData.Curves[1];
		FRichCurve& TransZCurve = OutPayloadData.Curves[2];
		FRichCurve& RotXCurve = OutPayloadData.Curves[3];
		FRichCurve& RotYCurve = OutPayloadData.Curves[4];
		FRichCurve& RotZCurve = OutPayloadData.Curves[5];
		FRichCurve& ScaleXCurve = OutPayloadData.Curves[6];
		FRichCurve& ScaleYCurve = OutPayloadData.Curves[7];
		FRichCurve& ScaleZCurve = OutPayloadData.Curves[8];

		const FFrameRate StageFrameRate{static_cast<uint32>(UsdStage.GetTimeCodesPerSecond()), 1};
		const ERichCurveInterpMode InterpMode = (UsdStage.GetInterpolationType() == EUsdInterpolationType::Linear)
													? ERichCurveInterpMode::RCIM_Linear
													: ERichCurveInterpMode::RCIM_Constant;

		double LastTimeSample = TNumericLimits<double>::Lowest();
		for (const double UsdTimeSample : UsdTimeSamples)
		{
			// We never want to evaluate the same time twice
			if (FMath::IsNearlyEqual(UsdTimeSample, LastTimeSample))
			{
				continue;
			}
			LastTimeSample = UsdTimeSample;

			int32 FrameNumber = FMath::FloorToInt(UsdTimeSample);
			float SubFrameNumber = UsdTimeSample - FrameNumber;

			FFrameTime FrameTime{FrameNumber, SubFrameNumber};
			double FrameTimeSeconds = static_cast<float>(StageFrameRate.AsSeconds(FrameTime));

			FTransform UEValue = ReaderFunc(UsdTimeSample);
			FVector Location = UEValue.GetLocation();
			FRotator Rotator = UEValue.Rotator();
			FVector Scale = UEValue.GetScale3D();

			FKeyHandle HandleTransX = TransXCurve.AddKey(FrameTimeSeconds, Location.X);
			FKeyHandle HandleTransY = TransYCurve.AddKey(FrameTimeSeconds, Location.Y);
			FKeyHandle HandleTransZ = TransZCurve.AddKey(FrameTimeSeconds, Location.Z);
			FKeyHandle HandleRotX = RotXCurve.AddKey(FrameTimeSeconds, Rotator.Roll);
			FKeyHandle HandleRotY = RotYCurve.AddKey(FrameTimeSeconds, Rotator.Pitch);
			FKeyHandle HandleRotZ = RotZCurve.AddKey(FrameTimeSeconds, Rotator.Yaw);
			FKeyHandle HandleScaleX = ScaleXCurve.AddKey(FrameTimeSeconds, Scale.X);
			FKeyHandle HandleScaleY = ScaleYCurve.AddKey(FrameTimeSeconds, Scale.Y);
			FKeyHandle HandleScaleZ = ScaleZCurve.AddKey(FrameTimeSeconds, Scale.Z);

			TransXCurve.SetKeyInterpMode(HandleTransX, InterpMode);
			TransYCurve.SetKeyInterpMode(HandleTransY, InterpMode);
			TransZCurve.SetKeyInterpMode(HandleTransZ, InterpMode);
			RotXCurve.SetKeyInterpMode(HandleRotX, InterpMode);
			RotYCurve.SetKeyInterpMode(HandleRotY, InterpMode);
			RotZCurve.SetKeyInterpMode(HandleRotZ, InterpMode);
			ScaleXCurve.SetKeyInterpMode(HandleScaleX, InterpMode);
			ScaleYCurve.SetKeyInterpMode(HandleScaleY, InterpMode);
			ScaleZCurve.SetKeyInterpMode(HandleScaleZ, InterpMode);
		}

		return true;
	}

	void AddTextureNode(
		const UE::FUsdPrim& Prim,
		const FString& NodeUid,
		const UsdToUnreal::FTextureParameterValue& Value,
		UInterchangeBaseNodeContainer& NodeContainer
	)
	{
		FString PrimPath = Prim.GetPrimPath().GetString();
		FString NodeName{FPaths::GetCleanFilename(Value.TextureFilePath)};

		// Check if Node already exist with this ID
		if (const UInterchangeTexture2DNode* Node = Cast<UInterchangeTexture2DNode>(NodeContainer.GetNode(NodeUid)))
		{
			return;
		}

		UInterchangeTexture2DNode* Node = NewObject<UInterchangeTexture2DNode>(&NodeContainer);
		Node->InitializeNode(NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
		Node->SetPayLoadKey(EncodeTexturePayloadKey(Value));

		static_assert((int)TextureAddress::TA_Wrap == (int)EInterchangeTextureWrapMode::Wrap);
		static_assert((int)TextureAddress::TA_Clamp == (int)EInterchangeTextureWrapMode::Clamp);
		static_assert((int)TextureAddress::TA_Mirror == (int)EInterchangeTextureWrapMode::Mirror);
		Node->SetCustomWrapU((EInterchangeTextureWrapMode)Value.AddressX);
		Node->SetCustomWrapV((EInterchangeTextureWrapMode)Value.AddressY);

		Node->SetCustomSRGB(Value.GetSRGBValue());

		// Provide the other UDIM tiles
		//
		// Note: There is an bImportUDIM option on UInterchangeGenericTexturePipeline that is exclusively used within
		// UInterchangeGenericTexturePipeline::HandleCreationOfTextureFactoryNode in order to essentially do the exact same
		// thing as we do here. In theory, we shouldn't need to do this then, and in fact it is a bit bad to do so because
		// we will always parse these UDIMs whether the option is enabled or disabled. The issue however is that (as of the
		// time of this writing) UInterchangeGenericTexturePipeline::HandleCreationOfTextureFactoryNode is hard-coded to expect
		// the texture payload key to be just the texture file path. We can't do that, because we need to also encode
		// the texture compression settings onto payload key...
		//
		// All of that is to say that everything will actually work fine, but if you uncheck "bImportUDIM" on the import options
		// you will still get UDIMs (for now).
		if (Value.bIsUDIM)
		{
			TMap<int32, FString> TileIndexToPath = UE::TextureUtilitiesCommon::GetUDIMBlocksFromSourceFile(
				Value.TextureFilePath,
				UE::TextureUtilitiesCommon::DefaultUdimRegexPattern
			);
			Node->SetSourceBlocks(MoveTemp(TileIndexToPath));
		}

		NodeContainer.AddNode(Node);
	}

	// We use this visitor to set UsdToUnreal::FParameterValue TVariant values onto UInterchangeMaterialInstanceNodes
	struct FParameterValueVisitor
	{
		FParameterValueVisitor(
			const UE::FUsdPrim& InPrim,
			UInterchangeBaseNodeContainer& InNodeContainer,
			UInterchangeMaterialInstanceNode& InMaterialNode,
			const TMap<FString, int32>& InPrimvarToUVIndex
		)
			: Prim(InPrim)
			, NodeContainer(InNodeContainer)
			, MaterialNode(InMaterialNode)
			, PrimvarToUVIndex(InPrimvarToUVIndex)
		{
		}

		void operator()(const float Value) const
		{
			MaterialNode.AddScalarParameterValue(*ParameterName, Value);

			// Disable the texture input since we have a direct value
			MaterialNode.AddScalarParameterValue(*FString::Printf(TEXT("Use%sTexture"), **ParameterName), 0.0f);
		}

		void operator()(const FVector& Value) const
		{
			MaterialNode.AddVectorParameterValue(*ParameterName, FLinearColor{Value});

			// Disable the texture input since we have a direct value
			MaterialNode.AddScalarParameterValue(*FString::Printf(TEXT("Use%sTexture"), **ParameterName), 0.0f);
		}

		void operator()(const UsdToUnreal::FTextureParameterValue& Value) const
		{
			// Emit texture node itself (this is the main place where this happens)
			// Note that the node name isn't just the texture path, as we may have multiple material users of this texture
			// with different settings, and so we need separate translated nodes for each material and parameter
			FString TextureUid = FString::Printf(TEXT("Texture:%s:%s"), *Prim.GetPrimPath().GetString(), **ParameterName);
			AddTextureNode(Prim, TextureUid, Value, NodeContainer);

			// Actual texture assignment
			MaterialNode.AddTextureParameterValue(*FString::Printf(TEXT("%sTexture"), **ParameterName), TextureUid);
			MaterialNode.AddScalarParameterValue(*FString::Printf(TEXT("Use%sTexture"), **ParameterName), 1.0f);

			// UV transform
			FLinearColor ScaleAndTranslation = FLinearColor{
				Value.UVScale.GetVector()[0],
				Value.UVScale.GetVector()[1],
				Value.UVTranslation[0],
				Value.UVTranslation[1]};
			MaterialNode.AddVectorParameterValue(*FString::Printf(TEXT("%sScaleTranslation"), **ParameterName), ScaleAndTranslation);
			MaterialNode.AddScalarParameterValue(*FString::Printf(TEXT("%sRotation"), **ParameterName), Value.UVRotation);

			// UV index
			if (const int32* FoundIndex = PrimvarToUVIndex.Find(Value.Primvar))
			{
				MaterialNode.AddScalarParameterValue(*FString::Printf(TEXT("%sUVIndex"), **ParameterName), *FoundIndex);
			}
			else
			{
				UE_LOG(
					LogUsd,
					Warning,
					TEXT("Failed to find primvar '%s' when setting material parameter '%s' on material '%s'. Available primvars and UV "
						 "indices: %s.%s"),
					*Value.Primvar,
					**ParameterName,
					*Prim.GetPrimPath().GetString(),
					*UsdUtils::StringifyMap(PrimvarToUVIndex),
					Value.Primvar.IsEmpty() ? TEXT(
						" Is your UsdUVTexture Shader missing the 'inputs:st' attribute? (It specifies which UV set to sample the texture with)"
					)
											: TEXT("")
				);
			}

			// Component mask (which channel of the texture to use)
			FLinearColor ComponentMask = FLinearColor::Black;
			switch (Value.OutputIndex)
			{
				case 0:	   // RGB
					ComponentMask = FLinearColor{1.f, 1.f, 1.f, 0.f};
					break;
				case 1:	   // R
					ComponentMask = FLinearColor{1.f, 0.f, 0.f, 0.f};
					break;
				case 2:	   // G
					ComponentMask = FLinearColor{0.f, 1.f, 0.f, 0.f};
					break;
				case 3:	   // B
					ComponentMask = FLinearColor{0.f, 0.f, 1.f, 0.f};
					break;
				case 4:	   // A
					ComponentMask = FLinearColor{0.f, 0.f, 0.f, 1.f};
					break;
			}
			MaterialNode.AddVectorParameterValue(*FString::Printf(TEXT("%sTextureComponent"), **ParameterName), ComponentMask);
		}

		void operator()(const UsdToUnreal::FPrimvarReaderParameterValue& Value) const
		{
			MaterialNode.AddVectorParameterValue(*ParameterName, FLinearColor{Value.FallbackValue});

			if (Value.PrimvarName == TEXT("displayColor"))
			{
				MaterialNode.AddScalarParameterValue(TEXT("UseVertexColorForBaseColor"), 1.0f);
			}
		}

		void operator()(const bool Value) const
		{
			MaterialNode.AddScalarParameterValue(*ParameterName, static_cast<float>(Value));
		}

	public:
		const UE::FUsdPrim& Prim;
		UInterchangeBaseNodeContainer& NodeContainer;
		UInterchangeMaterialInstanceNode& MaterialNode;
		const TMap<FString, int32>& PrimvarToUVIndex;
		const FString* ParameterName = nullptr;
	};

	void UInterchangeUSDTranslatorImpl::AddMaterialNode(
		const UE::FUsdPrim& Prim,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		UInterchangeBaseNodeContainer& NodeContainer
	)
	{
		FString PrimPath = Prim.GetPrimPath().GetString();
		FString MaterialUid = MaterialPrefix + PrimPath;
		FString MaterialPrimName(Prim.GetName().ToString());

		// Check if Node already exist with this ID
		if (const UInterchangeMaterialInstanceNode* Node = Cast<UInterchangeMaterialInstanceNode>(NodeContainer.GetNode(MaterialUid)))
		{
			return;
		}

		auto SetMaterialSlotDependencies = [this, &MaterialUid]()
		{
			// Now we need to check if we have to set the slot of the mesh nodes here
			if (TArray<FMaterialSlotMesh>* SlotMeshes = PrimPathToSlotMeshNodes.Find(MaterialUid))
			{
				if (FString* NewMaterialUID = MaterialUidToActualNodeUid.Find(MaterialUid))
				{
					for (const FMaterialSlotMesh& MaterialSlotMesh : *SlotMeshes)
					{
						if (!MaterialSlotMesh.MeshNode->GetSlotMaterialDependencyUid(MaterialSlotMesh.MaterialSlotName, *NewMaterialUID))
						{
							MaterialSlotMesh.MeshNode->SetSlotMaterialDependencyUid(MaterialSlotMesh.MaterialSlotName, *NewMaterialUID);
						}
					}
				}
			}
		};

		FName RenderContext = TranslatorSettings ? TranslatorSettings->RenderContext : UnrealIdentifiers::UniversalRenderContext;

		// Check for any references of MaterialX
#if WITH_EDITOR
		if (RenderContext == UnrealIdentifiers::MaterialXRenderContext)
		{
			TArray<FString> FilePaths = UsdUtils::GetMaterialXFilePaths(Prim);
			for (const FString& File : FilePaths)
			{
				// the file has already been handled no need to do a Translate again
				if (!Translators.Find(File))
				{
					UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
					UInterchangeSourceData* SourceData = UInterchangeManager::CreateSourceData(File);

					UInterchangeTranslatorBase* Translator = InterchangeManager.GetTranslatorForSourceData(SourceData);
					// check on the Translator, it might return nullptr in case of reimport
					if (Translator)
					{
						Translator->Translate(NodeContainer);
						Translators.Add(File, Translator);
					}
				}

				// The material from the MaterialXTranslator doesn't have the same UID, both the PrimPath have the same name but not the same path
				// We need to retrieve that name (which is the Material name) in the translator, then we can map it to the right mesh
				NodeContainer.BreakableIterateNodesOfType<UInterchangeShaderGraphNode>(
					[this, &MaterialPrimName, &MaterialUid](const FString&, UInterchangeShaderGraphNode* ShaderGraphNode)
					{
						FString ShaderGraphUid = ShaderGraphNode->GetUniqueID();
						if (FPaths::GetBaseFilename(ShaderGraphUid) == MaterialPrimName)
						{
							MaterialUidToActualNodeUid.Add(MaterialUid, ShaderGraphUid);
							return true;
						}
						else
						{
							return false;
						}
					}
				);
			}

			SetMaterialSlotDependencies();

			if (!FilePaths.IsEmpty())
			{
				return;
			}
		}
#endif	  // WITH_EDITOR

		if (RenderContext == UnrealIdentifiers::UnrealRenderContext)
		{
			UE_LOG(
				LogUsd,
				Warning,
				TEXT(
					"The 'unreal' render context is not yet supported via USD Interchange: The material '%s' will use the universal render context instead"
				),
				*PrimPath
			);
			RenderContext = UnrealIdentifiers::UniversalRenderContext;
		}

		UInterchangeMaterialInstanceNode* MaterialNode = NewObject<UInterchangeMaterialInstanceNode>(&NodeContainer);
		MaterialNode->InitializeNode(MaterialUid, MaterialPrimName, EInterchangeNodeContainerType::TranslatedAsset);
		MaterialNode->SetAssetName(MaterialPrimName);
		NodeContainer.AddNode(MaterialNode);

		// Set the material instance node to the correct mesh nodes
		MaterialUidToActualNodeUid.Add(MaterialUid, MaterialUid);
		SetMaterialSlotDependencies();

		UsdToUnreal::FUsdPreviewSurfaceMaterialData MaterialData;
		const bool bSuccess = UsdToUnreal::ConvertMaterial(Prim, MaterialData, *RenderContext.ToString());

		// Set all the parameter values to the interchange node
		bool bHasUDIMTexture = false;
		FParameterValueVisitor Visitor{Prim, NodeContainer, *MaterialNode, MaterialData.PrimvarToUVIndex};
		for (TPair<FString, UsdToUnreal::FParameterValue>& Pair : MaterialData.Parameters)
		{
			Visitor.ParameterName = &Pair.Key;
			Visit(Visitor, Pair.Value);

			// Also simultaneously check if any of these parameters wants to be an UDIM texture so that we can use the VT reference material later
			if (!bHasUDIMTexture)
			{
				if (UsdToUnreal::FTextureParameterValue* TextureParameter = Pair.Value.TryGet<UsdToUnreal::FTextureParameterValue>())
				{
					if (TextureParameter->bIsUDIM)
					{
						bHasUDIMTexture = true;
					}
				}
			}
		}

		// Find and set the right reference material
		//
		// TODO: Proper VT texture support (we'd need to know the texture resolution at this point, and we haven't parsed them yet...).
		// The way it currently works on Interchange is that the factory will create a VT or nonVT version of the texture to match the
		// material parameter slot. Since we'll currently never set the VT reference material, it essentially means it will always
		// downgrade our VT textures to non-VT.
		// The only exception is how we upgrade the reference material to VT in case we have any UDIM textures, as those are trivial to
		// check for (we don't have to actually load the textures to do it)
		EUsdReferenceMaterialProperties Properties = EUsdReferenceMaterialProperties::None;
		if (UsdUtils::IsMaterialTranslucent(MaterialData))
		{
			Properties |= EUsdReferenceMaterialProperties::Translucent;
		}
		if (bHasUDIMTexture)
		{
			Properties |= EUsdReferenceMaterialProperties::VT;
		}

		FSoftObjectPath ReferenceMaterial = UsdUnreal::MaterialUtils::GetReferencePreviewSurfaceMaterial(Properties);
		MaterialNode->SetCustomParent(ReferenceMaterial.ToString());
	}

	void AddDisplayColorMaterialInstanceNodeIfNeeded(UInterchangeBaseNodeContainer& NodeContainer, const FString& DisplayColorDesc)
	{
		using namespace UsdUnreal::MaterialUtils;

		FString NodeUid = MaterialPrefix + DisplayColorDesc;

		// We'll treat the DisplayColorDesc (something like "!DisplayColor_1_0") as the material instance UID here
		const UInterchangeMaterialInstanceNode* Node = Cast<UInterchangeMaterialInstanceNode>(NodeContainer.GetNode(DisplayColorDesc));
		if (Node)
		{
			return;
		}

		// Need to create a new instance
		TOptional<FDisplayColorMaterial> ParsedMat = FDisplayColorMaterial::FromString(DisplayColorDesc);
		if (!ParsedMat)
		{
			return;
		}
		FString NodeName = ParsedMat->ToPrettyString();

		const FSoftObjectPath* ReferenceMaterialPath = GetReferenceMaterialPath(ParsedMat.GetValue());
		if (!ReferenceMaterialPath)
		{
			return;
		}

		// Not needed
		const FString ParentNodeUid;
		UInterchangeMaterialInstanceNode* NewNode = UInterchangeMaterialInstanceNode::Create(&NodeContainer, DisplayColorDesc, ParentNodeUid);
		NewNode->InitializeNode(NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);

		NewNode->SetCustomParent(ReferenceMaterialPath->GetAssetPathString());
	}

	void AddLightNode(const UE::FUsdPrim& Prim, UInterchangeBaseNodeContainer& NodeContainer)
	{
#if USE_USD_SDK

		FString NodeUid = LightPrefix + Prim.GetPrimPath().GetString();
		FString NodeName(Prim.GetName().ToString());

		// Ref. UsdToUnreal::ConvertLight
		static const FString IntensityToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsIntensity);
		static const FString ExposureToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsExposure);
		static const FString ColorToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsColor);

		float Intensity = UsdUtils::GetAttributeValue<float>(Prim, IntensityToken);
		float Exposure = UsdUtils::GetAttributeValue<float>(Prim, ExposureToken);
		FLinearColor Color = UsdUtils::GetAttributeValue<FLinearColor>(Prim, ColorToken);

		const bool bSRGB = true;
		Color.ToFColor(bSRGB);

		static const FString TemperatureToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsColorTemperature);
		static const FString UseTemperatureToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsEnableColorTemperature);

		float Temperature = UsdUtils::GetAttributeValue<float>(Prim, TemperatureToken);
		bool UseTemperature = UsdUtils::GetAttributeValue<bool>(Prim, UseTemperatureToken);

		// "Shadow enabled" currently not supported

		auto SetBaseLightProperties = [&NodeUid, &NodeName, Color, Temperature, UseTemperature](UInterchangeBaseLightNode* LightNode)
		{
			LightNode->InitializeNode(NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
			LightNode->SetAssetName(NodeName);

			LightNode->SetCustomLightColor(Color);
			LightNode->SetCustomTemperature(Temperature);
			LightNode->SetCustomUseTemperature(UseTemperature);
		};

		static const FString RadiusToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsRadius);

		if (Prim.IsA(TEXT("DistantLight")))
		{
			UInterchangeDirectionalLightNode* LightNode = NewObject<UInterchangeDirectionalLightNode>(&NodeContainer);
			SetBaseLightProperties(LightNode);

			Intensity = UsdToUnreal::ConvertLightIntensityAttr(Intensity, Exposure);
			LightNode->SetCustomIntensity(Intensity);

			// LightSourceAngle currently not supported by UInterchangeDirectionalLightNode
			// float Angle = UsdUtils::GetAttributeValue<float>(Prim, TEXTVIEW("inputs:angle"));

			NodeContainer.AddNode(LightNode);
		}
		else if (Prim.IsA(TEXT("SphereLight")))
		{
			const FUsdStageInfo StageInfo(Prim.GetStage());

			const float Radius = UsdUtils::GetAttributeValue<float>(Prim, RadiusToken);
			const float SourceRadius = UsdToUnreal::ConvertDistance(StageInfo, Radius);	   // currently not supported

			if (Prim.HasAPI(TEXT("ShapingAPI")))
			{
				UInterchangeSpotLightNode* LightNode = NewObject<UInterchangeSpotLightNode>(&NodeContainer);
				SetBaseLightProperties(LightNode);

				LightNode->SetCustomIntensityUnits(EInterchangeLightUnits::Lumens);

				static const FString ConeAngleToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsShapingConeAngle);
				static const FString ConeSoftnessToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsShapingConeSoftness);

				float ConeAngle = UsdUtils::GetAttributeValue<float>(Prim, ConeAngleToken);
				float ConeSoftness = UsdUtils::GetAttributeValue<float>(Prim, ConeSoftnessToken);

				float InnerConeAngle = 0.0f;
				const float OuterConeAngle = UsdToUnreal::ConvertConeAngleSoftnessAttr(ConeAngle, ConeSoftness, InnerConeAngle);

				Intensity = UsdToUnreal::ConvertLuxShapingAPIIntensityAttr(Intensity, Exposure, Radius, ConeAngle, ConeSoftness, StageInfo);
				LightNode->SetCustomIntensity(Intensity);

				LightNode->SetCustomInnerConeAngle(InnerConeAngle);
				LightNode->SetCustomOuterConeAngle(OuterConeAngle);

				NodeContainer.AddNode(LightNode);
			}
			else
			{
				UInterchangePointLightNode* LightNode = NewObject<UInterchangePointLightNode>(&NodeContainer);
				SetBaseLightProperties(LightNode);

				LightNode->SetCustomIntensityUnits(EInterchangeLightUnits::Lumens);

				Intensity = UsdToUnreal::ConvertSphereLightIntensityAttr(Intensity, Exposure, Radius, StageInfo);
				LightNode->SetCustomIntensity(Intensity);

				NodeContainer.AddNode(LightNode);
			}
		}
		else if (Prim.IsA(TEXT("RectLight")) || Prim.IsA(TEXT("DiskLight")))
		{
			UInterchangeRectLightNode* LightNode = NewObject<UInterchangeRectLightNode>(&NodeContainer);
			SetBaseLightProperties(LightNode);

			LightNode->SetCustomIntensityUnits(EInterchangeLightUnits::Lumens);

			static const FString WidthToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsWidth);
			static const FString HeightToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsHeight);

			float Width = UsdUtils::GetAttributeValue<float>(Prim, WidthToken);
			float Height = UsdUtils::GetAttributeValue<float>(Prim, HeightToken);

			const FUsdStageInfo StageInfo(Prim.GetStage());

			if (Prim.IsA(TEXT("RectLight")))
			{
				Width = UsdToUnreal::ConvertDistance(StageInfo, Width);
				Height = UsdToUnreal::ConvertDistance(StageInfo, Height);
				Intensity = UsdToUnreal::ConvertRectLightIntensityAttr(Intensity, Exposure, Width, Height, StageInfo);
			}
			else
			{
				float Radius = UsdUtils::GetAttributeValue<float>(Prim, RadiusToken);
				Width = UsdToUnreal::ConvertDistance(StageInfo, Radius) * 2.f;
				Height = Width;

				Intensity = UsdToUnreal::ConvertDiskLightIntensityAttr(Intensity, Exposure, Radius, StageInfo);
			}
			LightNode->SetCustomIntensity(Intensity);
			LightNode->SetCustomSourceWidth(Width);
			LightNode->SetCustomSourceHeight(Height);

			NodeContainer.AddNode(LightNode);
		}
		// #ueent_todo:
		// DomeLight -> SkyLight
#endif	  // USE_USD_SDK
	}

	void AddCameraNode(const UE::FUsdPrim& Prim, UInterchangeBaseNodeContainer& NodeContainer)
	{
#if USE_USD_SDK
		FString NodeUid = CameraPrefix + Prim.GetPrimPath().GetString();
		FString NodeName(Prim.GetName().ToString());

		UInterchangePhysicalCameraNode* CameraNode = NewObject<UInterchangePhysicalCameraNode>(&NodeContainer);
		CameraNode->InitializeNode(NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
		NodeContainer.AddNode(CameraNode);

		// ref. UsdToUnreal::ConvertGeomCamera
		UE::FUsdStage Stage = Prim.GetStage();
		FUsdStageInfo StageInfo(Stage);

		static const FString FocalLengthToken = UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->focalLength);
		static const FString HorizontalApertureToken = UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->horizontalAperture);
		static const FString VerticalApertureToken = UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->verticalAperture);

		float FocalLength = UsdUtils::GetAttributeValue<float>(Prim, FocalLengthToken);
		FocalLength = UsdToUnreal::ConvertDistance(StageInfo, FocalLength);
		CameraNode->SetCustomFocalLength(FocalLength);

		float SensorWidth = UsdUtils::GetAttributeValue<float>(Prim, HorizontalApertureToken);
		SensorWidth = UsdToUnreal::ConvertDistance(StageInfo, SensorWidth);
		CameraNode->SetCustomSensorWidth(SensorWidth);

		float SensorHeight = UsdUtils::GetAttributeValue<float>(Prim, VerticalApertureToken);
		SensorHeight = UsdToUnreal::ConvertDistance(StageInfo, SensorHeight);
		CameraNode->SetCustomSensorHeight(SensorHeight);

		// Focus distance and FStop not currently supported
#endif	  // USE_USD_SDK
	}

	void AddMorphTargetNodes(
		const UE::FUsdPrim& MeshPrim,
		UInterchangeUSDTranslatorImpl& TranslatorImpl,
		UInterchangeMeshNode& MeshNode,
		UInterchangeBaseNodeContainer& NodeContainer,
		const FTraversalInfo& Info
	)
	{
		UE::FUsdSkelBlendShapeQuery Query{MeshPrim};
		if (!Query)
		{
			return;
		}

		const FString MeshPrimPath = MeshPrim.GetPrimPath().GetString();

		TFunction<void(const FString&, int32, const FString&)> AddMorphTargetNode =
			[&MeshNode, &MeshPrimPath, &NodeContainer](const FString& MorphTargetName, int32 BlendShapeIndex, const FString& InbetweenName)
		{
			// Note: We identify a blend shape by its Mesh prim path and the blend shape index, even though
			// the blend shape itself is a full standalone prim. This is for two reasons:
			//  - We need to also read the Mesh prim's mesh data when emitting the payload, so having the Mesh path on the payload key is handy;
			//  - It could be possible for different meshes to share the same BlendShape (possibly?), so we really want a separate version of
			//    a blend shape for each mesh that uses it.
			//
			// Despite of that though, we won't use the blendshape's full path as the morph target name, so that users can get different
			// blendshapes across the model to combine into a single morph target. Interchange has an import option to let you control
			// whether they become separate morph targets or not anyway ("Merge Morph Targets with Same Name")
			const FString NodeUid = GetMorphTargetMeshNodeUid(MeshPrimPath, BlendShapeIndex, InbetweenName);
			const FString PayloadKey = GetMorphTargetMeshPayloadKey(MeshPrimPath, BlendShapeIndex, InbetweenName);

			UInterchangeMeshNode* MorphTargetMeshNode = NewObject<UInterchangeMeshNode>(&NodeContainer);
			MorphTargetMeshNode->InitializeNode(NodeUid, MorphTargetName, EInterchangeNodeContainerType::TranslatedAsset);
			MorphTargetMeshNode->SetPayLoadKey(PayloadKey, EInterchangeMeshPayLoadType::MORPHTARGET);
			MorphTargetMeshNode->SetMorphTarget(true);
			MorphTargetMeshNode->SetMorphTargetName(MorphTargetName);
			NodeContainer.AddNode(MorphTargetMeshNode);
			MeshNode.SetMorphTargetDependencyUid(NodeUid);
		};

		for (size_t Index = 0; Index < Query.GetNumBlendShapes(); ++Index)
		{
			UE::FUsdSkelBlendShape BlendShape = Query.GetBlendShape(Index);
			if (!BlendShape)
			{
				continue;
			}
			UE::FUsdPrim BlendShapePrim = BlendShape.GetPrim();
			const FString BlendShapeName = BlendShapePrim.GetName().ToString();

			const FString UnusedInbetweenName;
			AddMorphTargetNode(BlendShapeName, Index, UnusedInbetweenName);

			for (const UE::FUsdSkelInbetweenShape& Inbetween : BlendShape.GetInbetweens())
			{
				const FString InbetweenName = Inbetween.GetAttr().GetName().ToString();
				const FString MorphTargetName = BlendShapeName + TEXT("_") + InbetweenName;
				AddMorphTargetNode(MorphTargetName, Index, InbetweenName);
			}
		}
	}

	void UInterchangeUSDTranslatorImpl::AddMeshNode(
		const UE::FUsdPrim& Prim,
		UInterchangeBaseNodeContainer& NodeContainer,
		const FTraversalInfo& Info
	)
	{
		FString PrimPath = Prim.GetPrimPath().GetString();
		FString NodeUid = MeshPrefix + PrimPath;
		FString NodeName(Prim.GetName().ToString());

		// Check if Node already exist with this ID
		if (const UInterchangeMeshNode* Node = Cast<UInterchangeMeshNode>(NodeContainer.GetNode(NodeUid)))
		{
			return;
		}

		// Fill in the MeshNode itself
		UInterchangeMeshNode* MeshNode = NewObject<UInterchangeMeshNode>(&NodeContainer);
		MeshNode->InitializeNode(NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
		MeshNode->SetAssetName(NodeName);
		const bool bIsSkinned = static_cast<bool>(Info.ClosestParentSkelRoot) && Prim.HasAPI(TEXT("SkelBindingAPI"));
		if (bIsSkinned)
		{
			MeshNode->SetSkinnedMesh(true);
			MeshNode->SetPayLoadKey(PrimPath, EInterchangeMeshPayLoadType::SKELETAL);
			if (Info.ActiveSkelQuery)
			{
				MeshNode->SetSkeletonDependencyUid(Info.ActiveSkelQuery.GetSkeleton().GetPrimPath().GetString());
			}

			AddMorphTargetNodes(Prim, *this, *MeshNode, NodeContainer, Info);

			// When returning the payload data later, we'll need at the very least our SkeletonQuery, so
			// here we store the Info object into the Impl
			{
				FWriteScopeLock ScopedInfoWriteLock{CachedTraversalInfoLock};
				NodeUidToCachedTraversalInfo.Add(NodeUid, Info);
			}
		}
		else
		{
			MeshNode->SetPayLoadKey(PrimPath, EInterchangeMeshPayLoadType::STATIC);
		}

		// Material assignments
		{
			const double TimeCode = UsdUtils::GetDefaultTimeCode();
			const bool bProvideMaterialIndices = false;
			UsdUtils::FUsdPrimMaterialAssignmentInfo Assignments = UsdUtils::GetPrimMaterialAssignments(
				Prim,
				TimeCode,
				bProvideMaterialIndices,
				CachedMeshConversionOptions.RenderContext,
				CachedMeshConversionOptions.MaterialPurpose
			);

			for (const UsdUtils::FUsdPrimMaterialSlot& Slot : Assignments.Slots)
			{
				// Use the material prim path/display color desc as the material slot name, because Interchange
				// already has a mechanism to merge material slots with the same name. Using the material name itself
				// as the slot name has Interchange combine slots with identical materials, which works fine. If we
				// were to use GeomSubset names or prim names in here though, it's possible that two similarly named
				// slots in different skeletal mesh chunks (but with different materials!) could get merged together,
				// which is not what we want
				const FString& SlotName = Slot.MaterialSource;

				// Get the Uid of the material instance that we'll end up assigning to this slot
				FString MaterialInstanceUid;
				bool bIsDisplayColor = false;
				switch (Slot.AssignmentType)
				{
					case UsdUtils::EPrimAssignmentType::DisplayColor:
					{
						bIsDisplayColor = true;
						AddDisplayColorMaterialInstanceNodeIfNeeded(NodeContainer, Slot.MaterialSource);
						MaterialInstanceUid = MaterialPrefix + Slot.MaterialSource;	   // This is e.g. "!DisplayColor_0_1"
						break;
					}
					case UsdUtils::EPrimAssignmentType::MaterialPrim:
					{
						MaterialInstanceUid = MaterialPrefix + Slot.MaterialSource;	   // This is the prim path
						break;
					}
					case UsdUtils::EPrimAssignmentType::UnrealMaterial:
					{
						// TODO: We can't support these yet without a custom pipeline unfortunately
						// We could spawn a material instance of the referenced material... That's probably not what you'd expect though
						break;
					}
					default:
					{
						ensure(false);
						break;
					}
				}

				// If we're a DisplayColor material, we know everything we need right now
				if (bIsDisplayColor)
				{
					MeshNode->SetSlotMaterialDependencyUid(SlotName, *MaterialInstanceUid);
				}
				// If we found a match let's set the slot to the corresponding Material right away, as we already must have traversed this material
				else if (FString* ActualMaterialInstanceUid = MaterialUidToActualNodeUid.Find(MaterialInstanceUid))
				{
					MeshNode->SetSlotMaterialDependencyUid(SlotName, *ActualMaterialInstanceUid);
				}
				// Otherwise, we need to wait until the material prim itself is translated, as we may need to defer to another translator
				// (e.g. MaterialX) for the translation, which could generate an entirely different translated node we can't know about yet
				else
				{
					// one material can be attached to several meshes
					PrimPathToSlotMeshNodes.FindOrAdd(MaterialInstanceUid).Add({SlotName, MeshNode});
				}
			}
		}

		NodeContainer.AddNode(MeshNode);
	}

	void AddTrackSetNode(UInterchangeUSDTranslatorImpl& Impl, UInterchangeBaseNodeContainer& NodeContainer)
	{
		// For now we only want a single track set (i.e. LevelSequence) per stage.
		// TODO: One track set per layer, and add the tracks to the tracksets that correspond to layers where the opinions came from
		// (similar to LevelSequenceHelper). Then we can use UInterchangeAnimationTrackSetInstanceNode to create "subsequences"
		if (Impl.CurrentTrackSet)
		{
			return;
		}

		UE::FSdfLayer Layer = Impl.UsdStage.GetRootLayer();
		const FString AnimTrackSetNodeUid = AnimationPrefix + Layer.GetIdentifier();
		const FString AnimTrackSetNodeDisplayName = FPaths::GetBaseFilename(Layer.GetDisplayName());	// Strip extension

		// We should only have one track set node per scene for now
		const UInterchangeAnimationTrackSetNode* ExistingNode = Cast<UInterchangeAnimationTrackSetNode>(NodeContainer.GetNode(AnimTrackSetNodeUid));
		if (!ensure(ExistingNode == nullptr))
		{
			return;
		};

		UInterchangeAnimationTrackSetNode* TrackSetNode = NewObject<UInterchangeAnimationTrackSetNode>(&NodeContainer);
		TrackSetNode->InitializeNode(AnimTrackSetNodeUid, AnimTrackSetNodeDisplayName, EInterchangeNodeContainerType::TranslatedAsset);
		TrackSetNode->SetCustomFrameRate(Layer.GetFramesPerSecond());	 // Key values in Interchange seem to be in seconds, so timeCodesPerSecond is
																		 // not relevant here

		NodeContainer.AddNode(TrackSetNode);
		Impl.CurrentTrackSet = TrackSetNode;
	}

	void AddTransformAnimationNode(const UE::FUsdPrim& Prim, UInterchangeUSDTranslatorImpl& Impl, UInterchangeBaseNodeContainer& NodeContainer)
	{
		const FString PrimPath = Prim.GetPrimPath().GetString();
		const FString UniquePath = PrimPath + TEXT("\\") + UnrealIdentifiers::TransformPropertyName.ToString();
		const FString AnimTrackNodeUid = AnimationTrackPrefix + UniquePath;

		const UInterchangeTransformAnimationTrackNode* ExistingNode = Cast<UInterchangeTransformAnimationTrackNode>(
			NodeContainer.GetNode(AnimTrackNodeUid)
		);
		if (ExistingNode)
		{
			return;
		}

		UInterchangeTransformAnimationTrackNode* TransformAnimTrackNode = NewObject<UInterchangeTransformAnimationTrackNode>(&NodeContainer);
		TransformAnimTrackNode->InitializeNode(AnimTrackNodeUid, UniquePath, EInterchangeNodeContainerType::TranslatedAsset);
		TransformAnimTrackNode->SetCustomActorDependencyUid(*PrimPath);
		TransformAnimTrackNode->SetCustomAnimationPayloadKey(UniquePath, EInterchangeAnimationPayLoadType::CURVE);
		TransformAnimTrackNode->SetCustomUsedChannels((int32)EMovieSceneTransformChannel::AllTransform);

		NodeContainer.AddNode(TransformAnimTrackNode);

		AddTrackSetNode(Impl, NodeContainer);
		Impl.CurrentTrackSet->AddCustomAnimationTrackUid(AnimTrackNodeUid);
	}

	void AddPropertyAnimationNodes(const UE::FUsdPrim& Prim, UInterchangeUSDTranslatorImpl& Impl, UInterchangeBaseNodeContainer& NodeContainer)
	{
		using namespace UE::InterchangeUsdTranslator::Private;

		if (!Prim)
		{
			return;
		}
		const FString PrimPath = Prim.GetPrimPath().GetString();

		for (UE::FUsdAttribute Attr : Prim.GetAttributes())
		{
			if (!Attr || !Attr.ValueMightBeTimeVarying() || Attr.GetNumTimeSamples() == 0)
			{
				continue;
			}

			// Emit a STEPCURVE in case of a bool track: CURVE is only for floats/doubles (c.f. FLevelSequenceHelper::PopulateAnimationTrack).
			// For now we're lucky in that all possible results from GetPropertiesForAttribute() are either all not bool, or either all bool,
			// so we can reuse this for all the different UEAttrNames we get from the same attribute
			const FName AttrTypeName = Attr.GetTypeName();
			const bool bIsBoolTrack = AttrTypeName == TEXT("bool") || AttrTypeName == TEXT("token");	// Visibility is a token track

			TArray<FName> UEAttrNames = UsdUtils::GetPropertiesForAttribute(Prim, Attr.GetName().ToString());
			for (const FName& UEAttrName : UEAttrNames)
			{
				const EInterchangePropertyTracks* FoundTrackType = PropertyNameToTrackType.Find(UEAttrName);
				if (!FoundTrackType)
				{
					continue;
				}

				// We don't use the USD attribute path here because we want one unique node per UE track name,
				// so that if e.g. both "intensity" and "exposure" are animated we make a single track for
				// the Intensity UE property
				const FString UniquePath = PrimPath + TEXT("\\") + UEAttrName.ToString();
				const FString AnimTrackNodeUid = AnimationTrackPrefix + UniquePath;

				const UInterchangeAnimationTrackNode* ExistingNode = Cast<UInterchangeAnimationTrackNode>(NodeContainer.GetNode(AnimTrackNodeUid));
				if (ExistingNode)
				{
					continue;
				}

				UInterchangeAnimationTrackNode* AnimTrackNode = NewObject<UInterchangeAnimationTrackNode>(&NodeContainer);
				AnimTrackNode->InitializeNode(AnimTrackNodeUid, UniquePath, EInterchangeNodeContainerType::TranslatedAsset);
				AnimTrackNode->SetCustomActorDependencyUid(*PrimPath);
				AnimTrackNode->SetCustomPropertyTrack(*FoundTrackType);
				AnimTrackNode->SetCustomAnimationPayloadKey(
					UniquePath,
					bIsBoolTrack ? EInterchangeAnimationPayLoadType::STEPCURVE : EInterchangeAnimationPayLoadType::CURVE
				);

				NodeContainer.AddNode(AnimTrackNode);

				AddTrackSetNode(Impl, NodeContainer);
				Impl.CurrentTrackSet->AddCustomAnimationTrackUid(AnimTrackNodeUid);
			}
		}
	}

	void AddSkeletalAnimationNode(
		const UE::FUsdSkelSkeletonQuery& SkeletonQuery,
		const TMap<FString, TPair<FString, int32>>& BoneToUidAndBoneIndex,
		UInterchangeUSDTranslatorImpl& TranslatorImpl,
		UInterchangeSceneNode& SkeletonPrimNode,
		UInterchangeBaseNodeContainer& NodeContainer,
		const FTraversalInfo& Info
	)
	{
		UE::FUsdSkelAnimQuery AnimQuery = SkeletonQuery.GetAnimQuery();
		if (!AnimQuery)
		{
			return;
		}

		UE::FUsdPrim SkelAnimationPrim = AnimQuery.GetPrim();
		if (!SkelAnimationPrim)
		{
			return;
		}

		UE::FUsdPrim SkeletonPrim = SkeletonQuery.GetSkeleton();
		if (!SkeletonPrim)
		{
			return;
		}

		UE::FUsdStage Stage = SkeletonPrim.GetStage();

		const FString SkelAnimationName = SkelAnimationPrim.GetName().ToString();
		const FString SkelAnimationPrimPath = SkelAnimationPrim.GetPrimPath().GetString();
		const FString SkeletonPrimPath = SkeletonPrim.GetPrimPath().GetString();
		const FString UniquePath = SkelAnimationPrimPath + TEXT("\\") + SkeletonPrimPath;
		const FString NodeUid = AnimationTrackPrefix + UniquePath;

		const UInterchangeSkeletalAnimationTrackNode* ExistingNode = Cast<UInterchangeSkeletalAnimationTrackNode>(NodeContainer.GetNode(NodeUid));
		if (ExistingNode)
		{
			return;
		};

		UInterchangeSkeletalAnimationTrackNode* SkelAnimNode = NewObject<UInterchangeSkeletalAnimationTrackNode>(&NodeContainer);
		SkelAnimNode->InitializeNode(NodeUid, SkelAnimationName, EInterchangeNodeContainerType::TranslatedAsset);
		SkelAnimNode->SetCustomSkeletonNodeUid(SkeletonPrimNode.GetUniqueID());

		// TODO: Uncomment this whenever Interchange supports skeletal animation sections, because
		// currently it seems that InterchangeLevelSequenceFactory.cpp doesn't even have the string "skel" anywhere.
		// If we were to add this all we'd get is a warning on the output log about "all referenced actors being missing",
		// in case it failed to find anything else (e.g. other actual property/transform track) to put on the LevelSequence.
		// AddTrackSetNode(TranslatorImpl, NodeContainer);
		// TranslatorImpl->CurrentTrackSet->AddCustomAnimationTrackUid(NodeUid);

		NodeContainer.AddNode(SkelAnimNode);

		// Time info
		{
			// TODO: Match the TrackSet framerate whenever Interchange supports skeletal animation sections.
			// float TrackSetFrameRate = 30.0f;
			// if (TranslatorImpl->CurrentTrackSet->GetCustomFrameRate(TrackSetFrameRate))
			// {
			// 	SkelAnimNode->SetCustomAnimationSampleRate(TrackSetFrameRate);
			// }
			SkelAnimNode->SetCustomAnimationSampleRate(Stage.GetFramesPerSecond());

			TOptional<double> StartTimeCode;
			TOptional<double> StopTimeCode;

			// For now we don't generate LevelSequences for sublayers and will instead put everything on a single
			// LevelSequence for the entire stage, so we don't need to care so much about sublayer offset/scale like
			// UsdToUnreal::ConvertSkelAnim does
			TArray<double> JointTimeSamples;
			if (AnimQuery.GetJointTransformTimeSamples(JointTimeSamples) && JointTimeSamples.Num() > 0)
			{
				StartTimeCode = JointTimeSamples[0];
				StopTimeCode = JointTimeSamples[JointTimeSamples.Num() - 1];
			}
			TArray<double> BlendShapeTimeSamples;
			if (AnimQuery.GetBlendShapeWeightTimeSamples(BlendShapeTimeSamples) && BlendShapeTimeSamples.Num() > 0)
			{
				StartTimeCode = FMath::Min(BlendShapeTimeSamples[0], StartTimeCode.Get(TNumericLimits<double>::Max()));
				StopTimeCode = FMath::Max(BlendShapeTimeSamples[BlendShapeTimeSamples.Num() - 1], StopTimeCode.Get(TNumericLimits<double>::Lowest()));
			}

			UE::FUsdStage UsdStage = SkeletonPrim.GetStage();
			double TimeCodesPerSecond = UsdStage.GetTimeCodesPerSecond();
			if (StartTimeCode.IsSet())
			{
				SkelAnimNode->SetCustomAnimationStartTime(StartTimeCode.GetValue() / TimeCodesPerSecond);
			}
			if (StopTimeCode.IsSet())
			{
				SkelAnimNode->SetCustomAnimationStopTime(StopTimeCode.GetValue() / TimeCodesPerSecond);
			}
		}

		// Joint animation
		TArray<FString> UsdJointOrder = AnimQuery.GetJointOrder();
		for (const FString& FullAnimatedBoneName : UsdJointOrder)
		{
			const TPair<FString, int32>* FoundPair = BoneToUidAndBoneIndex.Find(FullAnimatedBoneName);
			if (!FoundPair)
			{
				continue;
			}

			const FString& BoneSceneNodeUid = FoundPair->Key;
			int32 SkeletonOrderBoneIndex = FoundPair->Value;

			const FString BoneAnimPayloadKey = SkeletonPrimPath + TEXT("\\") + LexToString(SkeletonOrderBoneIndex);

			// When retrieving the payload later, We'll need that bone's index within the Skeleton prim to index into the
			// InUsdSkeletonQuery.ComputeJointLocalTransforms() results.
			// Note that we're describing joint transforms with baked frames here. It would have been possible to use transform
			// curves, but that may have lead to issues when interpolating problematic joint transforms. Instead, we'll bake
			// using USD, and let it interpolate the transforms however it wants
			SkelAnimNode->SetAnimationPayloadKeyForSceneNodeUid(BoneSceneNodeUid, BoneAnimPayloadKey, EInterchangeAnimationPayLoadType::BAKED);
		}

		// Morph targets
		{
			UE::FUsdSkelBinding SkelBinding;
			const bool bTraverseInstanceProxies = true;
			bool bSuccess = Info.FurthestSkelCache->ComputeSkelBinding(	   //
				Info.ClosestParentSkelRoot,
				SkeletonPrim,
				SkelBinding,
				bTraverseInstanceProxies
			);
			if (!bSuccess)
			{
				return;
			}

			TArray<FString> SkelAnimChannelOrder = AnimQuery.GetBlendShapeOrder();

			TMap<FString, int32> SkelAnimChannelIndices;
			SkelAnimChannelIndices.Reserve(SkelAnimChannelOrder.Num());
			for (int32 ChannelIndex = 0; ChannelIndex < SkelAnimChannelOrder.Num(); ++ChannelIndex)
			{
				const FString& ChannelName = SkelAnimChannelOrder[ChannelIndex];
				SkelAnimChannelIndices.Add(ChannelName, ChannelIndex);
			}

			TArray<UE::FUsdSkelSkinningQuery> SkinningTargets = SkelBinding.GetSkinningTargets();
			for (const UE::FUsdSkelSkinningQuery& SkinningTarget : SkinningTargets)
			{
				// USD lets you "skin" anything that can take the SkelBindingAPI, but we only care about Mesh here as
				// those are the only ones that can have blendshapes
				UE::FUsdPrim Prim = SkinningTarget.GetPrim();
				if (!Prim.IsA(TEXT("Mesh")))
				{
					continue;
				}
				const FString MeshPrimPath = Prim.GetPrimPath().GetString();

				TArray<FString> BlendShapeChannels;
				bool bInnerSucces = SkinningTarget.GetBlendShapeOrder(BlendShapeChannels);
				if (!bInnerSucces)
				{
					continue;
				}

				TArray<UE::FSdfPath> Targets;
				{
					UE::FUsdRelationship BlendShapeTargetsRel = SkinningTarget.GetBlendShapeTargetsRel();
					if (!BlendShapeTargetsRel)
					{
						continue;
					}
					bInnerSucces = BlendShapeTargetsRel.GetTargets(Targets);
					if (!bInnerSucces)
					{
						continue;
					}
				}

				if (BlendShapeChannels.Num() != Targets.Num())
				{
					UE_LOG(
						LogUsd,
						Warning,
						TEXT(
							"Skipping morph target curves for animation of skinned mesh '%s' because the number of entries in the 'skel:blendShapes' attribute (%d) doesn't match the number of entries in the 'skel:blendShapeTargets' attribute (%d)"
						),
						*MeshPrimPath,
						BlendShapeChannels.Num(),
						Targets.Num()
					);
					continue;
				}

				for (int32 BlendShapeIndex = 0; BlendShapeIndex < Targets.Num(); ++BlendShapeIndex)
				{
					const FString& ChannelName = BlendShapeChannels[BlendShapeIndex];
					int32* FoundSkelAnimChannelIndex = SkelAnimChannelIndices.Find(ChannelName);
					if (!FoundSkelAnimChannelIndex)
					{
						// This channel is not animated by this SkelAnimation prim
						continue;
					}

					// Note that we put no inbetween name on the MorphTargetUid: We only need to emit the morph target curve payloads
					// for the main shapes: We'll provide the inbetween "positions" when providing the curve and Interchange computes
					// the inbetween curves automatically
					const FString BlendShapePath = Targets[BlendShapeIndex].GetString();
					const FString MorphTargetUid = GetMorphTargetMeshNodeUid(MeshPrimPath, BlendShapeIndex);
					const FString PayloadKey = GetMorphTargetCurvePayloadKey(SkeletonPrimPath, *FoundSkelAnimChannelIndex, BlendShapePath);

					SkelAnimNode->SetAnimationPayloadKeyForMorphTargetNodeUid(	  //
						MorphTargetUid,
						PayloadKey,
						EInterchangeAnimationPayLoadType::MORPHTARGETCURVE
					);
				}
			}
		}
	}

	void AddSkeletonNodes(
		const UE::FUsdPrim& Prim,
		UInterchangeUSDTranslatorImpl& TranslatorImpl,
		UInterchangeSceneNode& SkeletonPrimNode,
		UInterchangeBaseNodeContainer& NodeContainer,
		FTraversalInfo& Info
	)
	{
		// If we're not inside of a SkelRoot, the skeleton shouldn't really do anything
		if (!Info.FurthestSkelCache.IsValid())
		{
			return;
		}

		// By the time we get here we've already emitted a scene node for the skeleton prim itself, so we just
		// need to emit a node hierarchy that mirrors the joints.

		// Make the prim node into an Interchange joint/bone itself. By doing this we solve three issues:
		//  - It becomes easy to identify our SkeletonDependencyUid when parsing Mesh nodes: It's just the skeleton prim path
		//    (as opposed to having to target the translated node of the first root joint of the skeleton);
		//  - We automatically handle USD skeletons with multiple root bones: We'll only ever have one "true"
		//    root bone anyway: The SkeletonPrimNode itself;
		//  - If a skeleton has no bones at all somehow, we'll still make one "bone" for it (this node).
		SkeletonPrimNode.AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());
		SkeletonPrimNode.SetCustomBindPoseLocalTransform(&NodeContainer, FTransform::Identity);
		SkeletonPrimNode.SetCustomTimeZeroLocalTransform(&NodeContainer, FTransform::Identity);
		const FString SkeletonPrimNodeUid = SkeletonPrimNode.GetUniqueID();

#if WITH_EDITOR
		// Convert the skeleton bones/joints into ConvertedData
		UE::FUsdSkelSkeletonQuery SkelQuery = Info.FurthestSkelCache->GetSkelQuery(Prim);
		const bool bEnsureAtLeastOneBone = false;
		const bool bEnsureSingleRootBone = false;
		UsdToUnreal::FUsdSkeletonData ConvertedData;
		const bool bSuccess = UsdToUnreal::ConvertSkeleton(SkelQuery, ConvertedData, bEnsureAtLeastOneBone, bEnsureSingleRootBone);
		if (!bSuccess)
		{
			return;
		}

		// Maps from the USD-style full bone name (e.g. "shoulder/elbow/hand") to the Uid we used for
		// the corresponding scene node, and the bone's index on the skeleton's joint order.
		// We'll need this to parse skeletal animations, if any
		TMap<FString, TPair<FString, int32>> BoneToUidAndBoneIndex;

		// Recursively traverse ConvertedData spawning the joint translated nodes
		TFunction<void(int32, UInterchangeSceneNode&, const FString&)> RecursiveTraverseBones = nullptr;
		RecursiveTraverseBones = [&RecursiveTraverseBones,
								  &BoneToUidAndBoneIndex,
								  &SkeletonPrimNodeUid,
								  &ConvertedData,
								  &NodeContainer	//
		](int32 BoneIndex, UInterchangeSceneNode& ParentNode, const FString& BonePath)
		{
			const UsdToUnreal::FUsdSkeletonData::FBone& Bone = ConvertedData.Bones[BoneIndex];

			// Reconcatenate a full "bone path" here for uniqueness, because Bone.Name is just the name of this
			// single bone/joint itself (e.g. "Elbow")
			const FString ConcatBonePath = (BonePath.IsEmpty() ? TEXT("") : (BonePath + TEXT("/"))) + Bone.Name;

			// Putting the BonePrefix here avoids the pathological case where the user has skeleton child prims
			// with names that match the joint names
			const FString BoneNodeUid = SkeletonPrimNodeUid + BonePrefix + ConcatBonePath;

			UInterchangeSceneNode* BoneNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
			BoneNode->InitializeNode(BoneNodeUid, Bone.Name, EInterchangeNodeContainerType::TranslatedScene);
			BoneNode->AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());

			// Note that we use our rest transforms for the Interchange bind pose as well: This because Interchange
			// will put this on the RefSkeleton and so it will make its way to the Skeleton asset. We already kind
			// of bake in our skeleton bind pose directly into our skinned mesh, so we really just want to put the
			// rest pose on the skeleton asset/ReferenceSkeleton
			BoneNode->SetCustomBindPoseLocalTransform(&NodeContainer, Bone.LocalRestTransform);
			BoneNode->SetCustomTimeZeroLocalTransform(&NodeContainer, Bone.LocalRestTransform);
			BoneNode->SetCustomLocalTransform(&NodeContainer, Bone.LocalRestTransform);

			NodeContainer.AddNode(BoneNode);
			NodeContainer.SetNodeParentUid(BoneNodeUid, ParentNode.GetUniqueID());

			BoneToUidAndBoneIndex.Add(ConcatBonePath, {BoneNodeUid, BoneIndex});

			for (int32 ChildIndex : Bone.ChildIndices)
			{
				RecursiveTraverseBones(ChildIndex, *BoneNode, ConcatBonePath);
			}
		};

		// Start traversing from the root bones (we may have more than one, so check them all)
		TSet<FString> UsedBoneNames;
		for (int32 BoneIndex = 0; BoneIndex < ConvertedData.Bones.Num(); ++BoneIndex)
		{
			const UsdToUnreal::FUsdSkeletonData::FBone& Bone = ConvertedData.Bones[BoneIndex];
			UsedBoneNames.Add(Bone.Name);

			if (Bone.ParentIndex == INDEX_NONE)
			{
				const FString BonePathRoot = TEXT("");
				RecursiveTraverseBones(BoneIndex, SkeletonPrimNode, BonePathRoot);
			}
		}

		// Interchange will abort parsing skeletons that don't have unique names for each bone. If the user has that
		// on their actual skeleton, then that's just invalid data and we can just let it fail and emit the error message.
		// However, we don't want to end up with duplicate bone names and fail to parse when the duplicate "bone" is due to
		// how we actually use the Skeleton prim itself as the root, as that's our little "trick". In this case, here we
		// just change the display text of the skeleton prim itself to be unique (which is used for the bone name)
		const FString SkeletonPrimName = SkeletonPrimNode.GetDisplayLabel();
		FString NewSkeletonPrimName = UsdUnreal::ObjectUtils::GetUniqueName(SkeletonPrimName, UsedBoneNames);
		if (NewSkeletonPrimName != SkeletonPrimName)
		{
			SkeletonPrimNode.SetDisplayLabel(NewSkeletonPrimName);
		}

		// Handle SkelAnimation prims, if we have any bound for this Skeleton
		AddSkeletalAnimationNode(SkelQuery, BoneToUidAndBoneIndex, TranslatorImpl, SkeletonPrimNode, NodeContainer, Info);

		// Cache our joint names in order, as this is needed when generating skeletal mesh payloads
		Info.SkelJointNames = MakeShared<TArray<FString>>();
		Info.SkelJointNames->Reserve(ConvertedData.Bones.Num());
		for (const UsdToUnreal::FUsdSkeletonData::FBone& Bone : ConvertedData.Bones)
		{
			Info.SkelJointNames->Add(Bone.Name);
		}
		{
			FWriteScopeLock ScopedInfoWriteLock{TranslatorImpl.CachedTraversalInfoLock};
			TranslatorImpl.NodeUidToCachedTraversalInfo.Add(SkeletonPrimNodeUid, Info);
		}
#endif
	}

	void Traverse(
		const UE::FUsdPrim& Prim,
		UInterchangeUSDTranslatorImpl& TranslatorImpl,
		UInterchangeBaseNodeContainer& NodeContainer,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		FTraversalInfo Info
	)
	{
		// Ignore prim subtrees from disabled purposes
		// TODO: Move this to the pipeline and filter only the factory nodes
		EUsdPurpose PrimPurpose = IUsdPrim::GetPurpose(Prim);
		if (!EnumHasAllFlags(TranslatorImpl.CachedMeshConversionOptions.PurposesToLoad, PrimPurpose))
		{
			return;
		}

		FString SceneNodeUid = Prim.GetPrimPath().GetString();
		FString DisplayLabel(Prim.GetName().ToString());

		// Do this before generating other nodes as they may need the updated info
		UpdateTraversalInfo(Info, Prim);

		// Generate asset node if applicable
		const FString* Prefix = nullptr;
		if (Prim.IsA(TEXT("Material")))
		{
			Prefix = &MaterialPrefix;
			TranslatorImpl.AddMaterialNode(Prim, TranslatorSettings, NodeContainer);
		}
		else if (Prim.IsA(TEXT("Mesh")))
		{
			Prefix = &MeshPrefix;
			TranslatorImpl.AddMeshNode(Prim, NodeContainer, Info);
		}
		else if (Prim.IsA(TEXT("Camera")))
		{
			Prefix = &CameraPrefix;
			AddCameraNode(Prim, NodeContainer);
		}
		else if (Prim.HasAPI(TEXT("LightAPI")))
		{
			Prefix = &LightPrefix;
			AddLightNode(Prim, NodeContainer);
		}

		// Only prims that require rendering (and have a renderable parent) get a scene node.
		// This includes Xforms but also Scopes, which are not Xformable
		UInterchangeSceneNode* SceneNode = nullptr;
		if (Prim.IsA(TEXT("Imageable")) && (Info.ParentNode || Prim.GetParent().IsPseudoRoot()))
		{
			SceneNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
			SceneNode->InitializeNode(SceneNodeUid, DisplayLabel, EInterchangeNodeContainerType::TranslatedScene);
			NodeContainer.AddNode(SceneNode);

			// If we're an Xformable, get our transform
			FTransform Transform = FTransform::Identity;
			bool bResetTransformStack = false;
			if (UsdToUnreal::ConvertXformable(
					Prim.GetStage(),
					UE::FUsdTyped(Prim),
					Transform,
					UsdUtils::GetEarliestTimeCode(),
					&bResetTransformStack
				))
			{
				SceneNode->SetCustomLocalTransform(&NodeContainer, Transform);
			}

			// Skeleton joints are separate scene nodes in Interchange, so we need to emit that node hierarchy now
			if (Prim.IsA(TEXT("Skeleton")))
			{
				AddSkeletonNodes(Prim, TranslatorImpl, *SceneNode, NodeContainer, Info);
			}

			// Connect scene node and asset node
			if (Prefix)
			{
				const FString AssetNodeUid = *Prefix + SceneNodeUid;
				SceneNode->SetCustomAssetInstanceUid(AssetNodeUid);
			}

			// Connect parent and child scene nodes
			if (Info.ParentNode)
			{
				NodeContainer.SetNodeParentUid(SceneNode->GetUniqueID(), Info.ParentNode->GetUniqueID());
			}

			// Add animation tracks
			AddPropertyAnimationNodes(Prim, TranslatorImpl, NodeContainer);
			if (UsdUtils::HasAnimatedTransform(Prim))
			{
				AddTransformAnimationNode(Prim, TranslatorImpl, NodeContainer);
			}
		}

		// Note: This has the effect of effectively shutting down the generation of scene nodes
		// below any prim that is not a least an Imageable, as we check for a valid parent before
		// generating one
		Info.ParentNode = SceneNode;

		// Recurse into child prims
		for (const FUsdPrim& ChildPrim : Prim.GetChildren())
		{
			Traverse(ChildPrim, TranslatorImpl, NodeContainer, TranslatorSettings, Info);
		}
	}

	bool GetStaticMeshPayloadData(
		const FString& PayloadKey,
		const UInterchangeUSDTranslatorImpl& Impl,
		const UsdToUnreal::FUsdMeshConversionOptions& Options,
		FMeshDescription& OutMeshDescription
	)
	{
		const FString& PrimPath = PayloadKey;
		UE::FUsdPrim Prim = Impl.UsdStage.GetPrimAtPath(UE::FSdfPath{*PrimPath});
		if (!Prim)
		{
			return false;
		}

		// TODO: We can't do much with these yet: There will be used to generate primvar-compatible
		// versions of the materials that are assigned to this mesh, whenever we get a pipeline
		UsdUtils::FUsdPrimMaterialAssignmentInfo TempMaterialInfo;
		bool bSuccess = UsdToUnreal::ConvertGeomMesh(Prim, OutMeshDescription, TempMaterialInfo, Options);
		if (!bSuccess)
		{
			return false;
		}

		FixMaterialSlotNames(OutMeshDescription, TempMaterialInfo.Slots);

		return true;
	}

	bool GetSkeletalMeshPayloadData(
		const FString& PayloadKey,
		const UInterchangeUSDTranslatorImpl& Impl,
		const UsdToUnreal::FUsdMeshConversionOptions& Options,
		FMeshDescription& OutMeshDescription,
		TArray<FString>& OutJointNames
	)
	{
#if WITH_EDITOR
		const FString& PrimPath = PayloadKey;
		UE::FUsdPrim Prim = Impl.UsdStage.GetPrimAtPath(UE::FSdfPath{*PrimPath});
		if (!Prim)
		{
			return false;
		}

		const FString& MeshNodeUid = MeshPrefix + Prim.GetPrimPath().GetString();

		// Read these variables from the data we cached during traversal for translation
		TSharedPtr<TArray<FString>> JointNames = nullptr;
		UE::FUsdSkelSkeletonQuery SkelQuery;
		{
			FReadScopeLock ReadLock{Impl.CachedTraversalInfoLock};

			const FTraversalInfo* MeshInfo = Impl.NodeUidToCachedTraversalInfo.Find(MeshNodeUid);
			if (!MeshInfo)
			{
				return false;
			}
			SkelQuery = MeshInfo->ActiveSkelQuery;
			if (!SkelQuery)
			{
				return false;
			}

			// The above fields are associated to the mesh *asset* node Uid (hence the prefix),
			// while the joint names are associated to the skeleton *scene* node Uid, so no prefix
			const FString SkeletonNodeUid = SkelQuery.GetSkeleton().GetPrimPath().GetString();
			const FTraversalInfo* SkeletonInfo = Impl.NodeUidToCachedTraversalInfo.Find(SkeletonNodeUid);
			if (!SkeletonInfo)
			{
				return false;
			}
			JointNames = SkeletonInfo->SkelJointNames;
			if (!JointNames)
			{
				return false;
			}
		}

		UE::FUsdSkelSkinningQuery SkinningQuery = UsdUtils::CreateSkinningQuery(Prim, SkelQuery);
		if (!SkinningQuery)
		{
			return false;
		}

		FSkeletalMeshImportData SkelMeshImportData;
		UsdUtils::FUsdPrimMaterialAssignmentInfo TempMaterialInfo;
		bool bSuccess = UsdToUnreal::ConvertSkinnedMesh(SkinningQuery, SkelQuery, SkelMeshImportData, TempMaterialInfo, Options);
		if (!bSuccess)
		{
			return false;
		}

		// TODO: Swap this code path with some function to directly convert a skinned USD mesh to MeshDescription.
		// We need that on the other USD workflows as well, not only here...
		//
		// Note: This is also doubly bad because it internally recomputes tangents and normals, which will also
		// be done by Interchange later..
		const USkeletalMesh* UnusedSkelMesh = nullptr;
		FSkeletalMeshBuildSettings* UnusedBuildSettings = nullptr;
		bSuccess = SkelMeshImportData.GetMeshDescription(UnusedSkelMesh, UnusedBuildSettings, OutMeshDescription);
		if (!bSuccess)
		{
			return false;
		}

		FixSkeletalMeshDescriptionColors(OutMeshDescription);

		FixMaterialSlotNames(OutMeshDescription, TempMaterialInfo.Slots);

		OutJointNames = *JointNames;

		return true;
#else
		return false;
#endif	  // WITH_EDITOR
	}

	bool GetMorphTargetPayloadData(
		const FString& PayloadKey,
		const UInterchangeUSDTranslatorImpl& Impl,
		const UsdToUnreal::FUsdMeshConversionOptions& Options,
		FMeshDescription& OutMeshDescription,
		FString& OutMorphTargetName
	)
	{
		// These payload keys are generated by GetMorphTargetMeshPayloadKey(), and so should take the form
		// "<mesh prim path>\<mesh blend shape index>\<optional inbetween name>"
		const bool bCullEmpty = false;
		TArray<FString> PayloadKeyTokens;
		PayloadKey.ParseIntoArray(PayloadKeyTokens, TEXT("\\"), bCullEmpty);
		if (PayloadKeyTokens.Num() != 3)
		{
			return false;
		}

		const FString& MeshPrimPath = PayloadKeyTokens[0];
		const FString& BlendShapeIndexStr = PayloadKeyTokens[1];
		const FString& InbetweenName = PayloadKeyTokens[2];

		int32 BlendShapeIndex = INDEX_NONE;
		bool bLexed = LexTryParseString(BlendShapeIndex, *BlendShapeIndexStr);
		if (!bLexed)
		{
			return false;
		}

		UE::FUsdPrim MeshPrim = Impl.UsdStage.GetPrimAtPath(FSdfPath{*MeshPrimPath});
		UE::FUsdSkelBlendShapeQuery Query{MeshPrim};
		if (!Query)
		{
			return false;
		}

		UE::FUsdSkelBlendShape BlendShape = Query.GetBlendShape(BlendShapeIndex);
		if (!BlendShape)
		{
			return false;
		}

		// TODO: This is extremely slow, as it will reimport the mesh for every single morph target!
		// It seems to be what the other translators do, however. We need some form of FMeshDescription caching here
		TArray<FString> UnusedJointNames;
		bool bConverted = GetSkeletalMeshPayloadData(MeshPrimPath, Impl, Options, OutMeshDescription, UnusedJointNames);
		if (!bConverted || OutMeshDescription.IsEmpty())
		{
			return false;
		}

		OutMorphTargetName = BlendShape.GetPrim().GetName().ToString();
		if (!InbetweenName.IsEmpty())
		{
			OutMorphTargetName += TEXT("_") + InbetweenName;
		}

		const float Weight = 1.0f;
		return UsdUtils::ApplyBlendShape(OutMeshDescription, BlendShape.GetPrim(), Weight, InbetweenName);
	}

	bool GetPropertyAnimationCurvePayloadData(
		const UE::FUsdStage& UsdStage,
		const FString& PayloadKey,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		FString PrimPath;
		FString UEPropertyNameStr;
		bool bSplit = PayloadKey.Split(TEXT("\\"), &PrimPath, &UEPropertyNameStr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (!bSplit)
		{
			return false;
		}

		UE::FUsdPrim Prim = UsdStage.GetPrimAtPath(UE::FSdfPath{*PrimPath});
		FName UEPropertyName = *UEPropertyNameStr;
		if (!Prim || UEPropertyName == NAME_None)
		{
			return false;
		}

		TArray<double> TimeSampleUnion;
		TArray<UE::FUsdAttribute> Attrs = UsdUtils::GetAttributesForProperty(Prim, UEPropertyName);
		bool bSuccess = UE::FUsdAttribute::GetUnionedTimeSamples(Attrs, TimeSampleUnion);
		if (!bSuccess)
		{
			return false;
		}

		const bool bIgnorePrimLocalTransform = false;
		UsdToUnreal::FPropertyTrackReader Reader = UsdToUnreal::CreatePropertyTrackReader(Prim, UEPropertyName, bIgnorePrimLocalTransform);
		if (Reader.BoolReader)
		{
			return ReadBools(UsdStage, TimeSampleUnion, Reader.BoolReader, OutPayloadData);
		}
		else if (Reader.ColorReader)
		{
			return ReadColors(UsdStage, TimeSampleUnion, Reader.ColorReader, OutPayloadData);
		}
		else if (Reader.FloatReader)
		{
			return ReadFloats(UsdStage, TimeSampleUnion, Reader.FloatReader, OutPayloadData);
		}
		else if (Reader.TransformReader)
		{
			return ReadTransforms(UsdStage, TimeSampleUnion, Reader.TransformReader, OutPayloadData);
		}

		return false;
	}

	bool GetJointAnimationCurvePayloadData(
		const UInterchangeUSDTranslatorImpl& Impl,
		const TArray<const UE::Interchange::FAnimationPayloadQuery*>& Queries,
		TArray<UE::Interchange::FAnimationPayloadData>& OutPayloadData
	)
	{
		if (Queries.Num() == 0)
		{
			return false;
		}

		// We expect all queries to be for the same skeleton, and have the same timing parameters,
		// since they were grouped up by HashAnimPayloadQuery, so let's just grab one for the params
		const UE::Interchange::FAnimationPayloadQuery* FirstQuery = Queries[0];

		// Parse payload key.
		// Here is takes the form "<skeleton prim path>\<joint index in skeleton order>"
		TArray<FString> PayloadKeyTokens;
		FirstQuery->PayloadKey.UniqueId.ParseIntoArray(PayloadKeyTokens, TEXT("\\"));
		if (PayloadKeyTokens.Num() != 2)
		{
			return false;
		}

		// Fetch our cached skeleton query
		const FString& SkeletonPrimPath = PayloadKeyTokens[0];
		UE::FUsdSkelSkeletonQuery SkelQuery;
		{
			FReadScopeLock ReadLock{Impl.CachedTraversalInfoLock};

			const FTraversalInfo* MeshInfo = Impl.NodeUidToCachedTraversalInfo.Find(SkeletonPrimPath);
			if (!MeshInfo)
			{
				return false;
			}
			SkelQuery = MeshInfo->ActiveSkelQuery;
			if (!SkelQuery)
			{
				return false;
			}
		}

		UE::FUsdPrim SkeletonPrim = SkelQuery.GetPrim();
		UE::FUsdStage Stage = SkeletonPrim.GetStage();
		FUsdStageInfo StageInfo{Stage};

		// Compute the bake ranges and intervals
		double TimeCodesPerSecond = Stage.GetTimeCodesPerSecond();
		double BakeFrequency = FirstQuery->TimeDescription.BakeFrequency;
		double RangeStartSeconds = FirstQuery->TimeDescription.RangeStartSecond;
		double RangeStopSeconds = FirstQuery->TimeDescription.RangeStopSecond;
		double SectionLengthSeconds = RangeStopSeconds - RangeStartSeconds;
		double StartTimeCode = RangeStartSeconds * TimeCodesPerSecond;
		const int32 NumBakedFrames = FMath::RoundToInt(FMath::Max(SectionLengthSeconds * TimeCodesPerSecond + 1.0, 1.0));
		double TimeCodeIncrement = (1.0 / BakeFrequency) * TimeCodesPerSecond;

		// Bake all joint transforms via USD into arrays for each separate joint (in whatever order SkelQuery gives us)
		TArray<TArray<FTransform>> BakedTransforms;
		for (int32 FrameIndex = 0; FrameIndex < NumBakedFrames; ++FrameIndex)
		{
			const double FrameTimeCode = StartTimeCode + FrameIndex * TimeCodeIncrement;

			TArray<FTransform> TransformsForTimeCode;
			bool bSuccess = SkelQuery.ComputeJointLocalTransforms(TransformsForTimeCode, FrameTimeCode);
			if (!bSuccess)
			{
				break;
			}

			for (FTransform& Transform : TransformsForTimeCode)
			{
				Transform = UsdUtils::ConvertTransformToUESpace(StageInfo, Transform);
			}

			// Setup our BakedTransforms in here, because we may actually get more or less transforms
			// from the SkeletonQuery than our AnimSequence wants/expects, given that it can specify
			// its own animated joint order
			int32 NumSkelJoints = TransformsForTimeCode.Num();
			if (FrameIndex == 0)
			{
				BakedTransforms.SetNum(NumSkelJoints);
				for (int32 JointIndex = 0; JointIndex < NumSkelJoints; ++JointIndex)
				{
					BakedTransforms[JointIndex].SetNum(NumBakedFrames);
				}
			}

			// Transpose our baked transforms into the arrays we'll eventually return
			for (int32 JointIndex = 0; JointIndex < NumSkelJoints; ++JointIndex)
			{
				BakedTransforms[JointIndex][FrameIndex] = TransformsForTimeCode[JointIndex];
			}
		}

		// Finally build our payload data return values by picking the desired baked arrays with the payload joint indices
		OutPayloadData.Reset(Queries.Num());
		for (int32 QueryIndex = 0; QueryIndex < Queries.Num(); ++QueryIndex)
		{
			const UE::Interchange::FAnimationPayloadQuery* Query = Queries[QueryIndex];

			FString IndexStr = Query->PayloadKey.UniqueId.RightChop(SkeletonPrimPath.Len() + 1);	// Also skip the '\'
			int32 JointIndex = INDEX_NONE;
			bool bLexed = LexTryParseString(JointIndex, *IndexStr);
			if (!bLexed)
			{
				continue;
			}

			UE::Interchange::FAnimationPayloadData& PayloadData = OutPayloadData.Emplace_GetRef(Query->SceneNodeUniqueID, Query->PayloadKey);
			PayloadData.BakeFrequency = BakeFrequency;
			PayloadData.RangeStartTime = RangeStartSeconds;
			PayloadData.RangeEndTime = RangeStopSeconds;

			if (BakedTransforms.IsValidIndex(JointIndex))
			{
				PayloadData.Transforms = MoveTemp(BakedTransforms[JointIndex]);
			}
		}

		return true;
	}

	bool GetMorphTargetAnimationCurvePayloadData(
		const UInterchangeUSDTranslatorImpl& Impl,
		const FString& PayloadKey,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		// Here we must output the morph target curve for a particular channel and skinning target, i.e.
		// the connection of a SkelAnimation blend shape channel to a particular Mesh prim.

		// These payload keys were generated from GetMorphTargetCurvePayloadKey(), so they take the form
		// "<skeleton prim path>\<skel anim channel index>\<blend shape path>"
		TArray<FString> PayloadKeyTokens;
		PayloadKey.ParseIntoArray(PayloadKeyTokens, TEXT("\\"));
		if (PayloadKeyTokens.Num() != 3)
		{
			return false;
		}
		const FString& SkeletonPrimPath = PayloadKeyTokens[0];
		const FString& AnimChannelIndexStr = PayloadKeyTokens[1];
		const FString& BlendShapePath = PayloadKeyTokens[2];

		const UE::FUsdStage& UsdStage = Impl.UsdStage;

		int32 SkelAnimChannelIndex = INDEX_NONE;
		bool bLexed = LexTryParseString(SkelAnimChannelIndex, *AnimChannelIndexStr);

		UE::FUsdPrim BlendShapePrim = UsdStage.GetPrimAtPath(UE::FSdfPath{*BlendShapePath});
		UE::FUsdSkelBlendShape BlendShape{BlendShapePrim};
		if (!BlendShape || !bLexed || SkelAnimChannelIndex == INDEX_NONE)
		{
			return false;
		}
		const FString BlendShapeName = BlendShapePrim.GetName().ToString();

		// Fill in the actual morph target curve
		UE::FUsdSkelAnimQuery AnimQuery;
		{
			UE::FUsdSkelSkeletonQuery SkelQuery;
			{
				FReadScopeLock ReadLock{Impl.CachedTraversalInfoLock};

				const FTraversalInfo* MeshInfo = Impl.NodeUidToCachedTraversalInfo.Find(SkeletonPrimPath);
				if (!MeshInfo)
				{
					return false;
				}
				SkelQuery = MeshInfo->ActiveSkelQuery;
				if (!SkelQuery)
				{
					return false;
				}
			}

			AnimQuery = SkelQuery.GetAnimQuery();
			if (!AnimQuery)
			{
				return false;
			}

			TArray<double> TimeCodes;
			bool bSuccess = AnimQuery.GetBlendShapeWeightTimeSamples(TimeCodes);
			if (!bSuccess)
			{
				return false;
			}

			OutPayloadData.Curves.SetNum(1);
			FRichCurve& Curve = OutPayloadData.Curves[0];
			Curve.ReserveKeys(TimeCodes.Num());

			const FFrameRate StageFrameRate{static_cast<uint32>(UsdStage.GetTimeCodesPerSecond()), 1};
			const ERichCurveInterpMode InterpMode = (UsdStage.GetInterpolationType() == EUsdInterpolationType::Linear)
														? ERichCurveInterpMode::RCIM_Linear
														: ERichCurveInterpMode::RCIM_Constant;

			TArray<float> Weights;
			for (double TimeCode : TimeCodes)
			{
				bSuccess = AnimQuery.ComputeBlendShapeWeights(Weights, TimeCode);
				if (!bSuccess || !Weights.IsValidIndex(SkelAnimChannelIndex))
				{
					break;
				}

				int32 FrameNumber = FMath::FloorToInt(TimeCode);
				float SubFrameNumber = TimeCode - FrameNumber;

				FFrameTime FrameTime{FrameNumber, SubFrameNumber};
				double FrameTimeSeconds = static_cast<float>(StageFrameRate.AsSeconds(FrameTime));

				FKeyHandle Handle = Curve.AddKey(FrameTimeSeconds, Weights[SkelAnimChannelIndex]);
				Curve.SetKeyInterpMode(Handle, InterpMode);
			}
		}

		TArray<FString> SkelAnimChannels = AnimQuery.GetBlendShapeOrder();

		// Provide inbetween names/positions for this morph target payload
		TArray<UE::FUsdSkelInbetweenShape> Inbetweens = BlendShape.GetInbetweens();
		if (Inbetweens.Num() > 0)
		{
			// Let's store them into this temp struct so that we can sort them by weight first,
			// as Interchange seems to expect that given how it will pass these right along into
			// ResolveWeightsForBlendShape inside InterchangeAnimSequenceFactory.cpp
			struct FInbetweenAndPosition
			{
				FString Name;
				float Position;
			};
			TArray<FInbetweenAndPosition> ParsedInbetweens;
			ParsedInbetweens.Reset(Inbetweens.Num());

			for (const UE::FUsdSkelInbetweenShape& Inbetween : Inbetweens)
			{
				float Position = 0.5f;
				bool bSuccess = Inbetween.GetWeight(&Position);
				if (!bSuccess)
				{
					continue;
				}

				// Skip invalid positions. Note that technically positions outside the [0, 1] range seem to be allowed, but
				// they don't seem to work very well with our inbetween weights resolution function for some reason.
				// The legacy USD workflows have this exact same check though, so for consistency let's just do the same, and
				// if becomes an issue we should fix both
				if (Position > 1.0f || Position < 0.0f || FMath::IsNearlyZero(Position) || FMath::IsNearlyEqual(Position, 1.0f))
				{
					continue;
				}

				const FString MorphTargetName = BlendShapeName + TEXT("_") + Inbetween.GetAttr().GetName().ToString();
				FInbetweenAndPosition& NewEntry = ParsedInbetweens.Emplace_GetRef();
				NewEntry.Name = MorphTargetName;
				NewEntry.Position = Position;
			}

			ParsedInbetweens.Sort(
				[](const FInbetweenAndPosition& LHS, const FInbetweenAndPosition& RHS)
				{
					// It's invalid USD to author two inbetweens with the same weight, so let's ignore that case here.
					// (Reference: https://openusd.org/release/api/_usd_skel__schemas.html#UsdSkel_BlendShape)
					return LHS.Position < RHS.Position;
				}
			);

			OutPayloadData.InbetweenCurveNames.Reset(Inbetweens.Num() + 1);
			OutPayloadData.InbetweenFullWeights.Reset(Inbetweens.Num());

			// We add the main morph target curve name to InbetweenCurveNames too (having it end up one size bigger than
			// InbetweenFullWeights) as it seems like that's what Interchange expects. See CreateMorphTargetCurve within
			// InterchangeAnimSequenceFactory.cpp, and the very end of FFbxMesh::AddAllMeshes within FbxMesh.cpp
			OutPayloadData.InbetweenCurveNames.Add(BlendShapeName);

			for (const FInbetweenAndPosition& InbetweenAndPosition : ParsedInbetweens)
			{
				OutPayloadData.InbetweenCurveNames.Add(InbetweenAndPosition.Name);
				OutPayloadData.InbetweenFullWeights.Add(InbetweenAndPosition.Position);
			}
		}

		return true;
	}
#endif	  // USE_USD_SDK
}	 // namespace UE::InterchangeUsdTranslator::Private

UInterchangeUsdTranslatorSettings::UInterchangeUsdTranslatorSettings()
	: GeometryPurpose((int32)(EUsdPurpose::Default | EUsdPurpose::Proxy | EUsdPurpose::Render | EUsdPurpose::Guide))
	, RenderContext(UnrealIdentifiers::UniversalRenderContext)	  // Default to the universal render context for now as we don't support 'unreal' yet
	, MaterialPurpose(*UnrealIdentifiers::MaterialPreviewPurpose)
	, InterpolationType(EUsdInterpolationType::Linear)
	, bOverrideStageOptions(false)
	, StageOptions{
		  0.01f,			   // MetersPerUnit
		  EUsdUpAxis::ZAxis	   // UpAxis
	  }
{
}

UInterchangeUSDTranslator::UInterchangeUSDTranslator()
	: Impl(MakeUnique<UE::InterchangeUsdTranslator::Private::UInterchangeUSDTranslatorImpl>())
{
}

EInterchangeTranslatorType UInterchangeUSDTranslator::GetTranslatorType() const
{
	return GInterchangeEnableUSDLevelImport ? EInterchangeTranslatorType::Scenes : EInterchangeTranslatorType::Assets;
}

EInterchangeTranslatorAssetType UInterchangeUSDTranslator::GetSupportedAssetTypes() const
{
	return EInterchangeTranslatorAssetType::Materials | EInterchangeTranslatorAssetType::Meshes | EInterchangeTranslatorAssetType::Animations;
}

TArray<FString> UInterchangeUSDTranslator::GetSupportedFormats() const
{
	TArray<FString> Extensions;
	if (GInterchangeEnableUSDImport)
	{
		UnrealUSDWrapper::AddUsdImportFileFormatDescriptions(Extensions);
	}
	return Extensions;
}

bool UInterchangeUSDTranslator::Translate(UInterchangeBaseNodeContainer& NodeContainer) const
{
#if USE_USD_SDK
	using namespace UE;
	using namespace UE::InterchangeUsdTranslator::Private;
	using namespace UsdToUnreal;

	UInterchangeUSDTranslatorImpl* ImplPtr = Impl.Get();
	if (!ImplPtr)
	{
		return false;
	}
	ImplPtr->CurrentTrackSet = nullptr;

	UInterchangeUsdTranslatorSettings* Settings = Cast<UInterchangeUsdTranslatorSettings>(GetSettings());
	if (!Settings)
	{
		return false;
	}

	FString FilePath = GetSourceData()->GetFilename();
	if (!FPaths::FileExists(FilePath))
	{
		return false;
	}

	// Import should always feel like it's directly from disk, so we ignore already loaded layers and stage cache
	const bool bUseStageCache = false;
	const bool bForceReloadLayersFromDisk = true;
	ImplPtr->UsdStage = UnrealUSDWrapper::OpenStage(*FilePath, EUsdInitialLoadSet::LoadAll, bUseStageCache, bForceReloadLayersFromDisk);
	if (!ImplPtr->UsdStage)
	{
		return false;
	}

	// Apply stage settings
	if (Settings)
	{
		// Apply coordinate system conversion to the stage if we have one
		if (Settings->bOverrideStageOptions)
		{
			UsdUtils::SetUsdStageMetersPerUnit(ImplPtr->UsdStage, Settings->StageOptions.MetersPerUnit);
			UsdUtils::SetUsdStageUpAxis(ImplPtr->UsdStage, Settings->StageOptions.UpAxis);
		}

		ImplPtr->UsdStage.SetInterpolationType(Settings->InterpolationType);
	}

	// Cache these so we don't have to keep converting these tokens over and over during translation
	FUsdMeshConversionOptions& MeshOptions = ImplPtr->CachedMeshConversionOptions;
	MeshOptions.PurposesToLoad = (EUsdPurpose)Settings->GeometryPurpose;

	// TODO: Change FUsdMeshConversionOptions to not hold USD types directly, so we don't have to the conversion below everywhere.
	// We can't use UsdToUnreal::ConvertToken() here because it returns a TUsdStore, and the template instantiation created in this module doesn't
	// really do anything anyway as the module doesn't use IMPLEMENT_MODULE_USD!
	// Luckily we can get around this here because pxr::TfToken doesn't allocate on its own: At most USD makes a copy of the string, which it should
	// allocate/deallocate on its own allocator.
	MeshOptions.RenderContext = Settings->RenderContext == UnrealIdentifiers::UniversalRenderContext
									? pxr::UsdShadeTokens->universalRenderContext
									: pxr::TfToken{TCHAR_TO_ANSI(*Settings->RenderContext.ToString())};
	MeshOptions.MaterialPurpose = Settings->MaterialPurpose.IsNone() ? pxr::UsdShadeTokens->allPurpose
																	 : pxr::TfToken{TCHAR_TO_ANSI(*Settings->MaterialPurpose.ToString())};

	// Traverse stage and emit translated nodes
	FTraversalInfo Info;
	for (const FUsdPrim& Prim : ImplPtr->UsdStage.GetPseudoRoot().GetChildren())
	{
		Traverse(Prim, *ImplPtr, NodeContainer, TranslatorSettings, Info);
	}

	return true;
#else
	return false;
#endif	  // USE_USD_SDK
}

void UInterchangeUSDTranslator::ReleaseSource()
{
	UE::InterchangeUsdTranslator::Private::UInterchangeUSDTranslatorImpl* ImplPtr = Impl.Get();
	if (!ImplPtr)
	{
		return;
	}

	ImplPtr->UsdStage = UE::FUsdStage{};
	ImplPtr->CurrentTrackSet = nullptr;

	if (TranslatorSettings)
	{
		TranslatorSettings->ClearFlags(RF_Standalone);
		TranslatorSettings = nullptr;
	}
}

UInterchangeTranslatorSettings* UInterchangeUSDTranslator::GetSettings() const
{
	using namespace UE::InterchangeUsdTranslator::Private;

	UInterchangeUSDTranslatorImpl* ImplPtr = Impl.Get();
	if (!ImplPtr)
	{
		return nullptr;
	}

	if (!TranslatorSettings)
	{
		TranslatorSettings = DuplicateObject<UInterchangeUsdTranslatorSettings>(
			UInterchangeUsdTranslatorSettings::StaticClass()->GetDefaultObject<UInterchangeUsdTranslatorSettings>(),
			GetTransientPackage()
		);
		TranslatorSettings->LoadSettings();
		TranslatorSettings->ClearFlags(RF_ArchetypeObject);
		TranslatorSettings->SetFlags(RF_Standalone);
		TranslatorSettings->ClearInternalFlags(EInternalObjectFlags::Async);
	}
	return TranslatorSettings;
}

void UInterchangeUSDTranslator::SetSettings(const UInterchangeTranslatorSettings* InterchangeTranslatorSettings)
{
	using namespace UE::InterchangeUsdTranslator::Private;

	UInterchangeUSDTranslatorImpl* ImplPtr = Impl.Get();
	if (!ImplPtr)
	{
		return;
	}

	if (TranslatorSettings)
	{
		TranslatorSettings->ClearFlags(RF_Standalone);
		TranslatorSettings->ClearInternalFlags(EInternalObjectFlags::Async);
		TranslatorSettings = nullptr;
	}
	if (const UInterchangeUsdTranslatorSettings* USDTranslatorSettings = Cast<UInterchangeUsdTranslatorSettings>(InterchangeTranslatorSettings))
	{
		TranslatorSettings = DuplicateObject<UInterchangeUsdTranslatorSettings>(USDTranslatorSettings, GetTransientPackage());
		TranslatorSettings->ClearInternalFlags(EInternalObjectFlags::Async);
		TranslatorSettings->SetFlags(RF_Standalone);
	}
}

TOptional<UE::Interchange::FMeshPayloadData> UInterchangeUSDTranslator::GetMeshPayloadData(
	const FInterchangeMeshPayLoadKey& PayloadKey,
	const FTransform& MeshGlobalTransform
) const
{
	using namespace UE::InterchangeUsdTranslator::Private;
	bool bSuccess = false;
	TOptional<UE::Interchange::FMeshPayloadData> Result;
#if USE_USD_SDK
	UInterchangeUSDTranslatorImpl* ImplPtr = Impl.Get();
	if (!ImplPtr)
	{
		return Result;
	}

	UsdToUnreal::FUsdMeshConversionOptions OptionsCopy = ImplPtr->CachedMeshConversionOptions;
	OptionsCopy.AdditionalTransform = MeshGlobalTransform;

	UE::Interchange::FMeshPayloadData MeshPayloadData;
	switch (PayloadKey.Type)
	{
		case EInterchangeMeshPayLoadType::STATIC:
		{
			bSuccess = UE::InterchangeUsdTranslator::Private::GetStaticMeshPayloadData(
				PayloadKey.UniqueId,
				*ImplPtr,
				OptionsCopy,
				MeshPayloadData.MeshDescription
			);
			break;
		}
		case EInterchangeMeshPayLoadType::SKELETAL:
		{
			bSuccess = UE::InterchangeUsdTranslator::Private::GetSkeletalMeshPayloadData(
				PayloadKey.UniqueId,
				*ImplPtr,
				OptionsCopy,
				MeshPayloadData.MeshDescription,
				MeshPayloadData.JointNames
			);
			break;
		}
		case EInterchangeMeshPayLoadType::MORPHTARGET:
		{
			bSuccess = UE::InterchangeUsdTranslator::Private::GetMorphTargetPayloadData(
				PayloadKey.UniqueId,
				*ImplPtr,
				OptionsCopy,
				MeshPayloadData.MeshDescription,
				MeshPayloadData.MorphTargetName
			);
			break;
		}
		case EInterchangeMeshPayLoadType::NONE:	   // Fallthrough
		default:
			break;
	}

	if (bSuccess)
	{
		Result.Emplace(MeshPayloadData);
	}
#endif	  // USE_USD_SDK
	return Result;
}

TOptional<UE::Interchange::FImportImage> UInterchangeUSDTranslator::GetTexturePayloadData(
	const FString& PayloadKey,
	TOptional<FString>& AlternateTexturePath
) const
{
	using namespace UE::InterchangeUsdTranslator::Private;

	TOptional<UE::Interchange::FImportImage> TexturePayloadData;

#if USE_USD_SDK
	FString FilePath;
	TextureGroup TextureGroup;
	bool bDecoded = DecodeTexturePayloadKey(PayloadKey, FilePath, TextureGroup);
	if (bDecoded)
	{
		// Defer back to another translator to actually parse the texture raw data
		UE::Interchange::Private::FScopedTranslator ScopedTranslator(FilePath, Results);
		const IInterchangeTexturePayloadInterface* TextureTranslator = ScopedTranslator.GetPayLoadInterface<IInterchangeTexturePayloadInterface>();
		if (ensure(TextureTranslator))
		{
			AlternateTexturePath = FilePath;

			// The texture translators don't use the payload key, and read the texture directly from the SourceData's file path
			const FString UnusedPayloadKey = {};
			TexturePayloadData = TextureTranslator->GetTexturePayloadData(UnusedPayloadKey, AlternateTexturePath);

			// Move compression settings onto the payload data.
			// Note: We don't author anything else on the texture payload data here (like the sRGB flag), because those
			// settings were already on our translated node, and presumably already made their way to the factory node.
			// The factory should use them to override whatever it finds in this payload data, with the exception of the
			// compression settings (which can't be stored on the translated node)
			TexturePayloadData->CompressionSettings = TextureGroup == TEXTUREGROUP_WorldNormalMap ? TC_Normalmap : TC_Default;
		}
	}
#endif	  // USE_USD_SDK

	// We did not find a suitable Payload in USD Translator, let's find one in one of the Translators (MaterialX for the moment)
	// The best way would be to have a direct association between the payload and the right Translator, but we don't have a suitable way of knowing
	// which Payload belongs to which Translator So let's just loop over them all
	for (const TPair<FString, UInterchangeTranslatorBase*>& Pair : Impl->Translators)
	{
		if (IInterchangeTexturePayloadInterface* TexturePayloadInterface = Cast<IInterchangeTexturePayloadInterface>(Pair.Value))
		{
			TexturePayloadData = TexturePayloadInterface->GetTexturePayloadData(PayloadKey, AlternateTexturePath);
			if (TexturePayloadData)
			{
				break;
			}
		}
	}

	return TexturePayloadData;
}

TOptional<UE::Interchange::FImportBlockedImage> UInterchangeUSDTranslator::GetBlockedTexturePayloadData(
	const FString& PayloadKey,
	TOptional<FString>& AlternateTexturePath
) const
{
	using namespace UE::InterchangeUsdTranslator::Private;

	UE::Interchange::FImportBlockedImage BlockData;

#if USE_USD_SDK
	FString FilePath;
	TextureGroup TextureGroup;
	bool bDecoded = DecodeTexturePayloadKey(PayloadKey, FilePath, TextureGroup);
	if (!bDecoded)
	{
		return {};
	}

	AlternateTexturePath = FilePath;

	// Collect all the UDIM tile filepaths similar to this current tile. If we've been asked to translate
	// a blocked texture then we must have some
	TMap<int32, FString> TileIndexToPath = UE::TextureUtilitiesCommon::GetUDIMBlocksFromSourceFile(
		FilePath,
		UE::TextureUtilitiesCommon::DefaultUdimRegexPattern
	);
	if (!ensure(TileIndexToPath.Num() > 0))
	{
		return {};
	}

	bool bInitializedBlockData = false;

	TArray<UE::Interchange::FImportImage> TileImages;
	TileImages.Reserve(TileIndexToPath.Num());

	for (const TPair<int32, FString>& TileIndexAndPath : TileIndexToPath)
	{
		int32 UdimTile = TileIndexAndPath.Key;
		const FString& TileFilePath = TileIndexAndPath.Value;

		int32 BlockX = INDEX_NONE;
		int32 BlockY = INDEX_NONE;
		UE::TextureUtilitiesCommon::ExtractUDIMCoordinates(UdimTile, BlockX, BlockY);
		if (BlockX == INDEX_NONE || BlockY == INDEX_NONE)
		{
			continue;
		}

		// Find another translator that actually supports that filetype to handle the texture
		UE::Interchange::Private::FScopedTranslator ScopedTranslator(TileFilePath, Results);
		const IInterchangeTexturePayloadInterface* TextureTranslator = ScopedTranslator.GetPayLoadInterface<IInterchangeTexturePayloadInterface>();
		if (!ensure(TextureTranslator))
		{
			continue;
		}

		// Invoke the translator to actually load the texture and parse it
		const FString UnusedPayloadKey = {};
		TOptional<UE::Interchange::FImportImage> TexturePayloadData;
		TexturePayloadData = TextureTranslator->GetTexturePayloadData(UnusedPayloadKey, AlternateTexturePath);
		if (!TexturePayloadData)
		{
			continue;
		}
		const UE::Interchange::FImportImage& Image = TileImages.Emplace_GetRef(MoveTemp(TexturePayloadData.GetValue()));
		TexturePayloadData.Reset();

		// Initialize the settings on the BlockData itself based on the first image we parse
		if (!bInitializedBlockData)
		{
			bInitializedBlockData = true;

			BlockData.Format = Image.Format;
			BlockData.CompressionSettings = TextureGroup == TEXTUREGROUP_WorldNormalMap ? TC_Normalmap : TC_Default;
			BlockData.bSRGB = Image.bSRGB;
			BlockData.MipGenSettings = Image.MipGenSettings;
		}

		// Prepare the BlockData to receive this image data (later)
		BlockData.InitBlockFromImage(BlockX, BlockY, Image);
	}

	// Move all of the FImportImage buffers into the BlockData itself
	BlockData.MigrateDataFromImagesToRawData(TileImages);
#endif	  // USE_USD_SDK

	return BlockData;
}

TArray<UE::Interchange::FAnimationPayloadData> UInterchangeUSDTranslator::GetAnimationPayloadData(
	const TArray<UE::Interchange::FAnimationPayloadQuery>& PayloadQueries
) const
{
	using namespace UE::Interchange;
	using namespace UE::InterchangeUsdTranslator::Private;
	// This is the results we return
	TArray<UE::Interchange::FAnimationPayloadData> AnimationPayloads;

	// Maps to help sorting the queries by payload type
	TArray<int32> BakeQueryIndexes;
	TArray<TArray<UE::Interchange::FAnimationPayloadData>> BakeAnimationPayloads;
	TArray<int32> CurveQueryIndexes;
	TArray<TArray<UE::Interchange::FAnimationPayloadData>> CurveAnimationPayloads;

	// Get all curves with a parallel for
	int32 PayloadCount = PayloadQueries.Num();
	for (int32 PayloadIndex = 0; PayloadIndex < PayloadCount; ++PayloadIndex)
	{
		const UE::Interchange::FAnimationPayloadQuery& PayloadQuery = PayloadQueries[PayloadIndex];
		EInterchangeAnimationPayLoadType QueryType = PayloadQuery.PayloadKey.Type;
		if (QueryType == EInterchangeAnimationPayLoadType::BAKED)
		{
			BakeQueryIndexes.Add(PayloadIndex);
		}
		else
		{
			CurveQueryIndexes.Add(PayloadIndex);
		}
	}

#if USE_USD_SDK

	// Import the Baked curve payloads
	if (BakeQueryIndexes.Num() > 0)
	{
		int32 BakePayloadCount = BakeQueryIndexes.Num();
		TMap<FString, TArray<const UE::Interchange::FAnimationPayloadQuery*>> BatchedBakeQueries;
		BatchedBakeQueries.Reserve(BakePayloadCount);

		// Get the BAKED transform synchronously, since there is some interchange task that parallel them
		for (int32 BakePayloadIndex = 0; BakePayloadIndex < BakePayloadCount; ++BakePayloadIndex)
		{
			if (!ensure(BakeQueryIndexes.IsValidIndex(BakePayloadIndex)))
			{
				continue;
			}
			int32 PayloadIndex = BakeQueryIndexes[BakePayloadIndex];
			if (!PayloadQueries.IsValidIndex(PayloadIndex))
			{
				continue;
			}
			const UE::Interchange::FAnimationPayloadQuery& PayloadQuery = PayloadQueries[PayloadIndex];
			check(PayloadQuery.PayloadKey.Type == EInterchangeAnimationPayLoadType::BAKED);
			// Joint transform animation queries.
			//
			// Currently we'll receive the PayloadQueries for all joints of a skeletal animation on the same GetAnimationPayloadData
			// call. Unfortunately in USD we must compute all joint transforms every time, even if all we need is data for a single
			// joint. For efficiency then, we group up all the queries for the separate joints of the same skeleton into one batch
			// task that we can resolve in one pass
			const FString BakedQueryHash = HashAnimPayloadQuery(PayloadQuery);
			TArray<const UE::Interchange::FAnimationPayloadQuery*>& Queries = BatchedBakeQueries.FindOrAdd(BakedQueryHash);
			Queries.Add(&PayloadQuery);
		}
		// Emit the batched joint transform animation tasks
		for (const TPair<FString, TArray<const UE::Interchange::FAnimationPayloadQuery*>>& BatchedBakedQueryPair : BatchedBakeQueries)
		{
			const TArray<const UE::Interchange::FAnimationPayloadQuery*>& Queries = BatchedBakedQueryPair.Value;
			TArray<UE::Interchange::FAnimationPayloadData> Result;
			GetJointAnimationCurvePayloadData(*Impl, Queries, Result);
			BakeAnimationPayloads.Add(Result);
		}

		// Append the bake curves results
		for (TArray<UE::Interchange::FAnimationPayloadData>& AnimationPayload : BakeAnimationPayloads)
		{
			AnimationPayloads.Append(AnimationPayload);
		}
	}

	// Import normal curves
	if (CurveQueryIndexes.Num() > 0)
	{
		auto GetAnimPayloadLambda = [this, &PayloadQueries, &CurveAnimationPayloads](int32 PayloadIndex)
		{
			if (!PayloadQueries.IsValidIndex(PayloadIndex))
			{
				return;
			}
			const UE::Interchange::FAnimationPayloadQuery& PayloadQuery = PayloadQueries[PayloadIndex];
			EInterchangeAnimationPayLoadType PayloadType = PayloadQuery.PayloadKey.Type;
			if (PayloadType == EInterchangeAnimationPayLoadType::CURVE || PayloadType == EInterchangeAnimationPayLoadType::STEPCURVE)
			{
				// Property track animation queries.
				//
				// We're fine handling these in isolation (currently GetAnimationPayloadData is called with
				// a single query at a time for these): Emit a separate task for each right away
				FAnimationPayloadData AnimationPayLoadData{PayloadQuery.SceneNodeUniqueID, PayloadQuery.PayloadKey};
				if (GetPropertyAnimationCurvePayloadData(Impl->UsdStage, PayloadQuery.PayloadKey.UniqueId, AnimationPayLoadData))
				{
					CurveAnimationPayloads[PayloadIndex].Emplace(AnimationPayLoadData);
				}
			}
			else if (PayloadType == EInterchangeAnimationPayLoadType::MORPHTARGETCURVE)
			{
				// Morph target curve queries.
				FAnimationPayloadData AnimationPayLoadData{PayloadQuery.SceneNodeUniqueID, PayloadQuery.PayloadKey};
				if (GetMorphTargetAnimationCurvePayloadData(*Impl, PayloadQuery.PayloadKey.UniqueId, AnimationPayLoadData))
				{
					CurveAnimationPayloads[PayloadIndex].Emplace(AnimationPayLoadData);
				}
			}
		};

		// Get all curves with a parallel for if there is many
		int32 CurvePayloadCount = CurveQueryIndexes.Num();
		CurveAnimationPayloads.AddDefaulted(CurvePayloadCount);
		const int32 BatchSize = 10;
		if (CurvePayloadCount > BatchSize)
		{
			const int32 NumBatches = (CurvePayloadCount / BatchSize) + 1;
			ParallelFor(
				NumBatches,
				[&CurveQueryIndexes, &GetAnimPayloadLambda](int32 BatchIndex)
				{
					int32 PayloadIndexOffset = BatchIndex * BatchSize;
					for (int32 PayloadIndex = PayloadIndexOffset; PayloadIndex < PayloadIndexOffset + BatchSize; ++PayloadIndex)
					{
						// The last batch can be incomplete
						if (!CurveQueryIndexes.IsValidIndex(PayloadIndex))
						{
							break;
						}
						GetAnimPayloadLambda(CurveQueryIndexes[PayloadIndex]);
					}
				},
				EParallelForFlags::BackgroundPriority	 // ParallelFor
			);
		}
		else
		{
			for (int32 PayloadIndex = 0; PayloadIndex < CurvePayloadCount; ++PayloadIndex)
			{
				int32 PayloadQueriesIndex = CurveQueryIndexes[PayloadIndex];
				if (PayloadQueries.IsValidIndex(PayloadQueriesIndex))
				{
					GetAnimPayloadLambda(PayloadQueriesIndex);
				}
			}
		}

		// Append the curves results
		for (TArray<UE::Interchange::FAnimationPayloadData>& AnimationPayload : CurveAnimationPayloads)
		{
			AnimationPayloads.Append(AnimationPayload);
		}
	}
#endif	  // USE_USD_SDK

	return AnimationPayloads;
}

#undef LOCTEXT_NAMESPACE
