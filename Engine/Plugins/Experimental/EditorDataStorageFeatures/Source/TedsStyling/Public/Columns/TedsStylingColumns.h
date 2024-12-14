// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

#include "TedsStylingColumns.generated.h"

/**
 * Owner style set
 */
USTRUCT(meta = (DisplayName = "Style Set"))
struct FSlateStyleSetColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY(meta = (Searchable))
	FName StyleSetName;
};

/**
 * Path to style resource on disk
 */
USTRUCT(meta = (DisplayName = "Style Path"))
struct FSlateStylePathColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY(meta = (Searchable))
	FName StylePath;
};

/**
 * Tag on rows that are part of a stylesheet
 */
USTRUCT(meta = (DisplayName = "Slate Style"))
struct FSlateStyleTag final : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

/**
 * Tag on rows that are brushes in the stylesheet
 * TEDS UI TODO: Once TEDS UI supports dynamic columns, we can remove this tag and make FSlateStyleTagDynamic
 */
USTRUCT(meta = (DisplayName = "Slate Brush"))
struct FSlateBrushTag final : public FEditorDataStorageTag
{
	GENERATED_BODY()
};
