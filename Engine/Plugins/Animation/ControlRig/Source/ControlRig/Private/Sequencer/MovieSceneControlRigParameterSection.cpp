// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneControlRigParameterSection.h"

#include "ConstraintsManager.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Logging/MessageLog.h"
#include "Compilation/MovieSceneTemplateInterrogation.h"
#include "MovieScene.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Rigs/FKControlRig.h"
#include "Animation/AnimSequence.h"
#include "Units/Execution/RigUnit_InverseExecution.h"
#include "Misc/ScopedSlowTask.h"
#include "MovieSceneTimeHelpers.h"
#include "Animation/AnimSequenceHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "Constraints/ControlRigTransformableHandle.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "TransformConstraint.h"
#include "TransformableHandle.h"
#include "AnimationCoreLibrary.h"
#include "UObject/ObjectSaveContext.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneControlRigParameterSection)

#if WITH_EDITOR
#include "AnimPose.h"
#endif

#define LOCTEXT_NAMESPACE "MovieSceneControlParameterRigSection"

#if WITH_EDITOR

struct FParameterFloatChannelEditorData
{
	FParameterFloatChannelEditorData(UControlRig *InControlRig, const FName& InName, bool bEnabledOverride, const FText& GroupName, int SortStartIndex)
	{
		ControlRig = InControlRig;
		ParameterName = InName;
		FString NameAsString = InName.ToString();
		{
			MetaData.SetIdentifiers(InName, GroupName, GroupName);
			MetaData.bEnabled = bEnabledOverride;
			MetaData.SortOrder = SortStartIndex++;
			MetaData.bCanCollapseToTrack = true;
		}

		ExternalValues.OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return GetValue(InControlRig, InName,InObject, Bindings); };
		
		ExternalValues.OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight) { GetChannelValueAndWeight(InName, Object, SectionToKey, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight); };
		
	}

	static TOptional<float> GetValue(UControlRig* ControlRig, FName ParameterName, UObject& InObject, FTrackInstancePropertyBindings* Bindings)
	{
		if (ControlRig)
		{
			FRigControlElement* ControlElement = ControlRig->FindControl(ParameterName);
			if (ControlElement)
			{
				return ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<float>();
			}
		}
		return TOptional<float>();
	}
	
	static void GetChannelValueAndWeight(FName ParameterName, UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
		float& OutValue, float& OutWeight)
	{
		OutValue = 0.0f;
		OutWeight = 1.0f;

		UMovieSceneTrack* Track = SectionToKey->GetTypedOuter<UMovieSceneTrack>();

		if (Track)
		{
			FMovieSceneEvaluationTrack EvalTrack = CastChecked<IMovieSceneTrackTemplateProducer>(Track)->GenerateTrackTemplate(Track);
			FMovieSceneInterrogationData InterrogationData;
			RootTemplate.CopyActuators(InterrogationData.GetAccumulator());

			FMovieSceneContext Context(FMovieSceneEvaluationRange(KeyTime, TickResolution));
			EvalTrack.Interrogate(Context, InterrogationData, Object);

			float Val = 0.0f;
			for (const FFloatInterrogationData& InVector : InterrogationData.Iterate<FFloatInterrogationData>(UMovieSceneControlRigParameterSection::GetFloatInterrogationKey()))
			{
				if (InVector.ParameterName == ParameterName)
				{
					Val = InVector.Val;
					break;
				}
			}
			OutValue = Val;
		}
		OutWeight = MovieSceneHelpers::CalculateWeightForBlending(SectionToKey, KeyTime);
	}

	FText							GroupName;
	FMovieSceneChannelMetaData      MetaData;
	TMovieSceneExternalValue<float> ExternalValues;
	FName ParameterName;
	UControlRig *ControlRig;
};

//Set up with all 4 Channels so it can be used by all vector types.
struct FParameterVectorChannelEditorData
{
	FParameterVectorChannelEditorData(UControlRig *InControlRig, const FName& InName, bool bEnabledOverride, const FText& GroupName, int SortStartIndex, int32 NumChannels)
	{
		ControlRig = InControlRig;
		ParameterName = InName;
		FString NameAsString = InName.ToString();
		FString TotalName = NameAsString;

		{
			TotalName += ".X";
			MetaData[0].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelX);
			TotalName = NameAsString;
			MetaData[0].IntentName = FCommonChannelData::ChannelX;
			MetaData[0].Group = GroupName;
			MetaData[0].bEnabled = bEnabledOverride;
			MetaData[0].SortOrder = SortStartIndex++;
			MetaData[0].bCanCollapseToTrack = true;
		}
		{
			TotalName += ".Y";
			MetaData[1].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelY);
			TotalName = NameAsString;
			MetaData[1].IntentName = FCommonChannelData::ChannelY;
			MetaData[1].Group = GroupName;
			MetaData[1].bEnabled = bEnabledOverride;
			MetaData[1].SortOrder = SortStartIndex++;
			MetaData[1].bCanCollapseToTrack = true;
		}
		{
			TotalName += ".Z";
			MetaData[2].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelZ);
			TotalName = NameAsString;
			MetaData[2].IntentName = FCommonChannelData::ChannelZ;
			MetaData[2].Group = GroupName;
			MetaData[2].bEnabled = bEnabledOverride;
			MetaData[2].SortOrder = SortStartIndex++;
			MetaData[2].bCanCollapseToTrack = true;
		}
		{
			TotalName += ".W";
			MetaData[3].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelW);
			TotalName = NameAsString;
			MetaData[3].IntentName = FCommonChannelData::ChannelW;
			MetaData[3].Group = GroupName;
			MetaData[3].bEnabled = bEnabledOverride;
			MetaData[3].SortOrder = SortStartIndex++;
			MetaData[3].bCanCollapseToTrack = true;
		}

		ExternalValues[0].OnGetExternalValue = [InControlRig, InName,NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelX(InObject, InControlRig, InName, NumChannels); };
		ExternalValues[1].OnGetExternalValue = [InControlRig, InName,NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelY(InObject, InControlRig, InName, NumChannels); };
		ExternalValues[2].OnGetExternalValue = [InControlRig, InName,NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelZ(InObject, InControlRig, InName, NumChannels); };
		ExternalValues[3].OnGetExternalValue = [InControlRig, InName,NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelW(InObject, InControlRig, InName, NumChannels); };

		ExternalValues[0].OnGetCurrentValueAndWeight = [InName, NumChannels](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight) { GetChannelValueAndWeight(InName, NumChannels, 0, Object, SectionToKey, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight); };
		ExternalValues[1].OnGetCurrentValueAndWeight = [InName, NumChannels](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight) { GetChannelValueAndWeight(InName, NumChannels, 1, Object, SectionToKey, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight); };
		ExternalValues[2].OnGetCurrentValueAndWeight = [InName, NumChannels](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight) { GetChannelValueAndWeight(InName, NumChannels, 2, Object, SectionToKey, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight); };
		ExternalValues[3].OnGetCurrentValueAndWeight = [InName, NumChannels](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight) { GetChannelValueAndWeight(InName, NumChannels, 3, Object, SectionToKey, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight); };

	}

	static FVector4 GetPropertyValue(UControlRig* ControlRig, FName ParameterName, UObject& InObject,int32 NumChannels)
	{
		if (ControlRig)
		{
			FRigControlElement* ControlElement = ControlRig->FindControl(ParameterName);
			if (ControlElement)
			{
		
				if (NumChannels == 2)
				{
					const FVector3f Vector = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
					return FVector4(Vector.X, Vector.Y, 0.f, 0.f);
				}
				else if (NumChannels == 3)
				{
					const FVector3f Vector = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
					return FVector4(Vector.X, Vector.Y, Vector.Z, 0.f);
				}
				else
				{
					const FRigControlValue::FTransform_Float Storage = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransform_Float>();
#if ENABLE_VECTORIZED_TRANSFORM
					return FVector4(Storage.TranslationX, Storage.TranslationY, Storage.TranslationZ, Storage.TranslationW);
#else
					return FVector4(Storage.TranslationX, Storage.TranslationY, Storage.TranslationZ, 0.f);
#endif
				}
			}
		}
		return FVector4();
	}

	static TOptional<float> ExtractChannelX(UObject& InObject, UControlRig* ControlRig, FName ParameterName, int32 NumChannels)
	{
		return GetPropertyValue(ControlRig, ParameterName, InObject, NumChannels).X;
	}
	static TOptional<float> ExtractChannelY(UObject& InObject, UControlRig* ControlRig, FName ParameterName, int32 NumChannels)
	{
		return GetPropertyValue(ControlRig, ParameterName, InObject, NumChannels).Y;
	}
	static TOptional<float> ExtractChannelZ(UObject& InObject, UControlRig* ControlRig, FName ParameterName, int32 NumChannels)
	{
		return GetPropertyValue(ControlRig, ParameterName, InObject, NumChannels).Z;
	}
	static TOptional<float> ExtractChannelW(UObject& InObject, UControlRig* ControlRig, FName ParameterName, int32 NumChannels)
	{
		return GetPropertyValue(ControlRig, ParameterName, InObject, NumChannels).W;
	}

	static void GetChannelValueAndWeight(FName ParameterName, int32 NumChannels, int32 Index, UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
		float& OutValue, float& OutWeight)
	{
		OutValue = 0.0f;
		OutWeight = 1.0f;
		if (Index >= NumChannels)
		{
			return;
		}

		UMovieSceneTrack* Track = SectionToKey->GetTypedOuter<UMovieSceneTrack>();

		if (Track)
		{
			FMovieSceneEvaluationTrack EvalTrack = CastChecked<IMovieSceneTrackTemplateProducer>(Track)->GenerateTrackTemplate(Track);
			FMovieSceneInterrogationData InterrogationData;
			RootTemplate.CopyActuators(InterrogationData.GetAccumulator());

			FMovieSceneContext Context(FMovieSceneEvaluationRange(KeyTime, TickResolution));
			EvalTrack.Interrogate(Context, InterrogationData, Object);

			switch (NumChannels)
			{
			case 2:
			{
				FVector2D Val(0.0f, 0.0f);
				for (const FVector2DInterrogationData& InVector : InterrogationData.Iterate<FVector2DInterrogationData>(UMovieSceneControlRigParameterSection::GetVector2DInterrogationKey()))
				{
					if (InVector.ParameterName == ParameterName)
					{
						Val = InVector.Val;
						break;
					}
				}
				switch (Index)
				{
				case 0:
					OutValue = Val.X;
					break;
				case 1:
					OutValue = Val.Y;
					break;
				}
			}
			break;
			case 3:
			{
				FVector Val(0.0f, 0.0f, 0.0f);
				for (const FVectorInterrogationData& InVector : InterrogationData.Iterate<FVectorInterrogationData>(UMovieSceneControlRigParameterSection::GetVectorInterrogationKey()))
				{
					if (InVector.ParameterName == ParameterName)
					{
						Val = InVector.Val;
						break;
					}
				}
				switch (Index)
				{
				case 0:
					OutValue = Val.X;
					break;
				case 1:
					OutValue = Val.Y;
					break;
				case 2:
					OutValue = Val.Z;
					break;
				}
			}
			break;
			case 4:
			{
				/* No Interrogation for Vector4, todo if we do add later
				FVector4 Val(0.0f, 0.0f, 0.0f, 0.0f);
				for (const FVector4InterrogationData& InVector : InterrogationData.Iterate<FVector4InterrogationData>(UMovieSceneControlRigParameterSection::GetVector4InterrogationKey()))
				{
					if (InVector.ParameterName == ParameterName)
					{
						Val = InVector.Val;
						break;
					}
				}
				switch (Index)
				{
				case 0:
					OutValue = Val.X;
					break;
				case 1:
					OutValue = Val.Y;
					break;
				case 2:
					OutValue = Val.Z;
					break;
				case 3:
					OutValue = Val.W;
					break;
				}
				*/
			}
			
			break;
			}
		}
		OutWeight = MovieSceneHelpers::CalculateWeightForBlending(SectionToKey, KeyTime);
	}
	FText							GroupName;
	FMovieSceneChannelMetaData      MetaData[4];
	TMovieSceneExternalValue<float> ExternalValues[4];
	FName ParameterName;
	UControlRig *ControlRig;
};

struct FParameterTransformChannelEditorData
{
	FParameterTransformChannelEditorData(UControlRig *InControlRig, const FName& InName, bool bEnabledOverride, EMovieSceneTransformChannel Mask, 
		const FText& GroupName, int SortStartIndex)
	{
		ControlRig = InControlRig;
		ParameterName = InName;
		static FText LongIntentFormatStr = NSLOCTEXT("MovieSceneControlParameterRigSection", "LongIntentFormatString", "{GroupName}.{IntentName}");

		static const TSet<FName> PropertyMetaDataKeys = { "UIMin", "UIMax", "SliderExponent", "LinearDeltaSensitivity", "Delta", "ClampMin", "ClampMax", "ForceUnits", "WheelStep" };

		const FProperty* RelativeLocationProperty = USceneComponent::StaticClass()->FindPropertyByName(USceneComponent::GetRelativeLocationPropertyName());
		const FProperty* RelativeRotationProperty = USceneComponent::StaticClass()->FindPropertyByName(USceneComponent::GetRelativeRotationPropertyName());
		const FProperty* RelativeScale3DProperty = USceneComponent::StaticClass()->FindPropertyByName(USceneComponent::GetRelativeScale3DPropertyName());

		//FText LocationGroup = NSLOCTEXT("MovieSceneControlParameterRigSection", "Location", "Location");
		//FText RotationGroup = NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation", "Rotation");
		//FText ScaleGroup = NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale", "Scale");

		FString NameAsString = InName.ToString();
		FString TotalName = NameAsString;
		FText TransformGroup = FText::Format(NSLOCTEXT("MovieSceneControlParameterRigSection", "MovieSceneControlParameterRigSectionGroupName", "{0}"), GroupName);

		{
			//MetaData[0].SetIdentifiers("Location.X", FCommonChannelData::ChannelX, LocationGroup);
			TotalName += ".Location.X";
			MetaData[0].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Location.X", "Location.X"), TransformGroup);
			MetaData[0].IntentName = NSLOCTEXT("MovieSceneControlParameterRigSection", "Location.X", "Location.X");
			MetaData[0].LongIntentNameFormat = LongIntentFormatStr;
			TotalName = NameAsString;

			MetaData[0].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::TranslationX);
			MetaData[0].Color = FCommonChannelData::RedChannelColor;
			MetaData[0].SortOrder = SortStartIndex++;
			MetaData[0].bCanCollapseToTrack = true;
			for (const FName& PropertyMetaDataKey : PropertyMetaDataKeys)
			{
				MetaData[0].PropertyMetaData.Add(PropertyMetaDataKey, RelativeLocationProperty->GetMetaData(PropertyMetaDataKey));
			}

			//MetaData[1].SetIdentifiers("Location.Y", FCommonChannelData::ChannelY, LocationGroup);
			TotalName += ".Location.Y";
			MetaData[1].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Location.Y", "Location.Y"), TransformGroup);
			MetaData[1].IntentName = NSLOCTEXT("MovieSceneControlParameterRigSection", "Location.Y", "Location.Y");
			MetaData[1].LongIntentNameFormat = LongIntentFormatStr;
			TotalName = NameAsString;

			MetaData[1].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::TranslationY);
			MetaData[1].Color = FCommonChannelData::GreenChannelColor;
			MetaData[1].SortOrder = SortStartIndex++;
			MetaData[1].bCanCollapseToTrack = true;
			for (const FName& PropertyMetaDataKey : PropertyMetaDataKeys)
			{
				MetaData[1].PropertyMetaData.Add(PropertyMetaDataKey, RelativeLocationProperty->GetMetaData(PropertyMetaDataKey));
			}

			//MetaData[2].SetIdentifiers("Location.Z", FCommonChannelData::ChannelZ, LocationGroup);
			TotalName += ".Location.Z";
			MetaData[2].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Location.Z", "Location.Z"), TransformGroup);
			MetaData[2].IntentName = NSLOCTEXT("MovieSceneControlParameterRigSection", "Location.Z", "Location.Z");
			MetaData[2].LongIntentNameFormat = LongIntentFormatStr;
			TotalName = NameAsString;

