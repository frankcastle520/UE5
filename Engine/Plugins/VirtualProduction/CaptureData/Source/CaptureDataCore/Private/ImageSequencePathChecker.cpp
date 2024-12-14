// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageSequencePathChecker.h"

#include "CaptureDataLog.h"

#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "ImageSequencePathChecker"

namespace UE::CaptureData
{
static const FText DialogTitle = LOCTEXT("MissingDataTitle", "Missing Data");
static const FTextFormat DialogMessageFormat = LOCTEXT(
	"ImageSequenceMissingDataMessage",

	"{AssetDisplayName} {NumCaptureDataFootageAssets}|plural(one=asset contains,other=assets contain) "
	"{NumInvalidImageSequences}|plural(one=an image sequence which is,other=image sequences which have) missing data.\n\n"
	"See output log for details."
);

FImageSequencePathChecker::FImageSequencePathChecker(FText InAssetDisplayName) :
	NumCaptureDataFootageAssets(0),
	NumInvalidImageSequences(0),
	AssetDisplayName(MoveTemp(InAssetDisplayName))
{
}

void FImageSequencePathChecker::Check(const UFootageCaptureData& InCaptureData)
{
	++NumCaptureDataFootageAssets;

	const TArray<UFootageCaptureData::FPathAssociation> InvalidImageSequences = InCaptureData.CheckImageSequencePaths();
	NumInvalidImageSequences += InvalidImageSequences.Num();

	if (!InvalidImageSequences.IsEmpty())
	{
		UE_LOG(
			LogCaptureDataCore,
			Warning,
			TEXT("%s contains image sequence(s) with missing data (see below): %s"),
			*AssetDisplayName.ToString(),
			*InCaptureData.GetPathName()
		);

		for (const UFootageCaptureData::FPathAssociation& InvalidImageSequence : InvalidImageSequences)
		{
			UE_LOG(
				LogCaptureDataCore,
				Warning,
				TEXT("Image sequence path does not exist or is not a folder: %s (%s)"),
				*InvalidImageSequence.PathOnDisk,
				*InvalidImageSequence.AssetPath
			);
		}
	}
}

void FImageSequencePathChecker::DisplayDialog() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("NumCaptureDataFootageAssets"), NumCaptureDataFootageAssets);
	Args.Add(TEXT("AssetDisplayName"), AssetDisplayName);
	Args.Add(TEXT("NumInvalidImageSequences"), NumInvalidImageSequences);

	const FText DialogMessage = FText::Format(DialogMessageFormat, Args);

	FMessageDialog::Open(EAppMsgType::Ok, DialogMessage, DialogTitle);
}

bool FImageSequencePathChecker::HasError() const
{
	return NumInvalidImageSequences > 0;
}

}

#undef LOCTEXT_NAMESPACE

