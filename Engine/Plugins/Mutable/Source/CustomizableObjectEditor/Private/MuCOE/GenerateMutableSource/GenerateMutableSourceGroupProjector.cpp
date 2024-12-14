// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/GenerateMutableSource/GenerateMutableSourceGroupProjector.h"

#include "GenerateMutableSourceImage.h"
#include "Materials/MaterialInterface.h"
#include "MuCO/CustomizableObjectInstancePrivate.h"
#include "MuCO/MultilayerProjector.h"
#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceProjector.h"
#include "MuCOE/Nodes/CustomizableObjectNodeAnimationPose.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierExtendMeshSection.h"
#include "MuCOE/Nodes/CustomizableObjectNodeGroupProjectorParameter.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObjectGroup.h"
#include "MuT/NodeColourConstant.h"
#include "MuT/NodeColourFromScalars.h"
#include "MuT/NodeImageLayerColour.h"
#include "MuT/NodeImageMultiLayer.h"
#include "MuT/NodeImagePlainColour.h"
#include "MuT/NodeImageProject.h"
#include "MuT/NodeImageResize.h"
#include "MuT/NodeImageSwitch.h"
#include "MuT/NodeImageSwizzle.h"
#include "MuT/NodeMeshSwitch.h"
#include "MuT/NodeRangeFromScalar.h"
#include "MuT/NodeScalarConstant.h"

class UPoseAsset;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