			MetaData[2].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::TranslationZ);
			MetaData[2].Color = FCommonChannelData::BlueChannelColor;
			MetaData[2].SortOrder = SortStartIndex++;
			MetaData[2].bCanCollapseToTrack = true;
			for (const FName& PropertyMetaDataKey : PropertyMetaDataKeys)
			{
				MetaData[2].PropertyMetaData.Add(PropertyMetaDataKey, RelativeLocationProperty->GetMetaData(PropertyMetaDataKey));
			}
		}
		{
			//MetaData[3].SetIdentifiers("Rotation.X", NSLOCTEXT("MovieSceneTransformSection", "RotationX", "Roll"), RotationGroup);
			TotalName += ".Rotation.X";
			MetaData[3].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation.X", "Rotation.Roll"), TransformGroup);
			MetaData[3].IntentName = NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation.X", "Rotation.Roll");
			MetaData[3].LongIntentNameFormat = LongIntentFormatStr;
			TotalName = NameAsString;

			MetaData[3].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::RotationX);
			MetaData[3].Color = FCommonChannelData::RedChannelColor;
			MetaData[3].SortOrder = SortStartIndex++;
			MetaData[3].bCanCollapseToTrack = true;
			for (const FName& PropertyMetaDataKey : PropertyMetaDataKeys)
			{
				MetaData[3].PropertyMetaData.Add(PropertyMetaDataKey, RelativeRotationProperty->GetMetaData(PropertyMetaDataKey));
			}

			//MetaData[4].SetIdentifiers("Rotation.Y", NSLOCTEXT("MovieSceneTransformSection", "RotationY", "Pitch"), RotationGroup);
			TotalName += ".Rotation.Y";
			MetaData[4].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation.Y", "Rotation.Pitch"), TransformGroup);
			MetaData[4].IntentName = NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation.Y", "Rotation.Pitch");
			MetaData[4].LongIntentNameFormat = LongIntentFormatStr;
			TotalName = NameAsString;

			MetaData[4].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::RotationY);
			MetaData[4].Color = FCommonChannelData::GreenChannelColor;
			MetaData[4].SortOrder = SortStartIndex++;
			MetaData[4].bCanCollapseToTrack = true;
			for (const FName& PropertyMetaDataKey : PropertyMetaDataKeys)
			{
				MetaData[4].PropertyMetaData.Add(PropertyMetaDataKey, RelativeRotationProperty->GetMetaData(PropertyMetaDataKey));
			}

			//MetaData[5].SetIdentifiers("Rotation.Z", NSLOCTEXT("MovieSceneTransformSection", "RotationZ", "Yaw"), RotationGroup);
			TotalName += ".Rotation.Z";
			MetaData[5].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation.Z", "Rotation.Yaw"), TransformGroup);
			MetaData[5].IntentName = NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation.Z", "Rotation.Yaw");
			MetaData[5].LongIntentNameFormat = LongIntentFormatStr;
			TotalName = NameAsString;

			MetaData[5].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::RotationZ);
			MetaData[5].Color = FCommonChannelData::BlueChannelColor;
			MetaData[5].SortOrder = SortStartIndex++;
			MetaData[5].bCanCollapseToTrack = true;
			for (const FName& PropertyMetaDataKey : PropertyMetaDataKeys)
			{
				MetaData[5].PropertyMetaData.Add(PropertyMetaDataKey, RelativeRotationProperty->GetMetaData(PropertyMetaDataKey));
			}
		}
		{
			//MetaData[6].SetIdentifiers("Scale.X", FCommonChannelData::ChannelX, ScaleGroup);
			TotalName += ".Scale.X";
			MetaData[6].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale.X", "Scale.X"), TransformGroup);
			MetaData[6].IntentName = NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale.X", "Scale.X");
			MetaData[6].LongIntentNameFormat = LongIntentFormatStr;
			TotalName = NameAsString;

			MetaData[6].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::ScaleX);
			MetaData[6].Color = FCommonChannelData::RedChannelColor;
			MetaData[6].SortOrder = SortStartIndex++;
			MetaData[6].bCanCollapseToTrack = true;
			for (const FName& PropertyMetaDataKey : PropertyMetaDataKeys)
			{
				MetaData[6].PropertyMetaData.Add(PropertyMetaDataKey, RelativeScale3DProperty->GetMetaData(PropertyMetaDataKey));
			}

			//MetaData[7].SetIdentifiers("Scale.Y", FCommonChannelData::ChannelY, ScaleGroup);
			TotalName += ".Scale.Y";
			MetaData[7].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale.Y", "Scale.Y"), TransformGroup);
			MetaData[7].IntentName = NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale.Y", "Scale.Y");
			MetaData[7].LongIntentNameFormat = LongIntentFormatStr;
			TotalName = NameAsString;

			MetaData[7].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::ScaleY);
			MetaData[7].Color = FCommonChannelData::GreenChannelColor;
			MetaData[7].SortOrder = SortStartIndex++;
			MetaData[7].bCanCollapseToTrack = true;
			for (const FName& PropertyMetaDataKey : PropertyMetaDataKeys)
			{
				MetaData[7].PropertyMetaData.Add(PropertyMetaDataKey, RelativeScale3DProperty->GetMetaData(PropertyMetaDataKey));
			}

			//MetaData[8].SetIdentifiers("Scale.Z", FCommonChannelData::ChannelZ, ScaleGroup);
			TotalName += ".Scale.Z";
			MetaData[8].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale.Z", "Scale.Z"), TransformGroup);
			MetaData[8].IntentName = NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale.Z", "Scale.Z");
			MetaData[8].LongIntentNameFormat = LongIntentFormatStr;
			TotalName = NameAsString;

			MetaData[8].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::ScaleZ);
			MetaData[8].Color = FCommonChannelData::BlueChannelColor;
			MetaData[8].SortOrder = SortStartIndex++;
			MetaData[8].bCanCollapseToTrack = true;
			for (const FName& PropertyMetaDataKey : PropertyMetaDataKeys)
			{
				MetaData[8].PropertyMetaData.Add(PropertyMetaDataKey, RelativeScale3DProperty->GetMetaData(PropertyMetaDataKey));
			}
		}
		{
			//MetaData[9].SetIdentifiers("Weight", NSLOCTEXT("MovieSceneTransformSection", "Weight", "Weight"));
			//MetaData[9].bEnabled = EnumHasAllFlags(Mask, EMovieSceneTransformChannel::Weight);
		}

		ExternalValues[0].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Translation = GetTranslation(InControlRig, InName, InObject, Bindings);
			return Translation.IsSet() ? Translation->X : TOptional<float>();
		};

		ExternalValues[1].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Translation = GetTranslation(InControlRig, InName, InObject, Bindings);
			return Translation.IsSet() ? Translation->Y : TOptional<float>();
		};
		ExternalValues[2].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Translation = GetTranslation(InControlRig, InName, InObject, Bindings);
			return Translation.IsSet() ? Translation->Z : TOptional<float>();
		};
		ExternalValues[3].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FRotator> Rotator = GetRotator(InControlRig, InName, InObject, Bindings);
			return Rotator.IsSet() ? Rotator->Roll : TOptional<float>();
		};
		ExternalValues[4].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FRotator> Rotator = GetRotator(InControlRig, InName, InObject, Bindings);
			return Rotator.IsSet() ? Rotator->Pitch : TOptional<float>();
		};
		ExternalValues[5].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FRotator> Rotator = GetRotator(InControlRig, InName, InObject, Bindings);
			return Rotator.IsSet() ? Rotator->Yaw : TOptional<float>();
		};
		ExternalValues[6].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Scale = GetScale(InControlRig, InName, InObject, Bindings);
			return Scale.IsSet() ? Scale->X : TOptional<float>();
		};
		ExternalValues[7].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Scale = GetScale(InControlRig, InName, InObject, Bindings);
			return Scale.IsSet() ? Scale->Y : TOptional<float>();
		};
		ExternalValues[8].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Scale = GetScale(InControlRig, InName, InObject, Bindings);
			return Scale.IsSet() ? Scale->Z : TOptional<float>();
		};

		ExternalValues[0].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 0, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[1].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 1, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[2].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 2, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[3].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 3, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[4].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 4, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[5].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 5, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[6].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 6, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[7].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 7, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[8].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 8, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};

	}

	static TOptional<FVector> GetTranslation(UControlRig* ControlRig, FName ParameterName, UObject& InObject, FTrackInstancePropertyBindings* Bindings)
	{
		if (ControlRig)
		{
			FRigControlElement* ControlElement = ControlRig->FindControl(ParameterName);
			if (ControlElement)
			{
				auto GetTranslationFromTransform = [&](const FVector& InTranslation)
				{
					// switch translation to constraint space if needed
					const uint32 ControlHash = UTransformableControlHandle::ComputeHash(ControlRig, ControlElement->GetFName());
					TOptional<FTransform> ConstraintSpaceTransform = FTransformConstraintUtils::GetRelativeTransform(ControlRig->GetWorld(), ControlHash);
					if (ConstraintSpaceTransform)
					{
						return ConstraintSpaceTransform->GetTranslation();
					}
					return InTranslation;
				};
				
				if (ControlElement->Settings.ControlType == ERigControlType::Transform)
				{
					const FRigControlValue::FTransform_Float Transform = 
						ControlRig
						->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransform_Float>();

					return GetTranslationFromTransform( FVector(Transform.GetTranslation()) );
				}
				else if  (ControlElement->Settings.ControlType == ERigControlType::TransformNoScale)
				{
					const FRigControlValue::FTransformNoScale_Float Transform = 
						ControlRig
						->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransformNoScale_Float>();

					return GetTranslationFromTransform( FVector(Transform.GetTranslation()) );
				}
				else if (ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
				{
					const FRigControlValue::FEulerTransform_Float Euler = 
						ControlRig
						->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FEulerTransform_Float>();

					return GetTranslationFromTransform( FVector(Euler.GetTranslation()) );
				}
				else if(ControlElement->Settings.ControlType == ERigControlType::Position)
				{
					FVector3f Vector = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
					return FVector(Vector.X, Vector.Y, Vector.Z);
				}
			}
		}
		return TOptional<FVector>();
	}

	static TOptional<FRotator> GetRotator(UControlRig* ControlRig, FName ParameterName, UObject& InObject, FTrackInstancePropertyBindings* Bindings)
	{
		if (ControlRig)

		{
			FRigControlElement* ControlElement = ControlRig->FindControl(ParameterName);
			if (ControlElement)
			{
				if (ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
				{
					// switch rotation to constraint space if needed
					const uint32 ControlHash = UTransformableControlHandle::ComputeHash(ControlRig, ControlElement->GetFName());
					TOptional<FTransform> ConstraintSpaceTransform = FTransformConstraintUtils::GetRelativeTransform(ControlRig->GetWorld(), ControlHash);
					if (ConstraintSpaceTransform)
					{
						return ConstraintSpaceTransform->GetRotation().Rotator();
					}
				}
				
				FVector Vector = ControlRig->GetControlSpecifiedEulerAngle(ControlElement);
				FRotator Rotation = FRotator(Vector.Y, Vector.Z, Vector.X);
				return Rotation;
			}
		}
		return TOptional<FRotator>();
	}

	static TOptional<FVector> GetScale(UControlRig* ControlRig, FName ParameterName, UObject& InObject, FTrackInstancePropertyBindings* Bindings)
	{
		if (ControlRig)
		{
			FRigControlElement* ControlElement = ControlRig->FindControl(ParameterName);
			if (ControlElement)
			{
				auto GetScaleFromTransform = [&](const FVector& InScale3D)
				{
					// switch scale to constraint space if needed
					const uint32 ControlHash = UTransformableControlHandle::ComputeHash(ControlRig, ControlElement->GetFName());
					TOptional<FTransform> ConstraintSpaceTransform = FTransformConstraintUtils::GetRelativeTransform(ControlRig->GetWorld(), ControlHash);
					if (ConstraintSpaceTransform)
					{
						return ConstraintSpaceTransform->GetScale3D();
					}
					return InScale3D;
				};
				
				if (ControlElement->Settings.ControlType == ERigControlType::Transform)
				{
					const FRigControlValue::FTransform_Float Transform = 
						ControlRig
						->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransform_Float>();

					return GetScaleFromTransform( FVector(Transform.GetScale3D()) );
				}
				else if (ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
				{
					const FRigControlValue::FEulerTransform_Float Transform = 
						ControlRig
						->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FEulerTransform_Float>();

					return GetScaleFromTransform( FVector(Transform.GetScale3D()) );
				}
				else if (ControlElement->Settings.ControlType == ERigControlType::Scale)
				{
					FVector3f Vector = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
					return FVector(Vector.X, Vector.Y, Vector.Z);
				}
			}
		}
		return TOptional<FVector>();
	}

	static void GetValueAndWeight(FName ParameterName, UObject* Object, UMovieSceneSection*  SectionToKey, int32 Index, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
		float& OutValue, float& OutWeight)
	{
		UMovieSceneTrack* Track = SectionToKey->GetTypedOuter<UMovieSceneTrack>();
		FMovieSceneEvaluationTrack EvalTrack = CastChecked<UMovieSceneControlRigParameterTrack>(Track)->GenerateTrackTemplate(Track);
		FMovieSceneInterrogationData InterrogationData;
		RootTemplate.CopyActuators(InterrogationData.GetAccumulator());

		FMovieSceneContext Context(FMovieSceneEvaluationRange(KeyTime, TickResolution));
		EvalTrack.Interrogate(Context, InterrogationData, Object);

		FVector CurrentPos = FVector::ZeroVector;
		FRotator CurrentRot = FRotator::ZeroRotator;
		FVector CurrentScale = FVector::ZeroVector;

		for (const FEulerTransformInterrogationData& Transform : InterrogationData.Iterate<FEulerTransformInterrogationData>(UMovieSceneControlRigParameterSection::GetTransformInterrogationKey()))
		{
			if (Transform.ParameterName == ParameterName)
			{
				CurrentPos = Transform.Val.GetLocation();
				CurrentRot = Transform.Val.Rotator();
				CurrentScale = Transform.Val.GetScale3D();
				break;
			}
		}

		switch (Index)
		{
		case 0:
			OutValue = CurrentPos.X;
			break;
		case 1:
			OutValue = CurrentPos.Y;
			break;
		case 2:
			OutValue = CurrentPos.Z;
			break;
		case 3:
			OutValue = CurrentRot.Roll;
			break;
		case 4:
			OutValue = CurrentRot.Pitch;
			break;
		case 5:
			OutValue = CurrentRot.Yaw;
			break;
		case 6:
			OutValue = CurrentScale.X;
			break;
		case 7:
			OutValue = CurrentScale.Y;
			break;
		case 8:
			OutValue = CurrentScale.Z;
			break;

		}
		OutWeight = MovieSceneHelpers::CalculateWeightForBlending(SectionToKey, KeyTime);
	}
		
public:

	FText							GroupName;
	FMovieSceneChannelMetaData      MetaData[9];
	TMovieSceneExternalValue<float> ExternalValues[9];
	FName ParameterName;
	UControlRig *ControlRig;
};

#endif // WITH_EDITOR

UMovieSceneControlRigParameterSection::UMovieSceneControlRigParameterSection() :bDoNotKey(false)
{
	// Section template relies on always restoring state for objects when they are no longer animating. This is how it releases animation control.
	EvalOptions.CompletionMode = EMovieSceneCompletionMode::RestoreState;
	TransformMask = EMovieSceneTransformChannel::AllTransform;

	Weight.SetDefault(1.0f);

#if WITH_EDITOR

	static const FMovieSceneChannelMetaData MetaData("Weight", LOCTEXT("WeightChannelText", "Weight"));
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(Weight, MetaData, TMovieSceneExternalValue<float>());

#else

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(Weight);

#endif
}

void UMovieSceneControlRigParameterSection::OnBindingIDsUpdated(const TMap<UE::MovieScene::FFixedObjectBindingID, UE::MovieScene::FFixedObjectBindingID>& OldFixedToNewFixedMap, FMovieSceneSequenceID LocalSequenceID, TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState)
{
	for (FConstraintAndActiveChannel& ConstraintChannel : ConstraintsChannels)
	{
		if (UTickableTransformConstraint* TransformConstraint = Cast< UTickableTransformConstraint>(ConstraintChannel.GetConstraint()))
		{
			if (TransformConstraint->ChildTRSHandle)
			{
				TransformConstraint->ChildTRSHandle->OnBindingIDsUpdated(OldFixedToNewFixedMap, LocalSequenceID, SharedPlaybackState);
			}
			if (TransformConstraint->ParentTRSHandle)
			{
				TransformConstraint->ParentTRSHandle->OnBindingIDsUpdated(OldFixedToNewFixedMap, LocalSequenceID, SharedPlaybackState);
			}
		}
	}
}

void UMovieSceneControlRigParameterSection::GetReferencedBindings(TArray<FGuid>& OutBindings)
{
	for (FConstraintAndActiveChannel& ConstraintChannel : ConstraintsChannels)
	{
		if (UTickableTransformConstraint* TransformConstraint = Cast< UTickableTransformConstraint>(ConstraintChannel.GetConstraint().Get()))
		{
			if (TransformConstraint->ChildTRSHandle && TransformConstraint->ChildTRSHandle->ConstraintBindingID.IsValid())
			{
				OutBindings.Add(TransformConstraint->ChildTRSHandle->ConstraintBindingID.GetGuid());
			}
			if (TransformConstraint->ParentTRSHandle && TransformConstraint->ParentTRSHandle->ConstraintBindingID.IsValid())
			{
				OutBindings.Add(TransformConstraint->ParentTRSHandle->ConstraintBindingID.GetGuid());
			}
		}
	}
}

void UMovieSceneControlRigParameterSection::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
}

bool UMovieSceneControlRigParameterSection::RenameParameterName(const FName& OldParameterName, const FName& NewParameterName)
{
	bool bWasReplaced = false;

	auto RenameParameterNameInner = [this, &bWasReplaced, OldParameterName, NewParameterName](auto& ParameterNamesAndCurves)
	{
		for (auto& ParameterNameAndCurve : ParameterNamesAndCurves)
		{
			if (ParameterNameAndCurve.ParameterName == OldParameterName)
			{
				if (!bWasReplaced)
				{
					Modify();
					bWasReplaced = true;
				}
				ParameterNameAndCurve.ParameterName = NewParameterName;
				break;
			}
		}
	};

	RenameParameterNameInner(ScalarParameterNamesAndCurves);
	RenameParameterNameInner(BoolParameterNamesAndCurves);
	RenameParameterNameInner(EnumParameterNamesAndCurves);
	RenameParameterNameInner(IntegerParameterNamesAndCurves);
	RenameParameterNameInner(Vector2DParameterNamesAndCurves);
	RenameParameterNameInner(VectorParameterNamesAndCurves);
	RenameParameterNameInner(ColorParameterNamesAndCurves);
	RenameParameterNameInner(TransformParameterNamesAndCurves);

	if (bWasReplaced)
	{
		ReconstructChannelProxy();
	}
	return bWasReplaced;
}

//make sure to zero out Scale values if getting to Additive or use the current values if getting set to Override
void UMovieSceneControlRigParameterSection::SetBlendType(EMovieSceneBlendType InBlendType)
{
	if (GetSupportedBlendTypes().Contains(InBlendType))
	{
		Modify();
		BlendType = InBlendType;
		if (ControlRig)
		{
			// Set Defaults based upon Type
			TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
			TArray<FRigControlElement*> Controls = ControlRig->AvailableControls();

			for (FRigControlElement* ControlElement : Controls)
			{
				if (!ControlRig->GetHierarchy()->IsAnimatable(ControlElement))
				{
					continue;
				}
				FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ControlElement->GetFName());
				if (pChannelIndex == nullptr)
				{
					continue;
				}
				int32 ChannelIndex = pChannelIndex->ChannelIndex;

				switch (ControlElement->Settings.ControlType)
				{
				case ERigControlType::Float:
				case ERigControlType::ScaleFloat:
				{
					if (InBlendType == EMovieSceneBlendType::Override)
					{
						float Val = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<float>();
						FloatChannels[ChannelIndex]->SetDefault(Val);
					}
					break;
				}
				case ERigControlType::Vector2D:
				{
					if (InBlendType == EMovieSceneBlendType::Override)
					{
						FVector3f Val = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
						FloatChannels[ChannelIndex]->SetDefault(Val.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(Val.Y);
					}
					break;
				}
				case ERigControlType::Position:
				case ERigControlType::Rotator:
				{
					if (InBlendType == EMovieSceneBlendType::Override)
					{
						FVector3f Val = (ControlElement->Settings.ControlType == ERigControlType::Rotator)
							? FVector3f(ControlRig->GetHierarchy()->GetControlSpecifiedEulerAngle(ControlElement)) : ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();

						FloatChannels[ChannelIndex]->SetDefault(Val.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(Val.Y);
						FloatChannels[ChannelIndex + 2]->SetDefault(Val.Z);
					}
					break;
				}
				case ERigControlType::Scale:
				{
					if (InBlendType == EMovieSceneBlendType::Absolute)
					{
						FloatChannels[ChannelIndex]->SetDefault(1.0f);
						FloatChannels[ChannelIndex + 1]->SetDefault(1.0f);
						FloatChannels[ChannelIndex + 2]->SetDefault(1.0f);
					}
					else if (InBlendType == EMovieSceneBlendType::Additive)
					{
						FloatChannels[ChannelIndex]->SetDefault(0.0f);
						FloatChannels[ChannelIndex + 1]->SetDefault(0.0f);
						FloatChannels[ChannelIndex + 2]->SetDefault(0.0f);
					}
					else if (InBlendType == EMovieSceneBlendType::Override)
					{
						FVector3f Val = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
						FloatChannels[ChannelIndex]->SetDefault(Val.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(Val.Y);
						FloatChannels[ChannelIndex + 2]->SetDefault(Val.Z);
					}
				}
				break;
				case ERigControlType::Transform:
				case ERigControlType::EulerTransform:
				case ERigControlType::TransformNoScale:
				{
					FTransform Val = FTransform::Identity;
					if (ControlElement->Settings.ControlType == ERigControlType::TransformNoScale)
					{
						FTransformNoScale NoScale =
							ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransformNoScale_Float>().ToTransform();
						Val = NoScale;
					}
					else if (ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
					{
						FEulerTransform Euler =
							ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FEulerTransform_Float>().ToTransform();
						Val = Euler.ToFTransform();
					}
					else
					{
						Val = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransform_Float>().ToTransform();
					}
					if (InBlendType == EMovieSceneBlendType::Override)
					{
						FVector CurrentVector = Val.GetTranslation();
						FloatChannels[ChannelIndex]->SetDefault(CurrentVector.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(CurrentVector.Y);
						FloatChannels[ChannelIndex + 2]->SetDefault(CurrentVector.Z);

						CurrentVector = ControlRig->GetHierarchy()->GetControlSpecifiedEulerAngle(ControlElement);
						FloatChannels[ChannelIndex + 3]->SetDefault(CurrentVector.X);
						FloatChannels[ChannelIndex + 4]->SetDefault(CurrentVector.Y);
						FloatChannels[ChannelIndex + 5]->SetDefault(CurrentVector.Z);
					}
					if (ControlElement->Settings.ControlType != ERigControlType::TransformNoScale)
					{
						if (InBlendType == EMovieSceneBlendType::Absolute)
						{
							FloatChannels[ChannelIndex + 6]->SetDefault(1.0f);
							FloatChannels[ChannelIndex + 7]->SetDefault(1.0f);
							FloatChannels[ChannelIndex + 8]->SetDefault(1.0f);
						}
						else if (InBlendType == EMovieSceneBlendType::Additive)
						{
							FloatChannels[ChannelIndex + 6]->SetDefault(0.0f);
							FloatChannels[ChannelIndex + 7]->SetDefault(0.0f);
							FloatChannels[ChannelIndex + 8]->SetDefault(0.0f);
						}
						else if (InBlendType == EMovieSceneBlendType::Override)
						{
							FVector CurrentVector = Val.GetScale3D();
							FloatChannels[ChannelIndex + 6]->SetDefault(CurrentVector.X);
							FloatChannels[ChannelIndex + 7]->SetDefault(CurrentVector.Y);
							FloatChannels[ChannelIndex + 8]->SetDefault(CurrentVector.Z);
						}
					}
				}
				break;
				}
			};
		}
	}
}


void UMovieSceneControlRigParameterSection::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
}
#if WITH_EDITOR
void UMovieSceneControlRigParameterSection::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);	
}
#endif

void UMovieSceneControlRigParameterSection::PostEditImport()
{
	Super::PostEditImport();
	if (UMovieSceneControlRigParameterTrack* Track = Cast< UMovieSceneControlRigParameterTrack>(GetOuter()))
	{
		SetControlRig(Track->GetControlRig());
	}
	ReconstructChannelProxy();
}

void UMovieSceneControlRigParameterSection::PostLoad()
{
	Super::PostLoad();
	//for spawnables the control rig saved in our channels may have changed so we need to update thaem
	if (ControlRig)
	{
		for (FConstraintAndActiveChannel& ConstraintChannel : ConstraintsChannels)
		{
			if (UTickableTransformConstraint* TransformConstraint = Cast<UTickableTransformConstraint>(ConstraintChannel.GetConstraint()))
			{
				if (UTransformableControlHandle* Handle = Cast<UTransformableControlHandle>(TransformConstraint->ChildTRSHandle))
				{
					Handle->ControlRig = ControlRig;
				}
			}
		}
	}
}

bool UMovieSceneControlRigParameterSection::HasScalarParameter(FName InParameterName) const
{
	for (const FScalarParameterNameAndCurve& ScalarParameterNameAndCurve : ScalarParameterNamesAndCurves)
	{
		if (ScalarParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasBoolParameter(FName InParameterName) const
{
	for (const FBoolParameterNameAndCurve& BoolParameterNameAndCurve : BoolParameterNamesAndCurves)
	{
		if (BoolParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasEnumParameter(FName InParameterName) const
{
	for (const FEnumParameterNameAndCurve& EnumParameterNameAndCurve : EnumParameterNamesAndCurves)
	{
		if (EnumParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasIntegerParameter(FName InParameterName) const
{
	for (const FIntegerParameterNameAndCurve& IntegerParameterNameAndCurve : IntegerParameterNamesAndCurves)
	{
		if (IntegerParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasVector2DParameter(FName InParameterName) const
{
	for (const FVector2DParameterNameAndCurves& Vector2DParameterNameAndCurve : Vector2DParameterNamesAndCurves)
	{
		if (Vector2DParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasVectorParameter(FName InParameterName) const
{
	for (const FVectorParameterNameAndCurves& VectorParameterNameAndCurve : VectorParameterNamesAndCurves)
	{
		if (VectorParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasColorParameter(FName InParameterName) const
{
	for (const FColorParameterNameAndCurves& ColorParameterNameAndCurve : ColorParameterNamesAndCurves)
	{
		if (ColorParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasTransformParameter(FName InParameterName) const
{
	for (const FTransformParameterNameAndCurves& TransformParameterNamesAndCurve : TransformParameterNamesAndCurves)
	{
		if (TransformParameterNamesAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasSpaceChannel(FName InParameterName) const
{
	for (const FSpaceControlNameAndChannel& SpaceChannel : SpaceChannels)
	{
		if (SpaceChannel.ControlName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

FSpaceControlNameAndChannel* UMovieSceneControlRigParameterSection::GetSpaceChannel(FName InParameterName) 
{
	for (FSpaceControlNameAndChannel& SpaceChannel : SpaceChannels)
	{
		if (SpaceChannel.ControlName == InParameterName)
		{
			return &SpaceChannel;
		}
	}
	return nullptr;
}

FName UMovieSceneControlRigParameterSection::FindControlNameFromSpaceChannel(const FMovieSceneControlRigSpaceChannel* InSpaceChannel) const
{
	for (const FSpaceControlNameAndChannel& SpaceChannel : SpaceChannels)
	{
		if (&(SpaceChannel.SpaceCurve) == InSpaceChannel)
		{
			return SpaceChannel.ControlName;
		}
	}
	return NAME_None;
}

void UMovieSceneControlRigParameterSection::AddScalarParameter(FName InParameterName, TOptional<float> DefaultValue, bool bReconstructChannel)
{
	FMovieSceneFloatChannel* ExistingChannel = nullptr;
	if (!HasScalarParameter(InParameterName))
	{
		const int32 NewIndex = ScalarParameterNamesAndCurves.Add(FScalarParameterNameAndCurve(InParameterName));
		ExistingChannel = &ScalarParameterNamesAndCurves[NewIndex].ParameterCurve;
		if (DefaultValue.IsSet())
		{
			ExistingChannel->SetDefault(DefaultValue.GetValue());
		}
		else
		{
			ExistingChannel->SetDefault(0.0f);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy();
		}
	}
}


void UMovieSceneControlRigParameterSection::AddBoolParameter(FName InParameterName, TOptional<bool> DefaultValue, bool bReconstructChannel)
{
	FMovieSceneBoolChannel* ExistingChannel = nullptr;
	if (!HasBoolParameter(InParameterName))
	{
		const int32 NewIndex = BoolParameterNamesAndCurves.Add(FBoolParameterNameAndCurve(InParameterName));
		ExistingChannel = &BoolParameterNamesAndCurves[NewIndex].ParameterCurve;
		if (DefaultValue.IsSet())
		{
			ExistingChannel->SetDefault(DefaultValue.GetValue());
		}
		else
		{
			ExistingChannel->SetDefault(false);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy();
		}
	}
}
void UMovieSceneControlRigParameterSection::AddEnumParameter(FName InParameterName, UEnum* Enum,TOptional<uint8> DefaultValue, bool bReconstructChannel)
{
	FMovieSceneByteChannel* ExistingChannel = nullptr;
	if (!HasEnumParameter(InParameterName))
	{
		const int32 NewIndex = EnumParameterNamesAndCurves.Add(FEnumParameterNameAndCurve(InParameterName));
		ExistingChannel = &EnumParameterNamesAndCurves[NewIndex].ParameterCurve;
		if (DefaultValue.IsSet())
		{
			ExistingChannel->SetDefault(DefaultValue.GetValue());
		}
		else
		{
			ExistingChannel->SetDefault(false);
		}
		ExistingChannel->SetEnum(Enum);
		if (bReconstructChannel)
		{
			ReconstructChannelProxy();
		}
	}
}

void UMovieSceneControlRigParameterSection::AddIntegerParameter(FName InParameterName, TOptional<int32> DefaultValue, bool bReconstructChannel)
{
	FMovieSceneIntegerChannel* ExistingChannel = nullptr;
	if (!HasIntegerParameter(InParameterName))
	{
		const int32 NewIndex = IntegerParameterNamesAndCurves.Add(FIntegerParameterNameAndCurve(InParameterName));
		ExistingChannel = &IntegerParameterNamesAndCurves[NewIndex].ParameterCurve;
		if (DefaultValue.IsSet())
		{
			ExistingChannel->SetDefault(DefaultValue.GetValue());
		}
		else
		{
			ExistingChannel->SetDefault(false);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy();
		}
	}
}

void UMovieSceneControlRigParameterSection::AddVector2DParameter(FName InParameterName, TOptional<FVector2D> DefaultValue, bool bReconstructChannel)
{
	FVector2DParameterNameAndCurves* ExistingCurves = nullptr;

	if (!HasVector2DParameter(InParameterName))
	{
		int32 NewIndex = Vector2DParameterNamesAndCurves.Add(FVector2DParameterNameAndCurves(InParameterName));
		ExistingCurves = &Vector2DParameterNamesAndCurves[NewIndex];
		if (DefaultValue.IsSet())
		{
			ExistingCurves->XCurve.SetDefault(DefaultValue.GetValue().X);
			ExistingCurves->YCurve.SetDefault(DefaultValue.GetValue().Y);
		}
		else
		{
			ExistingCurves->XCurve.SetDefault(0.0f);
			ExistingCurves->YCurve.SetDefault(0.0f);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy();
		}
	}
}

void UMovieSceneControlRigParameterSection::AddVectorParameter(FName InParameterName, TOptional<FVector> DefaultValue, bool bReconstructChannel)
{
	FVectorParameterNameAndCurves* ExistingCurves = nullptr;

	if (!HasVectorParameter(InParameterName))
	{
		int32 NewIndex = VectorParameterNamesAndCurves.Add(FVectorParameterNameAndCurves(InParameterName));
		ExistingCurves = &VectorParameterNamesAndCurves[NewIndex];
		if (DefaultValue.IsSet())
		{
			ExistingCurves->XCurve.SetDefault(DefaultValue.GetValue().X);
			ExistingCurves->YCurve.SetDefault(DefaultValue.GetValue().Y);
			ExistingCurves->ZCurve.SetDefault(DefaultValue.GetValue().Z);

		}
		else
		{
			ExistingCurves->XCurve.SetDefault(0.0f);
			ExistingCurves->YCurve.SetDefault(0.0f);
			ExistingCurves->ZCurve.SetDefault(0.0f);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy();
		}
	}
}

void UMovieSceneControlRigParameterSection::AddColorParameter(FName InParameterName, TOptional<FLinearColor> DefaultValue, bool bReconstructChannel)
{
	FColorParameterNameAndCurves* ExistingCurves = nullptr;

	if (!HasColorParameter(InParameterName))
	{
		int32 NewIndex = ColorParameterNamesAndCurves.Add(FColorParameterNameAndCurves(InParameterName));
		ExistingCurves = &ColorParameterNamesAndCurves[NewIndex];
		if (DefaultValue.IsSet())
		{
			ExistingCurves->RedCurve.SetDefault(DefaultValue.GetValue().R);
			ExistingCurves->GreenCurve.SetDefault(DefaultValue.GetValue().G);
			ExistingCurves->BlueCurve.SetDefault(DefaultValue.GetValue().B);
			ExistingCurves->AlphaCurve.SetDefault(DefaultValue.GetValue().A);
		}
		else
		{
			ExistingCurves->RedCurve.SetDefault(0.0f);
			ExistingCurves->GreenCurve.SetDefault(0.0f);
			ExistingCurves->BlueCurve.SetDefault(0.0f);
			ExistingCurves->AlphaCurve.SetDefault(0.0f);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy();
		}
	}
}

void UMovieSceneControlRigParameterSection::AddTransformParameter(FName InParameterName, TOptional<FEulerTransform> DefaultValue, bool bReconstructChannel)
{
	FTransformParameterNameAndCurves* ExistingCurves = nullptr;

	if (!HasTransformParameter(InParameterName))
	{
		int32 NewIndex = TransformParameterNamesAndCurves.Add(FTransformParameterNameAndCurves(InParameterName));
		ExistingCurves = &TransformParameterNamesAndCurves[NewIndex];
		if (DefaultValue.IsSet())
		{
			FEulerTransform& InValue = DefaultValue.GetValue();
			const FVector& Translation = InValue.GetLocation();
			const FRotator& Rotator = InValue.Rotator();
			const FVector& Scale = InValue.GetScale3D();
			ExistingCurves->Translation[0].SetDefault(Translation[0]);
			ExistingCurves->Translation[1].SetDefault(Translation[1]);
			ExistingCurves->Translation[2].SetDefault(Translation[2]);

			ExistingCurves->Rotation[0].SetDefault(Rotator.Roll);
			ExistingCurves->Rotation[1].SetDefault(Rotator.Pitch);
			ExistingCurves->Rotation[2].SetDefault(Rotator.Yaw);

			ExistingCurves->Scale[0].SetDefault(Scale[0]);
			ExistingCurves->Scale[1].SetDefault(Scale[1]);
			ExistingCurves->Scale[2].SetDefault(Scale[2]);

		}
		else if (GetBlendType() == EMovieSceneBlendType::Additive)
		{
			ExistingCurves->Translation[0].SetDefault(0.0f);
			ExistingCurves->Translation[1].SetDefault(0.0f);
			ExistingCurves->Translation[2].SetDefault(0.0f);

			ExistingCurves->Rotation[0].SetDefault(0.0f);
			ExistingCurves->Rotation[1].SetDefault(0.0f);
			ExistingCurves->Rotation[2].SetDefault(0.0f);

			ExistingCurves->Scale[0].SetDefault(0.0f);
			ExistingCurves->Scale[1].SetDefault(0.0f);
			ExistingCurves->Scale[2].SetDefault(0.0f);
		}
		if(bReconstructChannel)
		{
			ReconstructChannelProxy();
		}
	}
}

// only allow creation of space channels onto non-parented Controls
bool UMovieSceneControlRigParameterSection::CanCreateSpaceChannel(FName InControlName) const
{
	if (const FChannelMapInfo* ChannelInfo = ControlChannelMap.Find(InControlName))
	{
		if (ChannelInfo->ParentControlIndex == INDEX_NONE)
		{
			return true;
		}
	}
	return false;
}

void UMovieSceneControlRigParameterSection::AddSpaceChannel(FName InControlName, bool bReconstructChannel)
{
	//only add it if it's the first section since we can't blend them
	if (UMovieSceneControlRigParameterTrack* Track = GetTypedOuter<UMovieSceneControlRigParameterTrack>())
	{
		const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
		if (Sections[0] == this)
		{
			
			if (CanCreateSpaceChannel(InControlName) && !HasSpaceChannel(InControlName))
			{
				SpaceChannels.Add(FSpaceControlNameAndChannel(InControlName));
				if (OnSpaceChannelAdded.IsBound())
				{
					FSpaceControlNameAndChannel& NameAndChannel = SpaceChannels[SpaceChannels.Num() - 1];
					OnSpaceChannelAdded.Broadcast(this, InControlName, &NameAndChannel.SpaceCurve);
				}
			}
			if (bReconstructChannel)
			{
				ReconstructChannelProxy();
			}
		}
	}
}

bool UMovieSceneControlRigParameterSection::HasConstraintChannel(const FGuid& InGuid) const
{
	return ConstraintsChannels.ContainsByPredicate( [InGuid](const FConstraintAndActiveChannel& InChannel)
	{
		return InChannel.GetConstraint().Get() ? InChannel.GetConstraint()->ConstraintID == InGuid : false;
	});
}

FConstraintAndActiveChannel* UMovieSceneControlRigParameterSection::GetConstraintChannel(const FGuid& InConstraintID)
{
	const int32 Index = ConstraintsChannels.IndexOfByPredicate([InConstraintID](const FConstraintAndActiveChannel& InChannel)
		{
			return InChannel.GetConstraint().Get() ? InChannel.GetConstraint()->ConstraintID == InConstraintID : false;
		});
	return (Index != INDEX_NONE) ? &ConstraintsChannels[Index] : nullptr;	
}

void UMovieSceneControlRigParameterSection::ReplaceConstraint(const FName InConstraintName, UTickableConstraint* InConstraint) 
{
	const int32 Index = ConstraintsChannels.IndexOfByPredicate([InConstraintName](const FConstraintAndActiveChannel& InChannel)
	{
		return InChannel.GetConstraint().Get() ? InChannel.GetConstraint()->GetFName() == InConstraintName : false;
	});
	if (Index != INDEX_NONE)
	{
		Modify();
		ConstraintsChannels[Index].SetConstraint(InConstraint);
		ReconstructChannelProxy();
	}
}

void UMovieSceneControlRigParameterSection::OnConstraintsChanged()
{
	ReconstructChannelProxy();
}

void UMovieSceneControlRigParameterSection::AddConstraintChannel(UTickableConstraint* InConstraint)
{
	if (InConstraint && !HasConstraintChannel(InConstraint->ConstraintID))
	{
		Modify();
		
		const int32 NewIndex = ConstraintsChannels.Add(FConstraintAndActiveChannel(InConstraint));

		FMovieSceneConstraintChannel* ExistingChannel = &ConstraintsChannels[NewIndex].ActiveChannel;
		ExistingChannel->SetDefault(false);
		
		//make copy that we can spawn if it doesn't exist
		//the rename changes the outer to this section (from any actor manager)
		InConstraint->Rename(nullptr, this, REN_DontCreateRedirectors);
		
		if (OnConstraintChannelAdded.IsBound())
		{
			OnConstraintChannelAdded.Broadcast(this, ExistingChannel);
		}
		//todo got rid of the if(bReconstructChannel) flag since it was always true but it may need to be false from undo, in which case we need to change
		//change this virtual functions signature
		ReconstructChannelProxy();
	}
}

void UMovieSceneControlRigParameterSection::RemoveConstraintChannel(const UTickableConstraint* InConstraint)
{
	if (bDoNotRemoveChannel == true)
	{
		return;
	}
	const int32 Index = ConstraintsChannels.IndexOfByPredicate([InConstraint](const FConstraintAndActiveChannel& InChannel)
	{
		return InChannel.GetConstraint().Get() ? InChannel.GetConstraint().Get() == InConstraint : false;
	});

	if (ConstraintsChannels.IsValidIndex(Index))
	{
		Modify();
		ConstraintsChannels.RemoveAt(Index);
		ReconstructChannelProxy();
	}
}

TArray<FConstraintAndActiveChannel>& UMovieSceneControlRigParameterSection::GetConstraintsChannels() 
{
	return ConstraintsChannels;
}

const TArray<FConstraintAndActiveChannel>& UMovieSceneControlRigParameterSection::GetConstraintsChannels() const
{
	return ConstraintsChannels;
}

const FName& UMovieSceneControlRigParameterSection::FindControlNameFromConstraintChannel(
	const FMovieSceneConstraintChannel* InConstraintChannel) const
{
	const int32 Index = ConstraintsChannels.IndexOfByPredicate([InConstraintChannel](const FConstraintAndActiveChannel& InChannel)
	{
		return &(InChannel.ActiveChannel) == InConstraintChannel;
	});
	
	if (Index != INDEX_NONE)
	{
		// look for info referencing that constraint index
		using NameInfoIterator = TMap<FName, FChannelMapInfo>::TRangedForConstIterator;
		for (NameInfoIterator It = ControlChannelMap.begin(); It; ++It)
		{
			const FChannelMapInfo& Info = It->Value;
			if (Info.ConstraintsIndex.Contains(Index))
			{
				return It->Key;
			}
		}
	}

	static const FName DummyName = NAME_None;
	return DummyName;
}

TArray<FSpaceControlNameAndChannel>& UMovieSceneControlRigParameterSection::GetSpaceChannels()
{
	return SpaceChannels;
}
const TArray< FSpaceControlNameAndChannel>& UMovieSceneControlRigParameterSection::GetSpaceChannels() const
{
	return SpaceChannels;
}

bool UMovieSceneControlRigParameterSection::IsDifferentThanLastControlsUsedToReconstruct(const TArray<FRigControlElement*>& NewControls) const
{
	if (NewControls.Num() != LastControlsUsedToReconstruct.Num())
	{
		return true;
	}
	for (int32 Index = 0; Index < LastControlsUsedToReconstruct.Num(); ++Index)
	{
		//for the channel proxy we really just care about name and type, and if any are nullptr's
		if (LastControlsUsedToReconstruct[Index].Key != NewControls[Index]->GetFName() ||
			LastControlsUsedToReconstruct[Index].Value != NewControls[Index]->Settings.ControlType)
		{
			return true;
		}
	}
	return false;
}

void UMovieSceneControlRigParameterSection::StoreLastControlsUsedToReconstruct(const TArray<FRigControlElement*>& NewControls)
{
	LastControlsUsedToReconstruct.SetNum(NewControls.Num());
	for (int32 Index = 0; Index < LastControlsUsedToReconstruct.Num(); ++Index)
	{
		if (NewControls[Index])
		{
			LastControlsUsedToReconstruct[Index].Key = NewControls[Index]->GetFName();
			LastControlsUsedToReconstruct[Index].Value = NewControls[Index]->Settings.ControlType;
		}
	}
}

void UMovieSceneControlRigParameterSection::ReconstructChannelProxy()
{
	ChannelProxy.Reset();
	BroadcastChanged();
}

EMovieSceneChannelProxyType UMovieSceneControlRigParameterSection::CacheChannelProxy()
{
	const FName UIMin("UIMin");
	const FName UIMax("UIMax");

	FMovieSceneChannelProxyData Channels;
	ControlChannelMap.Empty();

	// Need to create the channels in sorted orders, only if we have controls
	if (ControlRig)
	{
		TArray<FRigControlElement*> SortedControls;
		ControlRig->GetControlsInOrder(SortedControls);
		StoreLastControlsUsedToReconstruct(SortedControls);
		if (SortedControls.Num() > 0)
		{
			int32 ControlIndex = 0;
			int32 MaskIndex = 0;
			int32 SortOrder = 1; //start with one so Weight is first 
			int32 FloatChannelIndex = 0;
			int32 BoolChannelIndex = 0;
			int32 EnumChannelIndex = 0;
			int32 IntegerChannelIndex = 0;
			int32 SpaceChannelIndex = 0;
			int32 CategoryIndex = 0;
			int32 ConstraintsChannelIndex = 0;

			const FName BoolChannelTypeName = FMovieSceneBoolChannel::StaticStruct()->GetFName();
			const FName EnumChannelTypeName = FMovieSceneByteChannel::StaticStruct()->GetFName();
			const FName IntegerChannelTypeName = FMovieSceneIntegerChannel::StaticStruct()->GetFName();
			const FName SpaceName = FName(TEXT("Space"));

			// begin constraints
		// begin constraints
			auto AddConstrainChannels = [this, &ConstraintsChannelIndex, &SortOrder, &Channels](
				const FName& InControlName, const FText& InGroup, const bool bEnabled)
				{

					const FConstraintsManagerController& Controller = FConstraintsManagerController::Get(ControlRig->GetWorld());

					static constexpr bool bSorted = true;
					const uint32 ControlHash = UTransformableControlHandle::ComputeHash(ControlRig.Get(), InControlName);
					TArray<TWeakObjectPtr<UTickableConstraint>> Constraints = Controller.GetParentConstraints(ControlHash, bSorted);
					for (const TWeakObjectPtr<UTickableConstraint>& Constraint : Constraints)
					{
						if (Constraint.IsValid())
						{
							const FGuid& ConstraintID = Constraint->ConstraintID;
							if (FConstraintAndActiveChannel* ConstraintChannel = GetConstraintChannel(ConstraintID))
							{
								if (FChannelMapInfo* ChannelInfo = ControlChannelMap.Find(InControlName))
								{
									ChannelInfo->ConstraintsIndex.Add(ConstraintsChannelIndex);
								}

#if WITH_EDITOR
								ConstraintChannel->ActiveChannel.ExtraLabel = [Constraint]
									{
										if (Constraint.IsValid())
										{
											FString ParentStr; Constraint->GetLabel().Split(TEXT("."), &ParentStr, nullptr);
											if (!ParentStr.IsEmpty())
											{
												return ParentStr;
											}
										}
										static const FString DummyStr;
										return DummyStr;
									};

								const FText DisplayText = FText::FromString(Constraint->GetTypeLabel());
								FMovieSceneChannelMetaData MetaData(Constraint->GetFName(), DisplayText, InGroup, bEnabled);
								ConstraintsChannelIndex += 1;
								MetaData.SortOrder = SortOrder++;
								MetaData.bCanCollapseToTrack = true;

								Channels.Add(ConstraintChannel->ActiveChannel, MetaData, TMovieSceneExternalValue<bool>());
#else
								Channels.Add(ConstraintChannel->ActiveChannel);
#endif
							}
						}
					}
				};
			// end constraints

#if WITH_EDITOR
		// masking for per control channels based on control filters
			auto MaybeApplyChannelMask = [](FMovieSceneChannelMetaData& OutMetadata, const FRigControlElement* ControlElement, ERigControlTransformChannel InChannel)
				{
					if (!OutMetadata.bEnabled)
					{
						return;
					}

					const TArray<ERigControlTransformChannel>& FilteredChannels = ControlElement->Settings.FilteredChannels;
					if (!FilteredChannels.IsEmpty())
					{
						OutMetadata.bEnabled = FilteredChannels.Contains(InChannel);
					}
				};
#endif

			URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
			for (FRigControlElement* ControlElement : SortedControls)
			{
				if (!Hierarchy->IsAnimatable(ControlElement))
				{
					continue;
				}

				FName ParentControlName = NAME_None;
				FText Group;

				if (Hierarchy->ShouldBeGrouped(ControlElement))
				{
					if (const FRigControlElement* ParentControlElement = Cast<FRigControlElement>(Hierarchy->GetFirstParent(ControlElement)))
					{
						ParentControlName = ParentControlElement->GetFName();
						Group = Hierarchy->GetDisplayNameForUI(ParentControlElement);
					}
				}

				bool bEnabled = GetControlNameMask(ControlElement->GetFName());

#if WITH_EDITOR
				switch (ControlElement->Settings.ControlType)
				{
				case ERigControlType::Float:
				case ERigControlType::ScaleFloat:
				{
					for (FScalarParameterNameAndCurve& Scalar : GetScalarParameterNamesAndCurves())
					{
						if (ControlElement->GetFName() == Scalar.ParameterName)
						{
							if (Group.IsEmpty())
							{
								ControlChannelMap.Add(Scalar.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, INDEX_NONE, NAME_None, MaskIndex, CategoryIndex));
								Group = Hierarchy->GetDisplayNameForUI(ControlElement);
								if (bEnabled)
								{
									++CategoryIndex;
								}
							}
							else
							{
								const FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ParentControlName);
								const int32 ParentControlIndex = pChannelIndex ? pChannelIndex->ControlIndex : INDEX_NONE;
								ControlChannelMap.Add(Scalar.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, ParentControlIndex, NAME_None, MaskIndex, CategoryIndex));
							}

							FParameterFloatChannelEditorData EditorData(ControlRig, Scalar.ParameterName, bEnabled, Group, SortOrder);
							EditorData.MetaData.DisplayText = Hierarchy->GetDisplayNameForUI(ControlElement);
							EditorData.MetaData.PropertyMetaData.Add(UIMin, FString::SanitizeFloat(ControlElement->Settings.MinimumValue.Get<float>()));
							EditorData.MetaData.PropertyMetaData.Add(UIMax, FString::SanitizeFloat(ControlElement->Settings.MaximumValue.Get<float>()));
							Channels.Add(Scalar.ParameterCurve, EditorData.MetaData, EditorData.ExternalValues);
							FloatChannelIndex += 1;
							SortOrder += 1;
							ControlIndex += 1;
							break;
						}
					}
					break;
				}
				case ERigControlType::Bool:
				{
					for (FBoolParameterNameAndCurve& Bool : GetBoolParameterNamesAndCurves())
					{
						if (ControlElement->GetFName() == Bool.ParameterName)
						{
							if (Group.IsEmpty())
							{
								ControlChannelMap.Add(Bool.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, BoolChannelIndex, INDEX_NONE, BoolChannelTypeName, MaskIndex, CategoryIndex));
								Group = Hierarchy->GetDisplayNameForUI(ControlElement);
								if (bEnabled)
								{
									++CategoryIndex;
								}
							}
							else
							{
								const FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ParentControlName);
								const int32 ParentControlIndex = pChannelIndex ? pChannelIndex->ControlIndex : INDEX_NONE;
								ControlChannelMap.Add(Bool.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, BoolChannelIndex, ParentControlIndex, BoolChannelTypeName, MaskIndex, CategoryIndex));
							}

							FMovieSceneChannelMetaData MetaData(Bool.ParameterName, Group, Group, bEnabled);
							MetaData.DisplayText = Hierarchy->GetDisplayNameForUI(ControlElement);
							MetaData.SortOrder = SortOrder++;
							BoolChannelIndex += 1;
							ControlIndex += 1;
							// Prevent single channels from collapsing to the track node
							MetaData.bCanCollapseToTrack = true;
							Channels.Add(Bool.ParameterCurve, MetaData, TMovieSceneExternalValue<bool>());
							break;
						}
					}
					break;
				}
				case ERigControlType::Integer:
				{
					if (ControlElement->Settings.ControlEnum)
					{
						for (FEnumParameterNameAndCurve& Enum : GetEnumParameterNamesAndCurves())
						{
							if (ControlElement->GetFName() == Enum.ParameterName)
							{
								if (Group.IsEmpty())
								{
									ControlChannelMap.Add(Enum.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, EnumChannelIndex, INDEX_NONE, EnumChannelTypeName, MaskIndex, CategoryIndex));
									Group = Hierarchy->GetDisplayNameForUI(ControlElement);
									if (bEnabled)
									{
										++CategoryIndex;
									}
								}
								else
								{
									const FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ParentControlName);
									const int32 ParentControlIndex = pChannelIndex ? pChannelIndex->ControlIndex : INDEX_NONE;
									ControlChannelMap.Add(Enum.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, EnumChannelIndex, ParentControlIndex, EnumChannelTypeName, MaskIndex, CategoryIndex));
								}

								FMovieSceneChannelMetaData MetaData(Enum.ParameterName, Group, Group, bEnabled);
								MetaData.DisplayText = Hierarchy->GetDisplayNameForUI(ControlElement);
								EnumChannelIndex += 1;
								ControlIndex += 1;
								MetaData.SortOrder = SortOrder++;
								// Prevent single channels from collapsing to the track node
								MetaData.bCanCollapseToTrack = true;
								Channels.Add(Enum.ParameterCurve, MetaData, TMovieSceneExternalValue<uint8>());
								break;
							}
						}
					}
					else
					{
						for (FIntegerParameterNameAndCurve& Integer : GetIntegerParameterNamesAndCurves())
						{
							if (ControlElement->GetFName() == Integer.ParameterName)
							{
								if (Group.IsEmpty())
								{
									ControlChannelMap.Add(Integer.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, IntegerChannelIndex, INDEX_NONE, IntegerChannelTypeName, MaskIndex, CategoryIndex));
									Group = Hierarchy->GetDisplayNameForUI(ControlElement);
									if (bEnabled)
									{
										++CategoryIndex;
									}
								}
								else
								{
									const FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ParentControlName);
									const int32 ParentControlIndex = pChannelIndex ? pChannelIndex->ControlIndex : INDEX_NONE;
									ControlChannelMap.Add(Integer.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, IntegerChannelIndex, ParentControlIndex, IntegerChannelTypeName, MaskIndex, CategoryIndex));
								}

								FMovieSceneChannelMetaData MetaData(Integer.ParameterName, Group, Group, bEnabled);
								MetaData.DisplayText = Hierarchy->GetDisplayNameForUI(ControlElement);
								IntegerChannelIndex += 1;
								ControlIndex += 1;
								MetaData.SortOrder = SortOrder++;
								// Prevent single channels from collapsing to the track node
								MetaData.bCanCollapseToTrack = true;
								MetaData.PropertyMetaData.Add(UIMin, FString::FromInt(ControlElement->Settings.MinimumValue.Get<int32>()));
								MetaData.PropertyMetaData.Add(UIMax, FString::FromInt(ControlElement->Settings.MaximumValue.Get<int32>()));
								Channels.Add(Integer.ParameterCurve, MetaData, TMovieSceneExternalValue<int32>());
								break;
							}
						}

					}
					break;
				}
				case ERigControlType::Vector2D:
				{
					for (FVector2DParameterNameAndCurves& Vector2D : GetVector2DParameterNamesAndCurves())
					{
						if (ControlElement->GetFName() == Vector2D.ParameterName)
						{
							if (Group.IsEmpty())
							{
								ControlChannelMap.Add(Vector2D.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, INDEX_NONE, NAME_None, MaskIndex, CategoryIndex));
								if (bEnabled)
								{
									++CategoryIndex;
								}
								Group = Hierarchy->GetDisplayNameForUI(ControlElement);
							}
							else
							{
								const FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ParentControlName);
								const int32 ParentControlIndex = pChannelIndex ? pChannelIndex->ControlIndex : INDEX_NONE;
								ControlChannelMap.Add(Vector2D.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, ParentControlIndex, NAME_None, MaskIndex, CategoryIndex));
							}
							FParameterVectorChannelEditorData EditorData(ControlRig, Vector2D.ParameterName, bEnabled, Group, SortOrder, 2);
							MaybeApplyChannelMask(EditorData.MetaData[0], ControlElement, ERigControlTransformChannel::TranslationX);
							MaybeApplyChannelMask(EditorData.MetaData[1], ControlElement, ERigControlTransformChannel::TranslationY);
							Channels.Add(Vector2D.XCurve, EditorData.MetaData[0], EditorData.ExternalValues[0]);
							Channels.Add(Vector2D.YCurve, EditorData.MetaData[1], EditorData.ExternalValues[1]);
							FloatChannelIndex += 2;
							SortOrder += 2;
							ControlIndex += 1;
							break;
						}
					}
					break;
				}
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
				{
					for (FVectorParameterNameAndCurves& Vector : GetVectorParameterNamesAndCurves())
					{
						if (ControlElement->GetFName() == Vector.ParameterName)
						{
							if (Group.IsEmpty())
							{
								ControlChannelMap.Add(Vector.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, INDEX_NONE, NAME_None, MaskIndex, CategoryIndex));
								if (bEnabled)
								{
									++CategoryIndex;
								}
								Group = Hierarchy->GetDisplayNameForUI(ControlElement);
							}
							else
							{
								const FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ParentControlName);
								const int32 ParentControlIndex = pChannelIndex ? pChannelIndex->ControlIndex : INDEX_NONE;
								ControlChannelMap.Add(Vector.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, ParentControlIndex, NAME_None, MaskIndex, CategoryIndex));
							}
							if (FSpaceControlNameAndChannel* SpaceChannel = GetSpaceChannel(Vector.ParameterName))
							{

								FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(Vector.ParameterName);
								if (pChannelIndex)
								{
									pChannelIndex->bDoesHaveSpace = true;
									pChannelIndex->SpaceChannelIndex = SpaceChannelIndex;
								}

								FString TotalName = Vector.ParameterName.ToString(); //need ControlName.Space for selection to work.
								FString SpaceString = SpaceName.ToString();
								TotalName += ("." + SpaceString);
								FMovieSceneChannelMetaData SpaceMetaData(FName(*TotalName), Group, Group, bEnabled);
								SpaceMetaData.DisplayText = FText::FromName(SpaceName);
								SpaceChannelIndex += 1;
								SpaceMetaData.SortOrder = SortOrder++;
								// Prevent single channels from collapsing to the track node
								SpaceMetaData.bCanCollapseToTrack = true;
								Channels.Add(SpaceChannel->SpaceCurve, SpaceMetaData);
							}


							FParameterVectorChannelEditorData EditorData(ControlRig, Vector.ParameterName, bEnabled, Group, SortOrder, 3);

							if (ControlElement->Settings.ControlType == ERigControlType::Position)
							{
								MaybeApplyChannelMask(EditorData.MetaData[0], ControlElement, ERigControlTransformChannel::TranslationX);
								MaybeApplyChannelMask(EditorData.MetaData[1], ControlElement, ERigControlTransformChannel::TranslationY);
								MaybeApplyChannelMask(EditorData.MetaData[2], ControlElement, ERigControlTransformChannel::TranslationZ);
							}
							else if (ControlElement->Settings.ControlType == ERigControlType::Rotator)
							{
								MaybeApplyChannelMask(EditorData.MetaData[0], ControlElement, ERigControlTransformChannel::Pitch);
								MaybeApplyChannelMask(EditorData.MetaData[1], ControlElement, ERigControlTransformChannel::Yaw);
								MaybeApplyChannelMask(EditorData.MetaData[2], ControlElement, ERigControlTransformChannel::Roll);
							}
							else if (ControlElement->Settings.ControlType == ERigControlType::Scale)
							{
								MaybeApplyChannelMask(EditorData.MetaData[0], ControlElement, ERigControlTransformChannel::ScaleX);
								MaybeApplyChannelMask(EditorData.MetaData[1], ControlElement, ERigControlTransformChannel::ScaleY);
								MaybeApplyChannelMask(EditorData.MetaData[2], ControlElement, ERigControlTransformChannel::ScaleZ);
							}

							Channels.Add(Vector.XCurve, EditorData.MetaData[0], EditorData.ExternalValues[0]);
							Channels.Add(Vector.YCurve, EditorData.MetaData[1], EditorData.ExternalValues[1]);
							Channels.Add(Vector.ZCurve, EditorData.MetaData[2], EditorData.ExternalValues[2]);
							FloatChannelIndex += 3;
							SortOrder += 3;
							ControlIndex += 1;
							break;
						}
					}
					break;
				}

				case ERigControlType::TransformNoScale:
				case ERigControlType::Transform:
				case ERigControlType::EulerTransform:
				{
					for (FTransformParameterNameAndCurves& Transform : GetTransformParameterNamesAndCurves())
					{
						if (ControlElement->GetFName() == Transform.ParameterName)
						{
							const FName ControlName = ControlElement->GetFName();
							if (Group.IsEmpty())
							{
								ControlChannelMap.Add(Transform.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, INDEX_NONE, NAME_None, MaskIndex, CategoryIndex));
								if (bEnabled)
								{
									++CategoryIndex;
								}
								Group = Hierarchy->GetDisplayNameForUI(ControlElement);
							}
							else
							{
								const FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ParentControlName);
								const int32 ParentControlIndex = pChannelIndex ? pChannelIndex->ControlIndex : INDEX_NONE;
								ControlChannelMap.Add(Transform.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, ParentControlIndex, NAME_None, MaskIndex, CategoryIndex));
							}

							// constraints
							AddConstrainChannels(ControlName, Group, bEnabled);

							// spaces
							if (FSpaceControlNameAndChannel* SpaceChannel = GetSpaceChannel(Transform.ParameterName))
							{
								FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(Transform.ParameterName);
								if (pChannelIndex)
								{
									pChannelIndex->bDoesHaveSpace = true;
									pChannelIndex->SpaceChannelIndex = SpaceChannelIndex;
								}

								FString TotalName = Transform.ParameterName.ToString(); //need ControlName.Space for selection to work.
								FString SpaceString = SpaceName.ToString();
								TotalName += ("." + SpaceString);
								FMovieSceneChannelMetaData SpaceMetaData(FName(*TotalName), Group, Group, bEnabled);
								SpaceMetaData.DisplayText = FText::FromName(SpaceName);
								SpaceChannelIndex += 1;
								SpaceMetaData.SortOrder = SortOrder++;
								// Prevent single channels from collapsing to the track node
								SpaceMetaData.bCanCollapseToTrack = true;
								//TMovieSceneExternalValue<FMovieSceneControlRigSpaceBaseKey> ExternalData;
								Channels.Add(SpaceChannel->SpaceCurve, SpaceMetaData);
							}


							FParameterTransformChannelEditorData EditorData(ControlRig, Transform.ParameterName, bEnabled, TransformMask.GetChannels(), Group,
								SortOrder);

							MaybeApplyChannelMask(EditorData.MetaData[0], ControlElement, ERigControlTransformChannel::TranslationX);
							MaybeApplyChannelMask(EditorData.MetaData[1], ControlElement, ERigControlTransformChannel::TranslationY);
							MaybeApplyChannelMask(EditorData.MetaData[2], ControlElement, ERigControlTransformChannel::TranslationZ);

							Channels.Add(Transform.Translation[0], EditorData.MetaData[0], EditorData.ExternalValues[0]);
							Channels.Add(Transform.Translation[1], EditorData.MetaData[1], EditorData.ExternalValues[1]);
							Channels.Add(Transform.Translation[2], EditorData.MetaData[2], EditorData.ExternalValues[2]);

							// note the order here is different from the rotator
							MaybeApplyChannelMask(EditorData.MetaData[3], ControlElement, ERigControlTransformChannel::Roll);
							MaybeApplyChannelMask(EditorData.MetaData[4], ControlElement, ERigControlTransformChannel::Pitch);
							MaybeApplyChannelMask(EditorData.MetaData[5], ControlElement, ERigControlTransformChannel::Yaw);

							Channels.Add(Transform.Rotation[0], EditorData.MetaData[3], EditorData.ExternalValues[3]);
							Channels.Add(Transform.Rotation[1], EditorData.MetaData[4], EditorData.ExternalValues[4]);
							Channels.Add(Transform.Rotation[2], EditorData.MetaData[5], EditorData.ExternalValues[5]);

							if (ControlElement->Settings.ControlType == ERigControlType::Transform ||
								ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
							{
								MaybeApplyChannelMask(EditorData.MetaData[6], ControlElement, ERigControlTransformChannel::ScaleX);
								MaybeApplyChannelMask(EditorData.MetaData[7], ControlElement, ERigControlTransformChannel::ScaleY);
								MaybeApplyChannelMask(EditorData.MetaData[8], ControlElement, ERigControlTransformChannel::ScaleZ);

								Channels.Add(Transform.Scale[0], EditorData.MetaData[6], EditorData.ExternalValues[6]);
								Channels.Add(Transform.Scale[1], EditorData.MetaData[7], EditorData.ExternalValues[7]);
								Channels.Add(Transform.Scale[2], EditorData.MetaData[8], EditorData.ExternalValues[8]);
								FloatChannelIndex += 9;
								SortOrder += 9;

							}
							else
							{
								FloatChannelIndex += 6;
								SortOrder += 6;

							}
							ControlIndex += 1;
							break;
						}
					}
				}
				default:
					break;
				}
#else
				switch (ControlElement->Settings.ControlType)
				{
				case ERigControlType::Float:
				{
					for (FScalarParameterNameAndCurve& Scalar : GetScalarParameterNamesAndCurves())
					{
						if (ControlElement->GetFName() == Scalar.ParameterName)
						{
							ControlChannelMap.Add(Scalar.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, INDEX_NONE, NAME_None, MaskIndex));
							Channels.Add(Scalar.ParameterCurve);
							FloatChannelIndex += 1;
							SortOrder += 1;
							ControlIndex += 1;
							break;
						}
					}
					break;
				}
				case ERigControlType::Bool:
				{
					for (FBoolParameterNameAndCurve& Bool : GetBoolParameterNamesAndCurves())
					{
						if (ControlElement->GetFName() == Bool.ParameterName)
						{
							ControlChannelMap.Add(Bool.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, BoolChannelIndex, INDEX_NONE, NAME_None, MaskIndex));
							Channels.Add(Bool.ParameterCurve);
							BoolChannelIndex += 1;
							SortOrder += 1;
							ControlIndex += 1;
							break;
						}
					}
					break;
				}
				case ERigControlType::Integer:
				{
					if (ControlElement->Settings.ControlEnum)
					{
						for (FEnumParameterNameAndCurve& Enum : GetEnumParameterNamesAndCurves())
						{
							if (ControlElement->GetFName() == Enum.ParameterName)
							{
								ControlChannelMap.Add(Enum.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, EnumChannelIndex, INDEX_NONE, NAME_None, MaskIndex));
								Channels.Add(Enum.ParameterCurve);
								EnumChannelIndex += 1;
								SortOrder += 1;
								ControlIndex += 1;
								break;
							}
						}
					}
					else
					{
						for (FIntegerParameterNameAndCurve& Integer : GetIntegerParameterNamesAndCurves())
						{
							if (ControlElement->GetFName() == Integer.ParameterName)
							{
								ControlChannelMap.Add(Integer.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, IntegerChannelIndex, INDEX_NONE, NAME_None, MaskIndex));
								Channels.Add(Integer.ParameterCurve);
								IntegerChannelIndex += 1;
								SortOrder += 1;
								ControlIndex += 1;
								break;
							}
						}
					}
					break;
				}
				case ERigControlType::Vector2D:
				{
					for (FVector2DParameterNameAndCurves& Vector2D : GetVector2DParameterNamesAndCurves())
					{
						if (ControlElement->GetFName() == Vector2D.ParameterName)
						{
							ControlChannelMap.Add(Vector2D.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, INDEX_NONE, NAME_None, MaskIndex));
							Channels.Add(Vector2D.XCurve);
							Channels.Add(Vector2D.YCurve);
							FloatChannelIndex += 2;
							SortOrder += 2;
							ControlIndex += 1;
							break;
						}
					}
					break;
				}
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
				{
					for (FVectorParameterNameAndCurves& Vector : GetVectorParameterNamesAndCurves())
					{
						if (ControlElement->GetFName() == Vector.ParameterName)
						{
							ControlChannelMap.Add(Vector.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, INDEX_NONE, NAME_None, MaskIndex));
							bool bDoSpaceChannel = true;
							if (bDoSpaceChannel)
							{
								if (FSpaceControlNameAndChannel* SpaceChannel = GetSpaceChannel(Vector.ParameterName))
								{
									FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(Vector.ParameterName);
									if (pChannelIndex)
									{
										pChannelIndex->bDoesHaveSpace = true;
										pChannelIndex->SpaceChannelIndex = SpaceChannelIndex;
									}
									SpaceChannelIndex += 1;
									Channels.Add(SpaceChannel->SpaceCurve);
								}
							}

							Channels.Add(Vector.XCurve);
							Channels.Add(Vector.YCurve);
							Channels.Add(Vector.ZCurve);
							FloatChannelIndex += 3;
							SortOrder += 3;
							ControlIndex += 1;
							break;
						}
					}
					break;
				}
				/*
				for (FColorParameterNameAndCurves& Color : GetColorParameterNamesAndCurves())
				{
					Channels.Add(Color.RedCurve);
					Channels.Add(Color.GreenCurve);
					Channels.Add(Color.BlueCurve);
					Channels.Add(Color.AlphaCurve);
					break
				}
				*/
				case ERigControlType::TransformNoScale:
				case ERigControlType::Transform:
				case ERigControlType::EulerTransform:
				{
					for (FTransformParameterNameAndCurves& Transform : GetTransformParameterNamesAndCurves())
					{
						if (ControlElement->GetFName() == Transform.ParameterName)
						{
							ControlChannelMap.Add(Transform.ParameterName, FChannelMapInfo(ControlIndex, SortOrder, FloatChannelIndex, INDEX_NONE, NAME_None, MaskIndex));

							bool bDoSpaceChannel = true;
							if (bDoSpaceChannel)
							{
								if (FSpaceControlNameAndChannel* SpaceChannel = GetSpaceChannel(Transform.ParameterName))
								{

									FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(Transform.ParameterName);
									if (pChannelIndex)
									{
										pChannelIndex->bDoesHaveSpace = true;
										pChannelIndex->SpaceChannelIndex = SpaceChannelIndex;
									}
									SpaceChannelIndex += 1;
									Channels.Add(SpaceChannel->SpaceCurve);
								}
							}

							Channels.Add(Transform.Translation[0]);
							Channels.Add(Transform.Translation[1]);
							Channels.Add(Transform.Translation[2]);

							Channels.Add(Transform.Rotation[0]);
							Channels.Add(Transform.Rotation[1]);
							Channels.Add(Transform.Rotation[2]);

							if (ControlElement->Settings.ControlType == ERigControlType::Transform || ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
							{
								Channels.Add(Transform.Scale[0]);
								Channels.Add(Transform.Scale[1]);
								Channels.Add(Transform.Scale[2]);
								FloatChannelIndex += 9;
								SortOrder += 9;
							}
							else
							{
								FloatChannelIndex += 6;
								SortOrder += 6;
							}

							ControlIndex += 1;
							break;
					}
				}
					break;
				}
			}
#endif
				++MaskIndex;

		}

#if WITH_EDITOR
			FMovieSceneChannelMetaData      MetaData;
			MetaData.SetIdentifiers("Weight", NSLOCTEXT("MovieSceneTransformSection", "Weight", "Weight"));
			MetaData.bEnabled = EnumHasAllFlags(TransformMask.GetChannels(), EMovieSceneTransformChannel::Weight);
			MetaData.SortOrder = 0;
			MetaData.bSortEmptyGroupsLast = false;
			MetaData.bCanCollapseToTrack = true;
			TMovieSceneExternalValue<float> ExVal;
			Channels.Add(Weight, MetaData, ExVal);
#else
			Channels.Add(Weight);

#endif
		}
	}

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
	
	return EMovieSceneChannelProxyType::Dynamic;
}

FMovieSceneInterrogationKey UMovieSceneControlRigParameterSection::GetFloatInterrogationKey()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FMovieSceneInterrogationKey UMovieSceneControlRigParameterSection::GetVector2DInterrogationKey()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FMovieSceneInterrogationKey UMovieSceneControlRigParameterSection::GetVectorInterrogationKey()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FMovieSceneInterrogationKey UMovieSceneControlRigParameterSection::GetVector4InterrogationKey()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FMovieSceneInterrogationKey UMovieSceneControlRigParameterSection::GetTransformInterrogationKey()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

float UMovieSceneControlRigParameterSection::GetTotalWeightValue(FFrameTime InTime) const
{
	float WeightVal = EvaluateEasing(InTime);
	if (EnumHasAllFlags(TransformMask.GetChannels(), EMovieSceneTransformChannel::Weight))
	{
		float ManualWeightVal = 1.f;
		Weight.Evaluate(InTime, ManualWeightVal);
		WeightVal *= ManualWeightVal;
	}
	return WeightVal;
}

void UMovieSceneControlRigParameterSection::KeyZeroValue(FFrameNumber InFrame, EMovieSceneKeyInterpolation DefaultInterpolation, bool bSelectedControls)
{
	TArray<FName> SelectedControls;
	if (bSelectedControls && ControlRig)
	{
		SelectedControls = ControlRig->CurrentControlSelection();
	}
	/* Don't set zero values on these doesn't make sense
	
	for (FBoolParameterNameAndCurve& Bool : GetBoolParameterNamesAndCurves())
	for (FEnumParameterNameAndCurve& Enum : GetEnumParameterNamesAndCurves())
	for (FIntegerParameterNameAndCurve& Integer : GetIntegerParameterNamesAndCurves())

	*/
	for (FScalarParameterNameAndCurve& Scalar : GetScalarParameterNamesAndCurves())
	{
		if (SelectedControls.Num() == 0 || SelectedControls.Contains(Scalar.ParameterName))
		{
			AddKeyToChannel(&Scalar.ParameterCurve, InFrame, 0.0f, DefaultInterpolation);
			Scalar.ParameterCurve.AutoSetTangents();
		}
	}
	for (FVector2DParameterNameAndCurves& Vector2D : GetVector2DParameterNamesAndCurves())
	{
		if (SelectedControls.Num() == 0 || SelectedControls.Contains(Vector2D.ParameterName))
		{
			AddKeyToChannel(&Vector2D.XCurve, InFrame, 0.0f, DefaultInterpolation);
			Vector2D.XCurve.AutoSetTangents();
			AddKeyToChannel(&Vector2D.YCurve, InFrame, 0.0f, DefaultInterpolation);
			Vector2D.YCurve.AutoSetTangents();
		}
	}
	for (FVectorParameterNameAndCurves& Vector : GetVectorParameterNamesAndCurves())
	{
		if (SelectedControls.Num() == 0 || SelectedControls.Contains(Vector.ParameterName))
		{
			AddKeyToChannel(&Vector.XCurve, InFrame, 0.0f, DefaultInterpolation);
			Vector.XCurve.AutoSetTangents();
			AddKeyToChannel(&Vector.YCurve, InFrame, 0.0f, DefaultInterpolation);
			Vector.YCurve.AutoSetTangents();
			AddKeyToChannel(&Vector.ZCurve, InFrame, 0.0f, DefaultInterpolation);
			Vector.ZCurve.AutoSetTangents();
		}
	}
	for (FTransformParameterNameAndCurves& Transform : GetTransformParameterNamesAndCurves())
	{
		if (SelectedControls.Num() == 0 || SelectedControls.Contains(Transform.ParameterName))
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AddKeyToChannel(&Transform.Translation[Index], InFrame, 0.0f, DefaultInterpolation);
				Transform.Translation[Index].AutoSetTangents();
				AddKeyToChannel(&Transform.Rotation[Index], InFrame, 0.0f, DefaultInterpolation);
				Transform.Rotation[Index].AutoSetTangents();
				if (GetBlendType() == EMovieSceneBlendType::Additive)
				{
					AddKeyToChannel(&Transform.Scale[Index], InFrame, 0.0f, DefaultInterpolation);
				}
				else
				{
					AddKeyToChannel(&Transform.Scale[Index], InFrame, 1.0f, DefaultInterpolation);
				}
				Transform.Scale[Index].AutoSetTangents();

			}
		}
	}
}

void UMovieSceneControlRigParameterSection::KeyWeightValue(FFrameNumber InFrame, EMovieSceneKeyInterpolation DefaultInterpolation, float InVal)
{
	AddKeyToChannel(&Weight, InFrame, InVal, DefaultInterpolation);
	Weight.AutoSetTangents();
}

void UMovieSceneControlRigParameterSection::RecreateWithThisControlRig(UControlRig* InControlRig, bool bSetDefault)
{
	bool bSameControlRig = (ControlRig == InControlRig);
	SetControlRig(InControlRig);
	/* Don't delete old tracks but eventually show that they aren't associated.. but
	then how to delete?
	BoolParameterNamesAndCurves.Empty();
	EnumParameterNamesAndCurves.Empty();
	IntegerParameterNamesAndCurves.Empty();
	ScalarParameterNamesAndCurves.Empty();
	Vector2DParameterNamesAndCurves.Empty();
	VectorParameterNamesAndCurves.Empty();
	ColorParameterNamesAndCurves.Empty();
	TransformParameterNamesAndCurves.Empty();
	*/

	//update the mask array to the new mask name set
	//need to do it here since we won't get controls until here
	const int32 NumControls = ControlRig->AvailableControls().Num();
	const int32 MaskNum = ControlsMask.Num();
	if (NumControls > 0 && NumControls == MaskNum)
	{
		ConvertMaskArrayToNameSet();
	}
	/*
	//if we had the same with same number of controls keep the mask otherwise reset it.

	if (!bSameControlRig || (NumControls > 0  && (NumControls != MaskNum)))
	{
		TArray<bool> OnArray;
		OnArray.Init(true, ControlRig->AvailableControls().Num());
		SetControlsMask(OnArray);
	}
	*/
	
	TArray<FRigControlElement*> SortedControls;
	ControlRig->GetControlsInOrder(SortedControls);

	TMap<FName, FName> CurveControlNameRemapping;
	URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
	if (GetLinkerCustomVersion(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::FKControlNamingScheme)
	{
		for (FRigControlElement* ControlElement : SortedControls)
		{
			if (ControlElement->Settings.ControlType == ERigControlType::Float)
			{
				const FName TargetCurveName = UFKControlRig::GetControlTargetName(ControlElement->GetFName(), ERigElementType::Curve);
				const FRigElementKey CurveKey = FRigElementKey(TargetCurveName, ERigElementType::Curve);
				// Ensure name is valid, and curve actually exists in the hierarchy,
				// this means we could not be renaming some controls for which the curves do not exist anymore, which ties back to comment at the top op the function
				// with regards to non-associated curves
				if (TargetCurveName != NAME_None && Hierarchy->Find(CurveKey))
				{
					// Add mapping from old to new control naming scheme (previous was using uniform naming for both bones and curves)
					CurveControlNameRemapping.Add(ControlElement->GetFName(), UFKControlRig::GetControlName(TargetCurveName, ERigElementType::Bone));
				}
			}
		}
	}

	for (FRigControlElement* ControlElement : SortedControls)
	{
		if (!Hierarchy->IsAnimatable(ControlElement))
		{
			continue;
		}

		FName PreviousName = Hierarchy->GetPreviousName(ControlElement->GetKey());
		if (PreviousName != NAME_None && PreviousName != ControlElement->GetKey().Name)
		{
			RenameParameterName(PreviousName, ControlElement->GetKey().Name);
		}

		if (const FName* OldCurveControlName = CurveControlNameRemapping.Find(ControlElement->GetFName()))
		{
			RenameParameterName(*OldCurveControlName, ControlElement->GetKey().Name);
		}
		
		switch (ControlElement->Settings.ControlType)
		{
		case ERigControlType::Float:
		case ERigControlType::ScaleFloat:
		{
			TOptional<float> DefaultValue;
			if (bSetDefault)
			{
				//or use IntialValue?
				DefaultValue = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<float>();
			}
			AddScalarParameter(ControlElement->GetFName(), DefaultValue, false);
			break;
		}
		case ERigControlType::Bool:
		{
			TOptional<bool> DefaultValue;
			//only add bools,int, enums and space onto first sections, which is the same as the default one
			if (bSetDefault)
			{
				DefaultValue = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<bool>();
				AddBoolParameter(ControlElement->GetFName(), DefaultValue, false);

			}
			break;
		}
		case ERigControlType::Integer:
		{
			if (ControlElement->Settings.ControlEnum)
			{
				TOptional<uint8> DefaultValue;
				//only add bools,int, enums and space onto first sections, which is the same as the default one
				if (bSetDefault)
				{
					DefaultValue = (uint8)ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<int32>();
					AddEnumParameter(ControlElement->GetFName(), ControlElement->Settings.ControlEnum, DefaultValue, false);
				}
			}
			else
			{
				TOptional<int32> DefaultValue;
				//only add bools,int, enums and space onto first sections, which is the same as the default one
				if (bSetDefault)
				{
					DefaultValue = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<int32>();
					AddIntegerParameter(ControlElement->GetFName(), DefaultValue, false);
				}
			}
			break;
		}
		case ERigControlType::Vector2D:
		{
			TOptional<FVector2D> DefaultValue;
			if (bSetDefault)
			{
				//or use IntialValue?
				const FVector3f TempValue = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
				DefaultValue = FVector2D(TempValue.X, TempValue.Y);
			}
			AddVector2DParameter(ControlElement->GetFName(), DefaultValue, false);
			break;
		}

		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
		{
			TOptional<FVector> DefaultValue;
			if (bSetDefault)
			{
				//or use IntialValue?
				DefaultValue = (FVector)ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
			}
			AddVectorParameter(ControlElement->GetFName(), DefaultValue, false);
			//mz todo specify rotator special so we can do quat interps
			break;
		}
		case ERigControlType::EulerTransform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::Transform:
		{
			TOptional<FEulerTransform> DefaultValue;
			if (bSetDefault)
			{
				if (ControlElement->Settings.ControlType == ERigControlType::Transform)
				{
					DefaultValue = FEulerTransform(
						ControlRig->
						GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransform_Float>().ToTransform());
				}
				else if (ControlElement->Settings.ControlType == ERigControlType::EulerTransform)

				{
					FEulerTransform Euler = 
						ControlRig->
						GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FEulerTransform_Float>().ToTransform();
					DefaultValue = Euler;
				}
				else
				{
					FTransformNoScale NoScale = 
						ControlRig->
						GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransformNoScale_Float>().ToTransform();
					DefaultValue = FEulerTransform(NoScale.Rotation.Rotator(), NoScale.Location, FVector::OneVector);
				}
			}
			AddTransformParameter(ControlElement->GetFName(), DefaultValue, false);
			break;
		}

		default:
			break;
		}
	}
	ReconstructChannelProxy();
}

void UMovieSceneControlRigParameterSection::SetControlRig(UControlRig* InControlRig)
{
	ControlRig = InControlRig;
	ControlRigClass = ControlRig ? ControlRig->GetClass() : nullptr;
}

void UMovieSceneControlRigParameterSection::ChangeControlRotationOrder(const FName& InControlName, const  TOptional<EEulerRotationOrder>& OldOrder,
	const  TOptional<EEulerRotationOrder>& NewOrder, EMovieSceneKeyInterpolation Interpolation)
{
	FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(InControlName);
	if (pChannelIndex == nullptr || GetControlRig() == nullptr)
	{
		return;
	}
	int32 ChannelIndex = pChannelIndex->ChannelIndex;
	TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (FRigControlElement* ControlElement = GetControlRig()->FindControl(InControlName))
	{
		if (ControlElement->Settings.ControlType == ERigControlType::Rotator ||
			ControlElement->Settings.ControlType == ERigControlType::EulerTransform ||
			ControlElement->Settings.ControlType == ERigControlType::Transform ||
			ControlElement->Settings.ControlType == ERigControlType::TransformNoScale)
		{

			auto AddArrayToSortedMap = [](const TArray<FFrameNumber>& InFrames, TSortedMap<FFrameNumber, FFrameNumber>& OutFrameMap)
			{
				for (const FFrameNumber& Frame : InFrames)
				{
					OutFrameMap.Add(Frame, Frame);
				}
			};

			const int32 StartIndex = (ControlElement->Settings.ControlType == ERigControlType::Rotator) ? 0 : 3;
			const int32 XIndex = StartIndex + ChannelIndex;
			const int32 YIndex = XIndex + 1;
			const int32 ZIndex = XIndex + 2;

			TSortedMap<FFrameNumber, FFrameNumber> AllKeys;
			TArray<FFrameNumber> KeyTimes;
			TArray<FKeyHandle> Handles;
			for (int32 Index = XIndex; Index < XIndex + 3; ++Index)
			{
				KeyTimes.SetNum(0);
				Handles.SetNum(0);
				FloatChannels[Index]->GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
				AddArrayToSortedMap(KeyTimes, AllKeys);
			}
			KeyTimes.SetNum(0);
			AllKeys.GenerateKeyArray(KeyTimes);
			if (KeyTimes.Num() <= 0)
			{
				//no keys so bail
				return;
			}
			FRotator Rotator(0.0f, 0.0f, 0.0f);
			const FFrameNumber StartFrame = KeyTimes[0];
			const FFrameNumber EndFrame = KeyTimes[KeyTimes.Num() - 1];
			for (const FFrameNumber& Frame : KeyTimes)
			{
				float Roll = 0.0f, Pitch = 0.0f, Yaw = 0.0f;
				FloatChannels[XIndex]->Evaluate(Frame, Roll);
				FloatChannels[YIndex]->Evaluate(Frame, Pitch);
				FloatChannels[ZIndex]->Evaluate(Frame, Yaw);
				Rotator = FRotator(Pitch, Yaw, Roll);
				FQuat Quat;
				//if set use animation core conversion, else use rotator conversion
				if (OldOrder.IsSet())
				{
					FVector Vector = Rotator.Euler();
					Quat = AnimationCore::QuatFromEuler(Vector, OldOrder.GetValue(),true);
				}
				else
				{
					Quat = FQuat(Rotator);
				}
				if (NewOrder.IsSet())
				{
					FVector Vector = AnimationCore::EulerFromQuat(Quat, NewOrder.GetValue(), true);
					Rotator = FRotator::MakeFromEuler(Vector);
				}
				else
				{
					Rotator = FRotator(Quat);
				}
				//this will reuse tangent like we want and only add if new
				AddKeyToChannel(FloatChannels[XIndex], Frame, Rotator.Roll, Interpolation);
				AddKeyToChannel(FloatChannels[YIndex], Frame, Rotator.Pitch, Interpolation);
				AddKeyToChannel(FloatChannels[ZIndex], Frame, Rotator.Yaw, Interpolation);
			}
			FixRotationWinding(InControlName, StartFrame, EndFrame);
		}
	}
}

void UMovieSceneControlRigParameterSection::ConvertMaskArrayToNameSet()
{
	if (ControlRig && ControlsMask.Num() > 0)
	{
		TArray<FRigControlElement*> SortedControls;
		ControlRig->GetControlsInOrder(SortedControls);
		if (SortedControls.Num() == ControlsMask.Num())
		{
			ControlNameMask.Empty();
			for (int32 Index = 0; Index < SortedControls.Num(); ++Index)
			{
				if (ControlsMask[Index] == false)
				{
					ControlNameMask.Add(SortedControls[Index]->GetKey().Name);
				}
			}
		}
		//empty ControlsMask, no longer needed
		ControlsMask.Empty();
	}
}

void UMovieSceneControlRigParameterSection::FillControlNameMask(bool bValue)
{
	if (ControlRig)
	{
		ControlNameMask.Empty();
		if (bValue == false)
		{
			TArray<FRigControlElement*> SortedControls;
			ControlRig->GetControlsInOrder(SortedControls);
			for (FRigControlElement* ControlElement : SortedControls)
			{
				if (ControlElement)
				{
					ControlNameMask.Add(ControlElement->GetKey().Name);
				}
			}
		}
		ReconstructChannelProxy();
	}
}

void UMovieSceneControlRigParameterSection::SetControlNameMask(const FName& Name, bool bValue)
{
	if (bValue == false)
	{
		ControlNameMask.Add(Name);
	}
	else
	{
		ControlNameMask.Remove(Name);
	}
	ReconstructChannelProxy();
}

//get value, will return false if not found
bool UMovieSceneControlRigParameterSection::GetControlNameMask(const FName& Name) const
{
	return (ControlNameMask.Find(Name) == nullptr);
}

void UMovieSceneControlRigParameterSection::FixRotationWinding(const FName& ControlName, FFrameNumber StartFrame, FFrameNumber EndFrame)
{
	FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ControlName);
	if (pChannelIndex == nullptr || GetControlRig() == nullptr)
	{
		return;
	}
	int32 ChannelIndex = pChannelIndex->ChannelIndex;
	TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (FRigControlElement* ControlElement = GetControlRig()->FindControl(ControlName))
	{
		if (ControlElement->Settings.ControlType == ERigControlType::Rotator  ||
			ControlElement->Settings.ControlType == ERigControlType::EulerTransform ||
			ControlElement->Settings.ControlType == ERigControlType::Transform || 
			ControlElement->Settings.ControlType == ERigControlType::TransformNoScale)
		{
			int32 StartIndex = (ControlElement->Settings.ControlType == ERigControlType::Rotator) ? 0 : 3;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				int32 RealIndex = StartIndex + Index + ChannelIndex;
				int32 NumKeys = FloatChannels[RealIndex]->GetNumKeys();
				bool bDidFrame = false;
				float PrevVal = 0.0f;
				for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
				{
					const FFrameNumber Frame = FloatChannels[RealIndex]->GetData().GetTimes()[KeyIndex];
					if (Frame >= StartFrame && Frame <= EndFrame)
					{
						FMovieSceneFloatValue Val = FloatChannels[RealIndex]->GetData().GetValues()[KeyIndex];
						if (bDidFrame == true)
						{
							FMath::WindRelativeAnglesDegrees(PrevVal, Val.Value);
							FloatChannels[RealIndex]->GetData().GetValues()[KeyIndex].Value = Val.Value;
						}
						else
						{
							bDidFrame = true;
						}
						PrevVal = Val.Value;
					}
					
				}
			}
		}
	}
}

void UMovieSceneControlRigParameterSection::OptimizeSection(const FName& ControlName, const FKeyDataOptimizationParams& Params)
{
	TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	TArrayView<FMovieSceneBoolChannel*> BoolChannels = GetChannelProxy().GetChannels<FMovieSceneBoolChannel>();
	TArrayView<FMovieSceneIntegerChannel*> IntegerChannels = GetChannelProxy().GetChannels<FMovieSceneIntegerChannel>();
	TArrayView<FMovieSceneByteChannel*> EnumChannels = GetChannelProxy().GetChannels<FMovieSceneByteChannel>();
	FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ControlName);
	if (pChannelIndex != nullptr)
	{
		int32 ChannelIndex = pChannelIndex->ChannelIndex;

		if (FRigControlElement* ControlElement = ControlRig->FindControl(ControlName))
		{
			switch (ControlElement->Settings.ControlType)
			{
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
				{
					FloatChannels[ChannelIndex]->Optimize(Params);
					FloatChannels[ChannelIndex + 1]->Optimize(Params);
					FloatChannels[ChannelIndex + 2]->Optimize(Params);
					break;
				}

				case ERigControlType::Transform:
				case ERigControlType::TransformNoScale:
				case ERigControlType::EulerTransform:
				{

					FloatChannels[ChannelIndex]->Optimize(Params);
					FloatChannels[ChannelIndex + 1]->Optimize(Params);
					FloatChannels[ChannelIndex + 2]->Optimize(Params);
					FloatChannels[ChannelIndex + 3]->Optimize(Params);
					FloatChannels[ChannelIndex + 4]->Optimize(Params);
					FloatChannels[ChannelIndex + 5]->Optimize(Params);

					if (ControlElement->Settings.ControlType == ERigControlType::Transform ||
						ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
					{
						FloatChannels[ChannelIndex + 6]->Optimize(Params);
						FloatChannels[ChannelIndex + 7]->Optimize(Params);
						FloatChannels[ChannelIndex + 8]->Optimize(Params);
					}
					break;

				}
				case ERigControlType::Bool:
				{
					BoolChannels[ChannelIndex]->Optimize(Params);
					break;
				}
				case ERigControlType::Integer:
				{
					if (ControlElement->Settings.ControlEnum)
					{
						EnumChannels[ChannelIndex]->Optimize(Params);
					}
					else
					{
						IntegerChannels[ChannelIndex]->Optimize(Params);
					}
					break;
				}
				default:
					break;
			}
		}
	}
}

void UMovieSceneControlRigParameterSection::AutoSetTangents(const FName& ControlName)
{
	TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ControlName);
	if (pChannelIndex != nullptr)
	{
		int32 ChannelIndex = pChannelIndex->ChannelIndex;

		if (FRigControlElement* ControlElement = ControlRig->FindControl(ControlName))
		{
			switch (ControlElement->Settings.ControlType)
			{
			case ERigControlType::Position:
			case ERigControlType::Scale:
			case ERigControlType::Rotator:
			{
				FloatChannels[ChannelIndex]->AutoSetTangents();
				FloatChannels[ChannelIndex + 1]->AutoSetTangents();
				FloatChannels[ChannelIndex + 2]->AutoSetTangents();
				break;
			}

			case ERigControlType::Transform:
			case ERigControlType::TransformNoScale:
			case ERigControlType::EulerTransform:
			{

				FloatChannels[ChannelIndex]->AutoSetTangents();
				FloatChannels[ChannelIndex + 1]->AutoSetTangents();
				FloatChannels[ChannelIndex + 2]->AutoSetTangents();
				FloatChannels[ChannelIndex + 3]->AutoSetTangents();
				FloatChannels[ChannelIndex + 4]->AutoSetTangents();
				FloatChannels[ChannelIndex + 5]->AutoSetTangents();

				if (ControlElement->Settings.ControlType == ERigControlType::Transform ||
					ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
				{
					FloatChannels[ChannelIndex + 6]->AutoSetTangents();
					FloatChannels[ChannelIndex + 7]->AutoSetTangents();
					FloatChannels[ChannelIndex + 8]->AutoSetTangents();
				}
				break;

			}
			default:
				break;
			}
		}
	}
}
#if WITH_EDITOR

void UMovieSceneControlRigParameterSection::RecordControlRigKey(FFrameNumber FrameNumber, bool bSetDefault, EMovieSceneKeyInterpolation InInterpMode)
{
	if (ControlRig)
	{
		TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		TArrayView<FMovieSceneBoolChannel*> BoolChannels = GetChannelProxy().GetChannels<FMovieSceneBoolChannel>();
		TArrayView<FMovieSceneIntegerChannel*> IntChannels = GetChannelProxy().GetChannels<FMovieSceneIntegerChannel>();
		TArrayView<FMovieSceneByteChannel*> EnumChannels = GetChannelProxy().GetChannels<FMovieSceneByteChannel>();

		// Helper lambda to add a FVector key to the float channels
		auto AddVectorKeyToFloatChannels = [&FloatChannels, InInterpMode](int32& ChannelIndex, FFrameNumber FrameNumber, const FVector& Value)
		{
			switch (InInterpMode)
			{
			case EMovieSceneKeyInterpolation::Linear:
				FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Value.X);
				FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Value.Y);
				FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Value.Z);
				break;

			case EMovieSceneKeyInterpolation::Constant:
				FloatChannels[ChannelIndex++]->AddConstantKey(FrameNumber, Value.X);
				FloatChannels[ChannelIndex++]->AddConstantKey(FrameNumber, Value.Y);
				FloatChannels[ChannelIndex++]->AddConstantKey(FrameNumber, Value.Z);
				break;

			case  EMovieSceneKeyInterpolation::Auto:
				FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Value.X, ERichCurveTangentMode::RCTM_Auto);
				FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Value.Y, ERichCurveTangentMode::RCTM_Auto);
				FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Value.Z, ERichCurveTangentMode::RCTM_Auto);
				break;
			case  EMovieSceneKeyInterpolation::SmartAuto:
			default:
				FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Value.X, ERichCurveTangentMode::RCTM_SmartAuto);
				FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Value.Y, ERichCurveTangentMode::RCTM_SmartAuto);
				FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Value.Z, ERichCurveTangentMode::RCTM_SmartAuto);
				break;
			}
		};

		TArray<FRigControlElement*> Controls;
		ControlRig->GetControlsInOrder(Controls);
		
		for (FRigControlElement* ControlElement : Controls)
		{
			if (!ControlRig->GetHierarchy()->IsAnimatable(ControlElement))
			{
				continue;
			}
			FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ControlElement->GetFName());
			if (pChannelIndex == nullptr)
			{
				continue;
			}
			int32 ChannelIndex = pChannelIndex->ChannelIndex;

	
			switch (ControlElement->Settings.ControlType)
			{
				case ERigControlType::Bool:
				{
					bool Val = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<bool>();
					if (bSetDefault)
					{
						BoolChannels[ChannelIndex]->SetDefault(Val);
					}
					BoolChannels[ChannelIndex]->GetData().AddKey(FrameNumber, Val);
					break;
				}
				case ERigControlType::Integer:
				{
					if (ControlElement->Settings.ControlEnum)
					{
						uint8 Val = (uint8)ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<uint8>();
						if (bSetDefault)
						{
							EnumChannels[ChannelIndex]->SetDefault(Val);
						}
						EnumChannels[ChannelIndex]->GetData().AddKey(FrameNumber, Val);
					}
					else
					{
						int32 Val = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<int32>();
						if (bSetDefault)
						{
							IntChannels[ChannelIndex]->SetDefault(Val);
						}
						IntChannels[ChannelIndex]->GetData().AddKey(FrameNumber, Val);
					}
					break;
				}
				case ERigControlType::Float:
				case ERigControlType::ScaleFloat:
				{
					float Val = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<float>();
					if (bSetDefault)
					{
						FloatChannels[ChannelIndex]->SetDefault(Val);
					}

					switch (InInterpMode)
					{
					case EMovieSceneKeyInterpolation::Linear:
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Val);
						break;

					case EMovieSceneKeyInterpolation::Constant:
						FloatChannels[ChannelIndex++]->AddConstantKey(FrameNumber, Val);
						break;

					case EMovieSceneKeyInterpolation::Auto:
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val, ERichCurveTangentMode::RCTM_Auto);
						break;

					case EMovieSceneKeyInterpolation::SmartAuto:
					default:
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val, ERichCurveTangentMode::RCTM_SmartAuto);
						break;
					}

					break;
				}
				case ERigControlType::Vector2D:
				{
					FVector3f Val = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
					if (bSetDefault)
					{
						FloatChannels[ChannelIndex]->SetDefault(Val.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(Val.Y);
					}

					switch (InInterpMode)
					{
					case EMovieSceneKeyInterpolation::Linear:
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Val.X);
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Val.Y);
						break;

					case  EMovieSceneKeyInterpolation::Constant:
						FloatChannels[ChannelIndex++]->AddConstantKey(FrameNumber, Val.X);
						FloatChannels[ChannelIndex++]->AddConstantKey(FrameNumber, Val.Y);
						break;
					case  EMovieSceneKeyInterpolation::Auto:
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val.X, ERichCurveTangentMode::RCTM_Auto);
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val.Y, ERichCurveTangentMode::RCTM_Auto);
						break;
					case EMovieSceneKeyInterpolation::SmartAuto:
					default:
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val.X, ERichCurveTangentMode::RCTM_SmartAuto);
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val.Y, ERichCurveTangentMode::RCTM_SmartAuto);
						break;
					}

					break;
				}
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
				{
					FVector3f Val = (ControlElement->Settings.ControlType == ERigControlType::Rotator)
						? FVector3f(ControlRig->GetHierarchy()->GetControlSpecifiedEulerAngle(ControlElement)): ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
					if (ControlElement->Settings.ControlType == ERigControlType::Rotator &&
						FloatChannels[ChannelIndex]->GetNumKeys() > 0)
					{
						float LastVal = FloatChannels[ChannelIndex]->GetValues()[FloatChannels[ChannelIndex]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, Val.X);
						LastVal = FloatChannels[ChannelIndex + 1]->GetValues()[FloatChannels[ChannelIndex + 1]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, Val.Y);
						LastVal = FloatChannels[ChannelIndex + 2]->GetValues()[FloatChannels[ChannelIndex + 2]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, Val.Z);
					}
					if (bSetDefault)
					{
						FloatChannels[ChannelIndex]->SetDefault(Val.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(Val.Y);
						FloatChannels[ChannelIndex + 2]->SetDefault(Val.Z);
					}

					AddVectorKeyToFloatChannels(ChannelIndex, FrameNumber, FVector(Val));

					break;
				}

				case ERigControlType::Transform:
				case ERigControlType::TransformNoScale:
				case ERigControlType::EulerTransform:
				{
					FTransform Val = FTransform::Identity;
					if (ControlElement->Settings.ControlType == ERigControlType::TransformNoScale)
					{
						FTransformNoScale NoScale = 
							ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransformNoScale_Float>().ToTransform();
						Val = NoScale;
					}
					else if (ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
					{
						FEulerTransform Euler = 
							ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FEulerTransform_Float>().ToTransform();
						Val = Euler.ToFTransform();
					}
					else
					{
						Val = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransform_Float>().ToTransform();
					}
					FVector CurrentVector = Val.GetTranslation();
					if (bSetDefault)
					{
						FloatChannels[ChannelIndex]->SetDefault(CurrentVector.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(CurrentVector.Y);
						FloatChannels[ChannelIndex + 2]->SetDefault(CurrentVector.Z);
					}

					AddVectorKeyToFloatChannels(ChannelIndex, FrameNumber, CurrentVector);
					CurrentVector = ControlRig->GetHierarchy()->GetControlSpecifiedEulerAngle(ControlElement);
					if (FloatChannels[ChannelIndex]->GetNumKeys() > 0)
					{
						float LastVal = FloatChannels[ChannelIndex]->GetValues()[FloatChannels[ChannelIndex]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, CurrentVector.X);
						LastVal = FloatChannels[ChannelIndex + 1]->GetValues()[FloatChannels[ChannelIndex + 1]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, CurrentVector.Y);
						LastVal = FloatChannels[ChannelIndex + 2]->GetValues()[FloatChannels[ChannelIndex + 2]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, CurrentVector.Z);
					}
					if (bSetDefault)
					{
						FloatChannels[ChannelIndex]->SetDefault(CurrentVector.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(CurrentVector.Y);
						FloatChannels[ChannelIndex + 2]->SetDefault(CurrentVector.Z);
					}

					AddVectorKeyToFloatChannels(ChannelIndex, FrameNumber, CurrentVector);

					if (ControlElement->Settings.ControlType == ERigControlType::Transform ||
						ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
					{
						CurrentVector = Val.GetScale3D();
						if (bSetDefault)
						{
							FloatChannels[ChannelIndex]->SetDefault(CurrentVector.X);
							FloatChannels[ChannelIndex + 1]->SetDefault(CurrentVector.Y);
							FloatChannels[ChannelIndex + 2]->SetDefault(CurrentVector.Z);
						}

						AddVectorKeyToFloatChannels(ChannelIndex, FrameNumber, CurrentVector);
					}
					break;
				}
			}
		}
	}
}

bool UMovieSceneControlRigParameterSection::LoadAnimSequenceIntoThisSection(UAnimSequence* AnimSequence, UMovieScene* MovieScene, UObject* BoundObject, bool bKeyReduce, float Tolerance, bool bResetControls, FFrameNumber InStartFrame, EMovieSceneKeyInterpolation InInterpolation)
{
	FFrameNumber SequenceStart = UE::MovieScene::DiscreteInclusiveLower(MovieScene->GetPlaybackRange());
	return LoadAnimSequenceIntoThisSection(AnimSequence, SequenceStart, MovieScene, BoundObject, bKeyReduce, Tolerance, bResetControls, InStartFrame, InInterpolation);
}

bool UMovieSceneControlRigParameterSection::LoadAnimSequenceIntoThisSection(UAnimSequence* AnimSequence, const FFrameNumber& SequenceStart, UMovieScene* MovieScene, UObject* BoundObject, bool bKeyReduce, float Tolerance, bool bResetControls, const FFrameNumber& InStartFrame, EMovieSceneKeyInterpolation InInterpolation)
{
	USkeletalMeshComponent* SkelMeshComp = Cast<USkeletalMeshComponent>(BoundObject);
	
	if (SkelMeshComp != nullptr && (SkelMeshComp->GetSkeletalMeshAsset() == nullptr || SkelMeshComp->GetSkeletalMeshAsset()->GetSkeleton() == nullptr))
	{
		return false;
	}
	
	USkeleton* Skeleton = SkelMeshComp ? SkelMeshComp->GetSkeletalMeshAsset()->GetSkeleton() : Cast<USkeleton>(BoundObject);
	if (Skeleton == nullptr)
	{
		return false;
	}
	UFKControlRig* AutoRig = Cast<UFKControlRig>(ControlRig);
	if (!AutoRig && !ControlRig->SupportsEvent(FRigUnit_InverseExecution::EventName))
	{
		return false;
	}

	TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (FloatChannels.Num() <= 0)
	{
		return false;
	}

	URigHierarchy* SourceHierarchy = ControlRig->GetHierarchy();
	
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	float Length = AnimSequence->GetPlayLength();
	const FFrameRate& FrameRate = AnimSequence->GetSamplingFrameRate();

	FFrameNumber StartFrame = SequenceStart + InStartFrame;
	FFrameNumber EndFrame = TickResolution.AsFrameNumber(Length) + StartFrame;

	Modify();
	if (HasStartFrame() && HasEndFrame())
	{
		StartFrame = GetInclusiveStartFrame();
		EndFrame = StartFrame + EndFrame;
		SetEndFrame(EndFrame);
	}
	ControlRig->Modify();

	const int32 NumberOfKeys = AnimSequence->GetDataModel()->GetNumberOfKeys();
	FFrameNumber FrameRateInFrameNumber = TickResolution.AsFrameNumber(FrameRate.AsInterval());
	int32 ExtraProgress = bKeyReduce ? FloatChannels.Num() : 0;
	
	FScopedSlowTask Progress(NumberOfKeys + ExtraProgress, LOCTEXT("BakingToControlRig_SlowTask", "Baking To Control Rig..."));	
	Progress.MakeDialog(true);

	//Make sure we are reset and run construction event  before evaluating
	/*
	TArray<FRigElementKey>ControlsToReset = ControlRig->GetHierarchy()->GetAllKeys(true, ERigElementType::Control);
	for (const FRigElementKey& ControlToReset : ControlsToReset)
	{
		if (ControlToReset.Type == ERigElementType::Control)
		{
			FRigControlElement* ControlElement = ControlRig->FindControl(ControlToReset.Name);
			if (ControlElement && !ControlElement->Settings.bIsTransientControl)
			{
				const FTransform InitialLocalTransform = ControlRig->GetHierarchy()->GetInitialLocalTransform(ControlToReset);
				ControlRig->GetHierarchy()->SetLocalTransform(ControlToReset, InitialLocalTransform);
			}
		}
	}
	SourceBones.ResetTransforms();
	SourceCurves.ResetValues();
	ControlRig->Execute(TEXT("Setup"));
	*/
	const IAnimationDataModel* DataModel = AnimSequence->GetDataModel();
	const FAnimationCurveData& CurveData = DataModel->GetCurveData();

	// copy the hierarchy from the CDO into the target control rig.
	// this ensures that the topology version matches in case of a dynamic hierarchy
	if (bResetControls)
	{
		if(!ControlRig->GetClass()->IsNative())
		{
			if (UControlRig* CDO = Cast<UControlRig>(ControlRig->GetClass()->GetDefaultObject()))
			{
				SourceHierarchy->CopyHierarchy(CDO->GetHierarchy());
			}
		}
	}

	// now set the hierarchies initial transforms based on the currently used skeletal mesh
	if (SkelMeshComp)
	{
		ControlRig->SetBoneInitialTransformsFromSkeletalMeshComponent(SkelMeshComp, true);
	}
	else
	{
		ControlRig->SetBoneInitialTransformsFromRefSkeleton(Skeleton->GetReferenceSkeleton());
	}
	if (bResetControls)
	{
		ControlRig->RequestConstruction();
		ControlRig->Evaluate_AnyThread();
	}

	for (int32 Index = 0; Index < NumberOfKeys; ++Index)
	{
		const float SequenceSecond = AnimSequence->GetTimeAtFrame(Index);
		const FFrameNumber FrameNumber = StartFrame + (FMath::Max(FrameRateInFrameNumber.Value, 1) * Index);

		if (bResetControls)
		{
			SourceHierarchy->ResetPoseToInitial();
			SourceHierarchy->ResetCurveValues();
		}

		for (const FFloatCurve& Curve : CurveData.FloatCurves)
		{
			const float Val = Curve.FloatCurve.Eval(SequenceSecond);
			SourceHierarchy->SetCurveValue(FRigElementKey(Curve.GetName(), ERigElementType::Curve), Val);
		}

		// retrieve the pose using the services that persona and sequencer rely on
		// rather than accessing the low level raw tracks.
		FAnimPoseEvaluationOptions EvaluationOptions;
		EvaluationOptions.OptionalSkeletalMesh = SkelMeshComp ? SkelMeshComp->GetSkeletalMeshAsset() : nullptr;
		EvaluationOptions.bShouldRetarget = false;
		EvaluationOptions.EvaluationType = EAnimDataEvalType::Raw;

		FAnimPose AnimPose;
		UAnimPoseExtensions::GetAnimPoseAtTime(AnimSequence, SequenceSecond, EvaluationOptions, AnimPose);

		TArray<FName> BoneNames;
		UAnimPoseExtensions::GetBoneNames(AnimPose, BoneNames);
		for(const FName& BoneName : BoneNames)
		{
			if(FRigBoneElement* BoneElement = SourceHierarchy->Find<FRigBoneElement>(FRigElementKey(BoneName, ERigElementType::Bone)))
			{
				FTransform LocalTransform = UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::Local);
				SourceHierarchy->SetLocalTransform(BoneElement->GetIndex(), LocalTransform, true, false);
			}
		}

		if (Index == 0)
		{
			//to make sure the first frame looks good we need to do this first. UE-100069
			ControlRig->Execute(FRigUnit_InverseExecution::EventName);
		}
		ControlRig->Execute(FRigUnit_InverseExecution::EventName);

		RecordControlRigKey(FrameNumber, Index == 0, InInterpolation);
		Progress.EnterProgressFrame(1);
		if (Progress.ShouldCancel())
		{
			return false;
		}
	}

	if (bKeyReduce)
	{
		FKeyDataOptimizationParams Params;
		Params.bAutoSetInterpolation = true;
		Params.Tolerance = Tolerance;
		for (FMovieSceneFloatChannel* Channel : FloatChannels)
		{
			Channel->Optimize(Params); //should also auto tangent
			Progress.EnterProgressFrame(1);
			if (Progress.ShouldCancel())
			{
				return false;
			}
		}

		TArrayView<FMovieSceneBoolChannel*> BoolChannels = GetChannelProxy().GetChannels<FMovieSceneBoolChannel>();
		for (FMovieSceneBoolChannel* Channel : BoolChannels)
		{
			Channel->Optimize(Params);
		}

		TArrayView<FMovieSceneIntegerChannel*> IntegerChannels = GetChannelProxy().GetChannels<FMovieSceneIntegerChannel>();
		for (FMovieSceneIntegerChannel* Channel : IntegerChannels)
		{
			Channel->Optimize(Params);
		}

		TArrayView<FMovieSceneByteChannel*> EnumChannels = GetChannelProxy().GetChannels<FMovieSceneByteChannel>();
		for (FMovieSceneByteChannel* Channel : EnumChannels)
		{
			Channel->Optimize(Params);
		}
	}
	
	return true;
}

