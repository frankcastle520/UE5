// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Channels/MovieSceneChannelData.h"
#include "Channels/MovieSceneChannelHandle.h"
#include "MVVM/Selection/Selection.h"

class FAvaSequencer;
class UMovieSceneSection;
struct FAvaEaseCurveTangents;
enum class EAvaEaseCurveToolOperation : uint8;
struct FKeyHandle;

namespace UE::Sequencer
{
	class FChannelModel;
}

struct FAvaEaseCurveKeySelection
{
public:
	struct FChannelData
	{
		TSharedPtr<UE::Sequencer::FChannelModel> ChannelModel;
		TObjectPtr<UMovieSceneSection> Section;
		FMovieSceneChannelHandle Channel;
		TArray<FKeyHandle> KeyHandles;
	};

	FAvaEaseCurveKeySelection() {}
	FAvaEaseCurveKeySelection(const TSharedPtr<FAvaSequencer>& InSequencer);

	void ForEachEaseableKey(const bool bInIncludeEqualValueKeys
		, const TFunctionRef<bool(const FKeyHandle& /*InKeyHandle*/, const FKeyHandle& /*InNextKeyHandle*/, const FChannelData&)>& InCallable);

	template <class ChannelHandle, class ChannelValue>
	bool CheckMatchingValues(const bool bInIncludeEqualValueKeys, const FChannelData& InChannelData
		, const TFunctionRef<bool(const FKeyHandle& /*InKeyHandle*/, const FKeyHandle& /*InNextKeyHandle*/, const FChannelData&)>& InCallable)
	{
		if (!InChannelData.Channel.Get())
		{
			return false;
		}
		
		TMovieSceneChannelHandle<ChannelHandle> Channel = InChannelData.Channel.Cast<ChannelHandle>();
		TMovieSceneChannelData<ChannelValue> ChannelData = Channel.Get()->GetData();

		const TArrayView<ChannelValue> ChannelValues = ChannelData.GetValues();
		const int32 KeyCount = ChannelValues.Num();

		for (const FKeyHandle& KeyHandle : InChannelData.KeyHandles)
		{
			if (KeyHandle == FKeyHandle::Invalid())
			{
				continue;
			}

			const int32 KeyIndex = ChannelData.GetIndex(KeyHandle);
			if (KeyIndex == INDEX_NONE)
			{
				continue;
			}

			// If there is no key after the selected key, we don't need to process.
			// The arrive tangents of this key will be set by the previous key's processing.
			int32 NextKeyIndex = KeyIndex + 1;
			NextKeyIndex = (NextKeyIndex < KeyCount) ? NextKeyIndex : INDEX_NONE;
			if (NextKeyIndex == INDEX_NONE)
			{
				continue;
			}

			// Need to check if the next key index is valid, otherwise GetHandle() will fail.
			const FKeyHandle NextKeyHandle = ChannelData.GetHandle(NextKeyIndex);
			if (NextKeyHandle == FKeyHandle::Invalid())
			{
				continue;
			}

			if (!bInIncludeEqualValueKeys && ChannelValues[KeyIndex].Value == ChannelValues[NextKeyIndex].Value)
			{
				continue;
			}

			if (!InCallable(KeyHandle, NextKeyHandle, InChannelData))
			{
				return false;
			}
		}

		return true;
	}

	FAvaEaseCurveTangents AverageTangents(const FFrameRate& InDisplayRate
		, const FFrameRate& InTickResolution
		, const bool bInAutoFlipTangents);

	void SetTangents(const FAvaEaseCurveTangents& InTangents
		, const EAvaEaseCurveToolOperation InOperation
		, const FFrameRate& InDisplayRate
		, const FFrameRate& InTickResolution
		, const bool bInAutoFlipTangents);

	const TMap<FName, FChannelData>& GetChannelKeyData() const
	{
		return ChannelKeyData;
	}

	int32 GetTotalSelectedKeys() const
	{
		return TotalSelectedKeys;
	}

	/** @return True if there is only one selected key and it is the last key of the channel. */
	bool IsLastOnlySelectedKey() const
	{
		return bIsLastOnlySelectedKey;
	}

	/** @return True if all selected keys are detected to be ease curves (broken, weighted, cubic tangents). */
	bool AreAllEaseCurves() const
	{
		return bAreAllEaseCurves;
	}

protected:
	TMap<FName, FChannelData> ChannelKeyData;

	int32 TotalSelectedKeys = 0;

	bool bIsLastOnlySelectedKey = false;

	bool bAreAllEaseCurves = true;
};