mu::NodeImagePtr GenerateMutableSourceGroupProjector(const int32 NodeLOD, const int32 ImageIndex, mu::NodeMeshPtr MeshNode, FMutableGraphGenerationContext& GenerationContext,
	UCustomizableObjectNodeMaterialBase* TypedNodeMat, UCustomizableObjectNodeModifierExtendMeshSection* TypedNodeExt, bool& bShareProjectionTexturesBetweenLODs, bool& bIsGroupProjectorImage,
	UTexture2D*& GroupProjectionReferenceTexture, TMap<FString, float>& TextureNameToProjectionResFactor, FString& AlternateResStateName)
{
	check(static_cast<bool>(TypedNodeMat) != static_cast<bool>(TypedNodeExt)); // XOr. TypedNodeMat valid or TypedNodeExt valid. At least one valid.

	if (!MeshNode.get())
	{
		return mu::NodeImagePtr();
	}

	TArray<mu::Ptr<mu::NodeImageProject>> ImageNodes;
	TArray<FGroupProjectorTempData> ImageNodes_ProjectorTempData;

	int32 TextureSize = 512;
	
	for (TArray<UCustomizableObjectNodeGroupProjectorParameter*> GroupProjectors : GenerationContext.CurrentGroupProjectors)
	{
		for (UCustomizableObjectNodeGroupProjectorParameter* ProjParamNode : GroupProjectors)
		{
			TOptional<FGroupProjectorTempData> OptionalProjectorTempData = GenerateMutableGroupProjector(ProjParamNode, GenerationContext);
			if (!OptionalProjectorTempData.IsSet())
			{
				continue;
			}

			FGroupProjectorTempData& ProjectorTempData = OptionalProjectorTempData.GetValue();
				
			const int32& DropProjectionTextureAtLOD = ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->DropProjectionTextureAtLOD;
			if (DropProjectionTextureAtLOD >= 0 && NodeLOD >= DropProjectionTextureAtLOD)
			{
				continue;
			}

			bShareProjectionTexturesBetweenLODs |= ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->bShareProjectionTexturesBetweenLODs;

			if (!GroupProjectionReferenceTexture)
			{
				GroupProjectionReferenceTexture = ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->ReferenceTexture;
				
				if (GroupProjectionReferenceTexture)
				{
					GenerationContext.AddParticipatingObject(*GroupProjectionReferenceTexture);
				}
			}

			const bool bProjectToImage = [&]
			{
				FString ParameterName;
				
				if (TypedNodeMat)
				{
					ParameterName = TypedNodeMat->GetParameterName(EMaterialParameterType::Texture, ImageIndex).ToString();
				}
				else
				{
					const FNodeMaterialParameterId ImageId = TypedNodeExt->GetParameterId(EMaterialParameterType::Texture, ImageIndex);
					return TypedNodeExt->UsesImage(ImageId);
				}

				return ParameterName == ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->MaterialChannelNameToConnect;
			}();
			
			if (bProjectToImage)
			{
				const bool bWarningReplacedImage = [&]
				{
					if (TypedNodeMat)
					{
						return TypedNodeMat->IsImageMutableMode(ImageIndex);
					}
					else
					{
						const FNodeMaterialParameterId ImageId = TypedNodeExt->GetParameterId(EMaterialParameterType::Texture, ImageIndex);
						return TypedNodeExt->UsesImage(ImageId);
					}
				}();
				
				if (bWarningReplacedImage)
				{
					const FString ImageName = [&]
					{
						if (TypedNodeMat)
						{
							return TypedNodeMat->GetParameterName(EMaterialParameterType::Texture, ImageIndex).ToString();
						}
						else
						{
							return TypedNodeExt->GetParameterName(EMaterialParameterType::Texture, ImageIndex).ToString();
						}
					}();
					
					FString msg = FString::Printf(TEXT("Material image [%s] is connected to an image but will be replaced by a Group Projector."), *ImageName);
					GenerationContext.Log(FText::FromString(msg), TypedNodeMat);
					continue;
				}
				
				mu::Ptr<mu::NodeImageProject> ImageNode = new mu::NodeImageProject();
				bIsGroupProjectorImage = true;
				ImageNode->SetLayout(ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->UVLayout);

				{
					if (TypedNodeMat &&
						!ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->AlternateProjectionResolutionStateName.IsEmpty()
						&& ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->AlternateProjectionResolutionFactor > 0)
					{
						TextureNameToProjectionResFactor.Add(
							TypedNodeMat->GetParameterName(EMaterialParameterType::Texture, ImageIndex).ToString(),
							ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->AlternateProjectionResolutionFactor);

						if (!AlternateResStateName.IsEmpty() &&
							AlternateResStateName != ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->AlternateProjectionResolutionStateName &&
							!ProjectorTempData.bAlternateResStateNameWarningDisplayed)
						{
							FString msg = FString::Printf(TEXT("All 'Alternate Projection Resolution State Name' properties in Group Projector Parameter nodes connected to same Group node must have the same value or be blank. Only the value of the last connected node will be used."));
							GenerationContext.Log(FText::FromString(msg), ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter);
							ProjectorTempData.bAlternateResStateNameWarningDisplayed = true;
						}

						AlternateResStateName = ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->AlternateProjectionResolutionStateName;
					}
				}

				{
					mu::NodeScalarConstantPtr ScalarNode = new mu::NodeScalarConstant;
					ScalarNode->SetValue(120.f);
					ImageNode->SetAngleFadeStart(ScalarNode);
				}

				{
					mu::NodeScalarConstantPtr ScalarNode = new mu::NodeScalarConstant;
					ScalarNode->SetValue(150.f);
					ImageNode->SetAngleFadeEnd(ScalarNode);
				}

				mu::NodeMeshSwitchPtr MeshSwitchNode = new mu::NodeMeshSwitch;
				MeshSwitchNode->SetParameter(ProjectorTempData.PoseOptionsParameter);
				MeshSwitchNode->SetOptionCount(ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->OptionPoses.Num() + 1);
				MeshSwitchNode->SetOption(0, MeshNode);

				for (int32 SelectorIndex = 0; SelectorIndex < ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->OptionPoses.Num(); ++SelectorIndex)
				{
					if (ProjectorTempData.CustomizableObjectNodeGroupProjectorParameter->OptionPoses[SelectorIndex].OptionPose)
					{
						mu::NodeMeshApplyPosePtr NodeMeshApplyPose = CreateNodeMeshApplyPose(GenerationContext, MeshNode,
							ProjectorTempData.PoseBoneDataArray[SelectorIndex].ArrayBoneName, ProjectorTempData.PoseBoneDataArray[SelectorIndex].ArrayTransform);

						if (!NodeMeshApplyPose)
						{
							FString msg = FString::Printf(TEXT("Couldn't get bone transform information from a Pose Asset."));
							GenerationContext.Log(FText::FromString(msg), TypedNodeMat);
						}

						MeshSwitchNode->SetOption(SelectorIndex + 1, NodeMeshApplyPose);
					}
					else
					{
						MeshSwitchNode->SetOption(SelectorIndex + 1, MeshNode);
					}
				}

				ImageNode->SetMesh(MeshSwitchNode);
				ImageNode->SetProjector(ProjectorTempData.NodeProjectorParameterPtr);
				ImageNode->SetImage(ProjectorTempData.NodeImagePtr);

				TextureSize = ProjectorTempData.TextureSize;
				ImageNode->SetImageSize( FUintVector2(TextureSize, TextureSize) );

				ImageNodes.Add(ImageNode);
				ImageNodes_ProjectorTempData.Add(ProjectorTempData);
			}
		}
	}
	
	if (ImageNodes.Num() == 0)
	{
		return mu::NodeImagePtr();
	}
	
	mu::Ptr<mu::NodeColourConstant> ZeroColorNode = new mu::NodeColourConstant();
	ZeroColorNode->Value = FVector4f(0.f, 0.f, 0.f, 1.0f);

	mu::NodeImagePlainColourPtr ZeroPlainColourNode = new mu::NodeImagePlainColour;
	ZeroPlainColourNode->SetSize(TextureSize, TextureSize);
	ZeroPlainColourNode->SetColour(ZeroColorNode);

	mu::NodeImageSwizzlePtr ZeroChannelNode = new mu::NodeImageSwizzle;
	ZeroChannelNode->SetFormat(mu::EImageFormat::IF_L_UBYTE);
	ZeroChannelNode->SetSource(0, ZeroPlainColourNode);
	ZeroChannelNode->SetSourceChannel(0, 2); // Just take a zeroed channel for the base alpha

	mu::NodeScalarConstantPtr OneConstantNode = new mu::NodeScalarConstant;
	OneConstantNode->SetValue(1.f);

	mu::NodeImagePtr ResultAlpha = ZeroChannelNode;
	mu::NodeImagePtr ResultImage = ZeroPlainColourNode;

	for (int32 i = 0; i < ImageNodes.Num(); ++i)
	{
		if (i > 0) // Resize the projection texture if necessary after the first iteration
		{
			int32 NewTextureSize = ImageNodes_ProjectorTempData.IsValidIndex(i) ? ImageNodes_ProjectorTempData[i].CustomizableObjectNodeGroupProjectorParameter->ProjectionTextureSize : TextureSize;

			if (NewTextureSize <= 0 || !FMath::IsPowerOfTwo(NewTextureSize))
			{
				NewTextureSize = TextureSize;
			}

			if (NewTextureSize != TextureSize)
			{
				TextureSize = NewTextureSize;

				ZeroPlainColourNode = new mu::NodeImagePlainColour;
				ZeroPlainColourNode->SetSize(TextureSize, TextureSize);
				ZeroPlainColourNode->SetColour(ZeroColorNode);
			}
		}

		mu::NodeImageSwizzlePtr ImageNodesAlphaChannelNode = new mu::NodeImageSwizzle;
		ImageNodesAlphaChannelNode->SetFormat(mu::EImageFormat::IF_L_UBYTE);
		ImageNodesAlphaChannelNode->SetSource(0, ImageNodes[i]);
		ImageNodesAlphaChannelNode->SetSourceChannel(0, 3);

		mu::Ptr<mu::NodeColourFromScalars> ColourFromScalars = new mu::NodeColourFromScalars;
		ColourFromScalars->X = ImageNodes_ProjectorTempData[i].NodeOpacityParameter;
		ColourFromScalars->Y = ImageNodes_ProjectorTempData[i].NodeOpacityParameter;
		ColourFromScalars->Z = ImageNodes_ProjectorTempData[i].NodeOpacityParameter;
		ColourFromScalars->W = OneConstantNode;

		mu::NodeImageLayerColourPtr OpacityMultiLayerNode = new mu::NodeImageLayerColour;
		OpacityMultiLayerNode->SetBlendType(mu::EBlendType::BT_MULTIPLY);
		OpacityMultiLayerNode->SetColour(ColourFromScalars);
		OpacityMultiLayerNode->SetBase(ImageNodesAlphaChannelNode);
		//OpacityMultiLayerNode->SetMask(OneChannelNode); // No mask needed

		mu::NodeImageSwizzlePtr MultiplySwizzleNode = new mu::NodeImageSwizzle;
		MultiplySwizzleNode->SetFormat(mu::EImageFormat::IF_L_UBYTE);
		MultiplySwizzleNode->SetSource(0, OpacityMultiLayerNode);
		MultiplySwizzleNode->SetSourceChannel(0, 0);

		mu::NodeImageMultiLayerPtr BaseAlphaMultiLayerNode = new mu::NodeImageMultiLayer;
		BaseAlphaMultiLayerNode->SetRange(ImageNodes_ProjectorTempData[i].NodeRange);
		BaseAlphaMultiLayerNode->SetBlendType(mu::EBlendType::BT_LIGHTEN);
		BaseAlphaMultiLayerNode->SetBase(ResultAlpha);
		BaseAlphaMultiLayerNode->SetBlended(MultiplySwizzleNode);
		//BaseAlphaMultiLayerNode->SetMask(MultiplySwizzleNode); // No mask needed
		ResultAlpha = BaseAlphaMultiLayerNode;

		mu::NodeImageMultiLayerPtr BaseMultiLayerNode = new mu::NodeImageMultiLayer;
		BaseMultiLayerNode->SetRange(ImageNodes_ProjectorTempData[i].NodeRange);
		BaseMultiLayerNode->SetBlendType(mu::EBlendType::BT_BLEND);
		BaseMultiLayerNode->SetBase(ResultImage);
		BaseMultiLayerNode->SetBlended(ImageNodes[i]);
		BaseMultiLayerNode->SetMask(MultiplySwizzleNode);
		ResultImage = BaseMultiLayerNode;
	}

	mu::NodeImageSwizzlePtr SwizzleNodeR = new mu::NodeImageSwizzle;
	SwizzleNodeR->SetFormat(mu::EImageFormat::IF_L_UBYTE);
	SwizzleNodeR->SetSource(0, ResultImage);
	SwizzleNodeR->SetSourceChannel(0, 0);

	mu::NodeImageSwizzlePtr SwizzleNodeG = new mu::NodeImageSwizzle;
	SwizzleNodeG->SetFormat(mu::EImageFormat::IF_L_UBYTE);
	SwizzleNodeG->SetSource(0, ResultImage);
	SwizzleNodeG->SetSourceChannel(0, 1);

	mu::NodeImageSwizzlePtr SwizzleNodeB = new mu::NodeImageSwizzle;
	SwizzleNodeB->SetFormat(mu::EImageFormat::IF_L_UBYTE);
	SwizzleNodeB->SetSource(0, ResultImage);
	SwizzleNodeB->SetSourceChannel(0, 2);

	mu::NodeImageSwizzlePtr FinalSwizzleNode = new mu::NodeImageSwizzle;
	FinalSwizzleNode->SetFormat(mu::EImageFormat::IF_RGBA_UBYTE);
	FinalSwizzleNode->SetSource(0, SwizzleNodeR);
	FinalSwizzleNode->SetSourceChannel(0, 0);
	FinalSwizzleNode->SetSource(1, SwizzleNodeG);
	FinalSwizzleNode->SetSourceChannel(1, 0);
	FinalSwizzleNode->SetSource(2, SwizzleNodeB);
	FinalSwizzleNode->SetSourceChannel(2, 0);
	FinalSwizzleNode->SetSource(3, ResultAlpha);
	FinalSwizzleNode->SetSourceChannel(3, 0);

	return FinalSwizzleNode;
}