#endif


void UMovieSceneControlRigParameterSection::AddEnumParameterKey(FName InParameterName, FFrameNumber InTime, uint8 InValue)
{
	FMovieSceneByteChannel* ExistingChannel = nullptr;
	for (FEnumParameterNameAndCurve& EnumParameterNameAndCurve : EnumParameterNamesAndCurves)
	{
		if (EnumParameterNameAndCurve.ParameterName == InParameterName)
		{
			ExistingChannel = &EnumParameterNameAndCurve.ParameterCurve;
			break;
		}
	}
	if (ExistingChannel == nullptr)
	{
		const int32 NewIndex = EnumParameterNamesAndCurves.Add(FEnumParameterNameAndCurve(InParameterName));
		ExistingChannel = &EnumParameterNamesAndCurves[NewIndex].ParameterCurve;

		ReconstructChannelProxy();
	}

	ExistingChannel->GetData().UpdateOrAddKey(InTime, InValue);

	if (TryModify())
	{
		SetRange(TRange<FFrameNumber>::Hull(TRange<FFrameNumber>(InTime), GetRange()));
	}
}


void UMovieSceneControlRigParameterSection::AddIntegerParameterKey(FName InParameterName, FFrameNumber InTime, int32 InValue)
{
	FMovieSceneIntegerChannel* ExistingChannel = nullptr;
	for (FIntegerParameterNameAndCurve& IntegerParameterNameAndCurve : IntegerParameterNamesAndCurves)
	{
		if (IntegerParameterNameAndCurve.ParameterName == InParameterName)
		{
			ExistingChannel = &IntegerParameterNameAndCurve.ParameterCurve;
			break;
		}
	}
	if (ExistingChannel == nullptr)
	{
		const int32 NewIndex = IntegerParameterNamesAndCurves.Add(FIntegerParameterNameAndCurve(InParameterName));
		ExistingChannel = &IntegerParameterNamesAndCurves[NewIndex].ParameterCurve;

		ReconstructChannelProxy();
	}

	ExistingChannel->GetData().UpdateOrAddKey(InTime, InValue);

	if (TryModify())
	{
		SetRange(TRange<FFrameNumber>::Hull(TRange<FFrameNumber>(InTime), GetRange()));
	}
}

