// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Common/TypedElementHandles.h"

#include "TedsStylingFactory.generated.h"

struct FSlateBrush;
struct FSlateColor;
class ISlateStyle;

UCLASS()
class UTedsStylingFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UTedsStylingFactory() override = default;

	void RegisterTables(IEditorDataStorageProvider& DataStorage) override;
	virtual void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;

	static void RegisterAllKnownStyles();
	
private:
	static void RegisterBrush(IEditorDataStorageProvider* DataStorage, const FName& StyleName, const FSlateBrush* Brush, const ISlateStyle& OwnerStyle);
	static void RegisterColor(IEditorDataStorageProvider* DataStorage, const FName& StyleName, const FSlateColor& Color, const ISlateStyle& OwnerStyle);
	static UE::Editor::DataStorage::RowHandle AddOrGetStyleRow(IEditorDataStorageProvider* DataStorage, const FName& StyleName, const ISlateStyle& OwnerStyle);
};