TOptional<FGroupProjectorTempData> GenerateMutableGroupProjector(UCustomizableObjectNodeGroupProjectorParameter* ProjParamNode, FMutableGraphGenerationContext& GenerationContext)
{
	FGeneratedGroupProjectorsKey Key;
	Key.Node = ProjParamNode;
	Key.CurrentComponent = GenerationContext.CurrentMeshComponent;
	
	if (FGroupProjectorTempData* Result = GenerationContext.GeneratedGroupProjectors.Find(Key))
	{
		return *Result;
	}

	FGroupProjectorTempData GroupProjectorTempData;
	
	// The static cast works here because it's already known to be a mu::NodeProjectorParameter* because of the UE5 Cast in the previous line
	GroupProjectorTempData.NodeProjectorParameterPtr = static_cast<mu::NodeProjectorParameter*>(GenerateMutableSourceProjector(&ProjParamNode->OutputPin(), GenerationContext).get());

	if (GroupProjectorTempData.NodeProjectorParameterPtr)
	{
		// Use the projector parameter uid + num to identify parameters derived from this node
		FGuid NumLayersParamUid = ProjParamNode->NodeGuid;
		NumLayersParamUid.D += 1;
		FGuid SelectedPoseParamUid = ProjParamNode->NodeGuid;
		SelectedPoseParamUid.D += 2;
		FGuid OpacityParamUid = ProjParamNode->NodeGuid;
		OpacityParamUid.D += 3;
		FGuid SelectedImageParamUid = ProjParamNode->NodeGuid;
		SelectedImageParamUid.D += 4;

		// Add to UCustomizableObjectNodeGroupProjectorParameter::OptionTextures those textures that are present in
		// UCustomizableObjectNodeGroupProjectorParameter::OptionTexturesDataTable avoiding any repeated element
		TArray<FGroupProjectorParameterImage> ArrayOptionTexture = ProjParamNode->GetFinalOptionTexturesNoRepeat();

		if ((ProjParamNode->OptionTexturesDataTable != nullptr) &&
			(ProjParamNode->DataTableTextureColumnName.ToString().IsEmpty() || (ProjParamNode->DataTableTextureColumnName.ToString() == "None")))
		{
			FString msg = FString::Printf(TEXT("The group projection node has a table assigned to the Option Images Data Table property, but no column to read textures is specified at the Data Table Texture Column Name property."));
			GenerationContext.Log(FText::FromString(msg), ProjParamNode, EMessageSeverity::Error, true);
		}

		GroupProjectorTempData.CustomizableObjectNodeGroupProjectorParameter = ProjParamNode;

		mu::NodeScalarParameterPtr NodeScalarParam;
		if (mu::Ptr<mu::NodeScalarParameter>* FindResult = GenerationContext.GeneratedScalarParameters.Find(NumLayersParamUid.ToString()))
		{
			NodeScalarParam = *FindResult;
		}
		else
		{
			NodeScalarParam = new mu::NodeScalarParameter;
			FString NodeScalarParamName = ProjParamNode->ParameterName + NUM_LAYERS_PARAMETER_POSTFIX;
			NodeScalarParam->SetName(NodeScalarParamName);
			NodeScalarParam->SetUid(NumLayersParamUid.ToString());
			NodeScalarParam->SetDefaultValue(0.f);

			GenerationContext.ParameterUIDataMap.Add(NodeScalarParamName,
		FMutableParameterData(ProjParamNode->ParamUIMetadata, EMutableParameterType::Int));
					
			GenerationContext.GeneratedScalarParameters.Add(NumLayersParamUid.ToString(), NodeScalarParam);
		}

		mu::Ptr<mu::NodeRangeFromScalar> NodeRangeFromScalar = new mu::NodeRangeFromScalar;
		NodeRangeFromScalar->SetSize(NodeScalarParam);
		GroupProjectorTempData.NodeRange = NodeRangeFromScalar;
		GroupProjectorTempData.NodeProjectorParameterPtr->SetRangeCount(1);
		GroupProjectorTempData.NodeProjectorParameterPtr->SetRange(0, NodeRangeFromScalar);
		
		mu::NodeScalarEnumParameterPtr PoseEnumParameterNode;
		if (mu::Ptr<mu::NodeScalarEnumParameter>* FindResult = GenerationContext.GeneratedEnumParameters.Find(SelectedPoseParamUid.ToString()))
		{
			PoseEnumParameterNode = *FindResult;
		}
		else
		{
			PoseEnumParameterNode = new mu::NodeScalarEnumParameter;
			FString PoseNodeEnumParamName = ProjParamNode->ParameterName + POSE_PARAMETER_POSTFIX;
			PoseEnumParameterNode->SetName(PoseNodeEnumParamName);
			PoseEnumParameterNode->SetUid(SelectedPoseParamUid.ToString());
			PoseEnumParameterNode->SetValueCount(ProjParamNode->OptionPoses.Num() + 1);
			PoseEnumParameterNode->SetDefaultValueIndex(0);
			GroupProjectorTempData.PoseOptionsParameter = PoseEnumParameterNode;

			GenerationContext.GeneratedEnumParameters.Add(SelectedPoseParamUid.ToString(), PoseEnumParameterNode);

			GenerationContext.ParameterUIDataMap.Add(PoseNodeEnumParamName,
		FMutableParameterData(ProjParamNode->ParamUIMetadata, EMutableParameterType::Int));
		}
		
		mu::NodeScalarParameterPtr OpacityParameterNode;
		if (mu::Ptr<mu::NodeScalarParameter>* FindResult = GenerationContext.GeneratedScalarParameters.Find(OpacityParamUid.ToString()))
		{
			OpacityParameterNode = *FindResult;
		}
		else
		{
			OpacityParameterNode = new mu::NodeScalarParameter;
			FString OpacityParameterNodeName = ProjParamNode->ParameterName + OPACITY_PARAMETER_POSTFIX;
			OpacityParameterNode->SetName(OpacityParameterNodeName);
			OpacityParameterNode->SetUid(OpacityParamUid.ToString());
			OpacityParameterNode->SetDefaultValue(0.75f);
			OpacityParameterNode->SetRangeCount(1);
			OpacityParameterNode->SetRange(0, NodeRangeFromScalar);

			GenerationContext.GeneratedScalarParameters.Add(OpacityParamUid.ToString(), OpacityParameterNode);
					
			FMutableParamUIMetadata OpacityMetadata = ProjParamNode->ParamUIMetadata;
			OpacityMetadata.ObjectFriendlyName = FString("Opacity");
			GroupProjectorTempData.NodeOpacityParameter = OpacityParameterNode;
					
			FMutableParameterData ParameterUIData;
			ParameterUIData.ParamUIMetadata = OpacityMetadata;
			ParameterUIData.Type = EMutableParameterType::Float;
				
			GenerationContext.ParameterUIDataMap.Add(OpacityParameterNodeName, ParameterUIData);
		}
		
		if (ArrayOptionTexture.Num() == 0)
		{
			FString msg = FString::Printf(TEXT("The group projection node must have at least one option image connected to a texture or at least one valid element in Option Images Data Table."));
			GenerationContext.Log(FText::FromString(msg), ProjParamNode, EMessageSeverity::Error, true);
			return {};
		}

		FMutableComponentInfo* CurrentComponentInfo = GenerationContext.GetCurrentComponentInfo();
		check(CurrentComponentInfo);
		
		PoseEnumParameterNode->SetValue(0, 0.f, "Default pose");

		for (int32 PoseIndex = 0; PoseIndex < ProjParamNode->OptionPoses.Num(); ++PoseIndex)
		{
			PoseEnumParameterNode->SetValue(PoseIndex + 1, (float)PoseIndex + 1.f, ProjParamNode->OptionPoses[PoseIndex].PoseName);

			TArray<FString> ArrayBoneName;
			TArray<FTransform> ArrayTransform;
			UPoseAsset* PoseAsset = ProjParamNode->OptionPoses[PoseIndex].OptionPose;

			if (PoseAsset == nullptr) // Check if the slot has a selected pose. Could be left empty by the user
			{
				FString msg = FString::Printf(TEXT("The group projection node must have a pose assigned on each Option Poses element."));
				GenerationContext.Log(FText::FromString(msg), ProjParamNode, EMessageSeverity::Error, true);
				return {};
			}

			check(GroupProjectorTempData.PoseBoneDataArray.Num() == PoseIndex);
			GroupProjectorTempData.PoseBoneDataArray.AddDefaulted(1);
			UCustomizableObjectNodeAnimationPose::StaticRetrievePoseInformation(PoseAsset, CurrentComponentInfo->RefSkeletalMesh,
				GroupProjectorTempData.PoseBoneDataArray[PoseIndex].ArrayBoneName, GroupProjectorTempData.PoseBoneDataArray[PoseIndex].ArrayTransform);
		}
	
		mu::NodeScalarEnumParameterPtr EnumParameterNode;
		if (mu::Ptr<mu::NodeScalarEnumParameter>* FindResult = GenerationContext.GeneratedEnumParameters.Find(SelectedImageParamUid.ToString()))
		{
			EnumParameterNode = *FindResult;
		}
		else
		{
			EnumParameterNode = new mu::NodeScalarEnumParameter;

			FString NodeEnumParamName = ProjParamNode->ParameterName + IMAGE_PARAMETER_POSTFIX;
			EnumParameterNode->SetName(NodeEnumParamName);
			EnumParameterNode->SetUid(SelectedImageParamUid.ToString());
			EnumParameterNode->SetValueCount(ArrayOptionTexture.Num());
			EnumParameterNode->SetDefaultValueIndex(0);
			EnumParameterNode->SetRangeCount(1);
			EnumParameterNode->SetRange(0, NodeRangeFromScalar);

			GenerationContext.GeneratedEnumParameters.Add(SelectedImageParamUid.ToString(), EnumParameterNode);
					
			FMutableParameterData ParameterUIData;
			ParameterUIData.ParamUIMetadata = ProjParamNode->ParamUIMetadata;
			ParameterUIData.Type = EMutableParameterType::Int;
			ParameterUIData.IntegerParameterGroupType = ECustomizableObjectGroupType::COGT_ONE;
			ParameterUIData.ParamUIMetadata.ExtraInformation.Add(FString("UseThumbnails"));

			for (int OptionTextureIndex = 0; OptionTextureIndex < ArrayOptionTexture.Num(); ++OptionTextureIndex)
			{
				EnumParameterNode->SetValue(OptionTextureIndex, (float)OptionTextureIndex, ArrayOptionTexture[OptionTextureIndex].OptionName);

				FMutableParamUIMetadata OptionMetadata = ParameterUIData.ParamUIMetadata;
				OptionMetadata.UIThumbnail = ArrayOptionTexture[OptionTextureIndex].OptionTexture;
				ParameterUIData.ArrayIntegerParameterOption.Add(
					ArrayOptionTexture[OptionTextureIndex].OptionName,
					FIntegerParameterUIData(OptionMetadata));
			}

			GenerationContext.ParameterUIDataMap.Add(NodeEnumParamName, ParameterUIData);
		}

		mu::NodeImageSwitchPtr SwitchNode = new mu::NodeImageSwitch;
		SwitchNode->SetParameter(EnumParameterNode);
		SwitchNode->SetOptionCount(ArrayOptionTexture.Num());

		const uint32 AdditionalLODBias = GenerationContext.Options.bUseLODAsBias ? GenerationContext.FirstLODAvailable : 0;
		for (int32 SelectorIndex = 0; SelectorIndex < ArrayOptionTexture.Num(); ++SelectorIndex)
		{
			if (const TObjectPtr<UTexture2D>& Texture = ArrayOptionTexture[SelectorIndex].OptionTexture)
			{
				mu::Ptr<mu::Image> ImageConstant = GenerateImageConstant(ArrayOptionTexture[SelectorIndex].OptionTexture, GenerationContext, false);

				mu::Ptr<mu::NodeImageConstant> ImageNode = new mu::NodeImageConstant();
				ImageNode->SetValue(ImageConstant.get());

				const uint32 MipsToSkip = ComputeLODBiasForTexture(GenerationContext, *Texture, ProjParamNode->ReferenceTexture) + AdditionalLODBias;
				SwitchNode->SetOption(SelectorIndex, ResizeTextureByNumMips(ImageNode, MipsToSkip));

				// Calculate the number of mips to tag as high res for this image.
				if (ProjParamNode->ReferenceTexture)
				{
					int32 TotalMips = mu::Image::GetMipmapCount(ProjParamNode->ReferenceTexture->GetSizeX(), ProjParamNode->ReferenceTexture->GetSizeY());
					int32 NumMipsBeyondMin = FMath::Max(0, TotalMips - GenerationContext.Options.MinDiskMips);
					int32 HighResMipsForThisImage = FMath::Min(NumMipsBeyondMin, GenerationContext.Options.NumHighResImageMips);
					ImageNode->SourceDataDescriptor.SourceHighResMips = HighResMipsForThisImage;

					const FString TextureName = GetNameSafe(Texture).ToLower();
					ImageNode->SourceDataDescriptor.SourceId = CityHash32(reinterpret_cast<const char*>(*TextureName), TextureName.Len() * sizeof(FString::ElementType));
				}
			}
			else
			{
				FString msg = FString::Printf(TEXT("The group projection node must have a texture for all the options. Please set a texture for all the options."));
				GenerationContext.Log(FText::FromString(msg), ProjParamNode);
			}
		}

		int32 TextureSize = ProjParamNode->ProjectionTextureSize > 0 ? ProjParamNode->ProjectionTextureSize : 512;

		// If TextureSize is not power of two, round up to the next power of two 
		if (!FMath::IsPowerOfTwo(TextureSize))
		{
			TextureSize = FMath::RoundUpToPowerOfTwo(TextureSize);
		}

		// Apply additional LODBias if necessary
		GroupProjectorTempData.TextureSize = FMath::Max(TextureSize >> AdditionalLODBias, 1);

		GroupProjectorTempData.NodeImagePtr = SwitchNode;
	}

	GenerationContext.GeneratedGroupProjectors.Add(Key, GroupProjectorTempData);
	
	return GroupProjectorTempData;
}

#undef LOCTEXT_NAMESPACE