bool UMovieSceneControlRigParameterSection::RemoveEnumParameter(FName InParameterName)
{
	for (int32 i = 0; i < EnumParameterNamesAndCurves.Num(); i++)
	{
		if (EnumParameterNamesAndCurves[i].ParameterName == InParameterName)
		{
			EnumParameterNamesAndCurves.RemoveAt(i);
			ReconstructChannelProxy();
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::RemoveIntegerParameter(FName InParameterName)
{
	for (int32 i = 0; i < IntegerParameterNamesAndCurves.Num(); i++)
	{
		if (IntegerParameterNamesAndCurves[i].ParameterName == InParameterName)
		{
			IntegerParameterNamesAndCurves.RemoveAt(i);
			ReconstructChannelProxy();
			return true;
		}
	}
	return false;
}


TArray<FEnumParameterNameAndCurve>& UMovieSceneControlRigParameterSection::GetEnumParameterNamesAndCurves()
{
	return EnumParameterNamesAndCurves;
}

const TArray<FEnumParameterNameAndCurve>& UMovieSceneControlRigParameterSection::GetEnumParameterNamesAndCurves() const
{
	return EnumParameterNamesAndCurves;
}

TArray<FIntegerParameterNameAndCurve>& UMovieSceneControlRigParameterSection::GetIntegerParameterNamesAndCurves()
{
	return IntegerParameterNamesAndCurves;
}

const TArray<FIntegerParameterNameAndCurve>& UMovieSceneControlRigParameterSection::GetIntegerParameterNamesAndCurves() const
{
	return IntegerParameterNamesAndCurves;
}
/*
void FillControlNameMask(bool bValue)
{
	TArray<FRigControlElement*> SortedControls;
	ControlRig->GetControlsInOrder(SortedControls);
	ControlNameMask.Empty();
	for (FRigControlElement* ControlElement : SortedControls)
	{
		if (ControlElement)
		{
			ControlNameMask.Add(ControlElement->GetKey().Name, bValue);
		}
	}
}

void SetControlNameMask(const FName& Name, bool bValue)
{
	ControlNameMask.Add(Name, bValue);
}

//get value, will return false if not found
bool GetControlNameMask(const FName& Name, bool& OutValue)
{
	bool* bValue = ControlNameMask.Find(Name);
	if (bValue)
	{
		OutValue = *bValue;
		return true;
	}
	return false;
}
*/
void UMovieSceneControlRigParameterSection::ClearAllParameters()
{
	BoolParameterNamesAndCurves.SetNum(0);
	ScalarParameterNamesAndCurves.SetNum(0);
	Vector2DParameterNamesAndCurves.SetNum(0);
	VectorParameterNamesAndCurves.SetNum(0);
	ColorParameterNamesAndCurves.SetNum(0);
	TransformParameterNamesAndCurves.SetNum(0);
	EnumParameterNamesAndCurves.SetNum(0);
	IntegerParameterNamesAndCurves.SetNum(0);
	SpaceChannels.SetNum(0);
	ConstraintsChannels.SetNum(0);
}
void UMovieSceneControlRigParameterSection::RemoveAllKeys(bool bIncludeSpaceKeys)
{
	TArray<FFrameNumber> KeyTimes;
	TArray<FKeyHandle> Handles;
	if (bIncludeSpaceKeys)
	{
		for (FSpaceControlNameAndChannel& Space : SpaceChannels)
		{
			KeyTimes.SetNum(0);
			Handles.SetNum(0);
			Space.SpaceCurve.GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
			Space.SpaceCurve.DeleteKeys(Handles);
		}
	}
	for (FBoolParameterNameAndCurve& Bool : GetBoolParameterNamesAndCurves())
	{
		KeyTimes.SetNum(0);
		Handles.SetNum(0);
		Bool.ParameterCurve.GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
		Bool.ParameterCurve.DeleteKeys(Handles);
	}
	for (FEnumParameterNameAndCurve& Enum : GetEnumParameterNamesAndCurves())
	{
		KeyTimes.SetNum(0);
		Handles.SetNum(0);
		Enum.ParameterCurve.GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
		Enum.ParameterCurve.DeleteKeys(Handles);
	}
	for (FIntegerParameterNameAndCurve& Integer : GetIntegerParameterNamesAndCurves())
	{
		KeyTimes.SetNum(0);
		Handles.SetNum(0);
		Integer.ParameterCurve.GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
		Integer.ParameterCurve.DeleteKeys(Handles);
	}

	for (FScalarParameterNameAndCurve& Scalar : GetScalarParameterNamesAndCurves())
	{
		KeyTimes.SetNum(0);
		Handles.SetNum(0);
		Scalar.ParameterCurve.GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
		Scalar.ParameterCurve.DeleteKeys(Handles);
	}
	for (FVector2DParameterNameAndCurves& Vector2D : GetVector2DParameterNamesAndCurves())
	{
		KeyTimes.SetNum(0);
		Handles.SetNum(0);
		Vector2D.XCurve.GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
		Vector2D.XCurve.DeleteKeys(Handles);
		KeyTimes.SetNum(0);
		Handles.SetNum(0);
		Vector2D.YCurve.GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
		Vector2D.YCurve.DeleteKeys(Handles);
	}
	for (FVectorParameterNameAndCurves& Vector : GetVectorParameterNamesAndCurves())
	{
		KeyTimes.SetNum(0);
		Handles.SetNum(0);
		Vector.XCurve.GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
		Vector.XCurve.DeleteKeys(Handles);
		KeyTimes.SetNum(0);
		Handles.SetNum(0);
		Vector.YCurve.GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
		Vector.YCurve.DeleteKeys(Handles);
		KeyTimes.SetNum(0);
		Handles.SetNum(0);
		Vector.ZCurve.GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
		Vector.ZCurve.DeleteKeys(Handles);
	}
	for (FTransformParameterNameAndCurves& Transform : GetTransformParameterNamesAndCurves())
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			KeyTimes.SetNum(0);
			Handles.SetNum(0);
			Transform.Translation[Index].GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
			Transform.Translation[Index].DeleteKeys(Handles);
			KeyTimes.SetNum(0);
			Handles.SetNum(0);
			Transform.Rotation[Index].GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
			Transform.Rotation[Index].DeleteKeys(Handles);
			KeyTimes.SetNum(0);
			Handles.SetNum(0);
			Transform.Scale[Index].GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
			Transform.Scale[Index].DeleteKeys(Handles);
		}
	}
}

UControlRig* UMovieSceneControlRigParameterSection::GetControlRig(UWorld* InGameWorld) const
{
	if (InGameWorld == nullptr)
	{
		return ControlRig;
	}
	else if (UMovieSceneControlRigParameterTrack* Track = GetTypedOuter<UMovieSceneControlRigParameterTrack>())
	{
		return Track->GetGameWorldControlRig(InGameWorld);
	}
	return nullptr;
}


int32 UMovieSceneControlRigParameterSection::GetActiveCategoryIndex(FName ControlName) const
{
	int32 CategoryIndex = INDEX_NONE;
	const FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(ControlName);
	if (pChannelIndex != nullptr && GetControlNameMask(ControlName))
	{
		CategoryIndex = pChannelIndex->CategoryIndex;
	}
	return CategoryIndex;
}


TOptional<float> UMovieSceneControlRigParameterSection::EvaluateScalarParameter(const  FFrameTime& InTime, FName InParameterName)
{
	TOptional<float> OptValue;	
	if (const FChannelMapInfo* ChannelInfo = ControlChannelMap.Find(InParameterName))
	{
		TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		float Value = 0.0f;
		FloatChannels[ChannelInfo->ChannelIndex]->Evaluate(InTime, Value);
		OptValue = Value;
	}
	return OptValue;
}

TOptional<bool> UMovieSceneControlRigParameterSection::EvaluateBoolParameter(const  FFrameTime& InTime, FName InParameterName)
{
	TOptional<bool> OptValue;
	if (const FChannelMapInfo* ChannelInfo = ControlChannelMap.Find(InParameterName))
	{
		TArrayView<FMovieSceneBoolChannel*> BoolChannels = GetChannelProxy().GetChannels<FMovieSceneBoolChannel>();
		bool Value = false;
		BoolChannels[ChannelInfo->ChannelIndex]->Evaluate(InTime, Value);
		OptValue = Value;
	}
	return OptValue;
}

TOptional<uint8> UMovieSceneControlRigParameterSection::EvaluateEnumParameter(const  FFrameTime& InTime, FName InParameterName)
{
	TOptional<uint8> OptValue;
	if (const FChannelMapInfo* ChannelInfo = ControlChannelMap.Find(InParameterName))
	{
		TArrayView<FMovieSceneByteChannel*> EnumChannels = GetChannelProxy().GetChannels<FMovieSceneByteChannel>();
		uint8 Value = 0;
		EnumChannels[ChannelInfo->ChannelIndex]->Evaluate(InTime, Value);
		OptValue = Value;
	}
	return OptValue;
}

TOptional<int32> UMovieSceneControlRigParameterSection::EvaluateIntegerParameter(const  FFrameTime& InTime, FName InParameterName)
{
	TOptional<int32> OptValue;
	if (const FChannelMapInfo* ChannelInfo = ControlChannelMap.Find(InParameterName))
	{
		TArrayView<FMovieSceneIntegerChannel*> IntChannels = GetChannelProxy().GetChannels<FMovieSceneIntegerChannel>();
		int32 Value = 0;
		IntChannels[ChannelInfo->ChannelIndex]->Evaluate(InTime, Value);
		OptValue = Value;
	}
	return OptValue;
}

TOptional<FVector> UMovieSceneControlRigParameterSection::EvaluateVectorParameter(const FFrameTime& InTime, FName InParameterName)
{
	TOptional<FVector> OptValue;
	if (const FChannelMapInfo* ChannelInfo = ControlChannelMap.Find(InParameterName))
	{
		TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		FVector3f Value(0.0f, 0.0f, 0.0f);
		FloatChannels[ChannelInfo->ChannelIndex]->Evaluate(InTime, Value.X);
		FloatChannels[ChannelInfo->ChannelIndex + 1]->Evaluate(InTime, Value.Y);
		FloatChannels[ChannelInfo->ChannelIndex + 2]->Evaluate(InTime, Value.Z);
		OptValue = (FVector)Value;
	}
	return OptValue;
}

TOptional<FVector2D> UMovieSceneControlRigParameterSection::EvaluateVector2DParameter(const  FFrameTime& InTime, FName InParameterName)
{
	TOptional<FVector2D> OptValue;
	if (const FChannelMapInfo* ChannelInfo = ControlChannelMap.Find(InParameterName))
	{
		TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		FVector2f Value(0.0f, 0.0f);
		FloatChannels[ChannelInfo->ChannelIndex]->Evaluate(InTime, Value.X);
		FloatChannels[ChannelInfo->ChannelIndex + 1]->Evaluate(InTime, Value.Y);
		OptValue = FVector2D(Value);
	}
	return OptValue;
}

TOptional<FLinearColor>UMovieSceneControlRigParameterSection:: EvaluateColorParameter(const  FFrameTime& InTime, FName InParameterName)
{
	TOptional<FLinearColor> OptValue;
	if (const FChannelMapInfo* ChannelInfo = ControlChannelMap.Find(InParameterName))
	{
		TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		FLinearColor Value(0.0f, 0.0f, 0.0f, 1.0f);
		FloatChannels[ChannelInfo->ChannelIndex]->Evaluate(InTime, Value.R);
		FloatChannels[ChannelInfo->ChannelIndex + 1]->Evaluate(InTime, Value.G);		
		FloatChannels[ChannelInfo->ChannelIndex + 2]->Evaluate(InTime, Value.B);
		FloatChannels[ChannelInfo->ChannelIndex + 3]->Evaluate(InTime, Value.A);
		OptValue = Value;
	}
	return OptValue;
}

TOptional<FEulerTransform> UMovieSceneControlRigParameterSection::EvaluateTransformParameter(const  FFrameTime& InTime, FName InParameterName)
{
	TOptional<FEulerTransform> OptValue;
	if (const FChannelMapInfo* ChannelInfo = ControlChannelMap.Find(InParameterName))
	{
		TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		FEulerTransform Value = FEulerTransform::Identity;
		FVector3f Translation(ForceInitToZero), Scale(FVector3f::OneVector);
		FRotator3f Rotator(0.0f, 0.0f, 0.0f);

		FloatChannels[ChannelInfo->ChannelIndex    ]->Evaluate(InTime, Translation.X);
		FloatChannels[ChannelInfo->ChannelIndex + 1]->Evaluate(InTime, Translation.Y);
		FloatChannels[ChannelInfo->ChannelIndex + 2]->Evaluate(InTime, Translation.Z);

		FloatChannels[ChannelInfo->ChannelIndex + 3]->Evaluate(InTime, Rotator.Roll);
		FloatChannels[ChannelInfo->ChannelIndex + 4]->Evaluate(InTime, Rotator.Pitch);
		FloatChannels[ChannelInfo->ChannelIndex + 5]->Evaluate(InTime, Rotator.Yaw);
		if (ControlRig)
		{
			if (FRigControlElement* ControlElement = ControlRig->FindControl(InParameterName))
			{
				if (ControlElement->Settings.ControlType == ERigControlType::Transform ||
					ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
				{
					FloatChannels[ChannelInfo->ChannelIndex + 6]->Evaluate(InTime, Scale.X);
					FloatChannels[ChannelInfo->ChannelIndex + 7]->Evaluate(InTime, Scale.Y);
					FloatChannels[ChannelInfo->ChannelIndex + 8]->Evaluate(InTime, Scale.Z);
				}

			}
		}
		Value = FEulerTransform(FRotator(Rotator), (FVector)Translation, (FVector)Scale);
		OptValue = Value;
	}
	return OptValue;
}

TOptional<FMovieSceneControlRigSpaceBaseKey> UMovieSceneControlRigParameterSection::EvaluateSpaceChannel(const  FFrameTime& InTime, FName InParameterName)
{
	TOptional<FMovieSceneControlRigSpaceBaseKey> OptValue;
	if (FSpaceControlNameAndChannel* Channel = GetSpaceChannel(InParameterName))
	{
		FMovieSceneControlRigSpaceBaseKey Value;
		using namespace UE::MovieScene;
		EvaluateChannel(&(Channel->SpaceCurve),InTime, Value);
		OptValue = Value;
	}
	return OptValue;
}

UObject* UMovieSceneControlRigParameterSection::GetImplicitObjectOwner()
{
	if (GetControlRig())
	{
		return GetControlRig();
	}
	return Super::GetImplicitObjectOwner();
}

FEnumParameterNameAndCurve::FEnumParameterNameAndCurve(FName InParameterName)
{
	ParameterName = InParameterName;
}

FIntegerParameterNameAndCurve::FIntegerParameterNameAndCurve(FName InParameterName)
{
	ParameterName = InParameterName;
}

#undef LOCTEXT_NAMESPACE 


