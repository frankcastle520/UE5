// Copyright Epic Games, Inc. All Rights Reserved.
#include "DetailsViewCategoryHiderCustomization.h"

#include "DetailLayoutBuilder.h"

TSharedRef<IDetailCustomization> FDetailsViewCategoryHiderCustomization::MakeInstance(TArray<FName>&& InCategoriesToHide)
{
	return MakeShared<FDetailsViewCategoryHiderCustomization>(MoveTemp(InCategoriesToHide));
}

TSharedRef<IDetailCustomization> FDetailsViewCategoryHiderCustomization::MakeInstance(const TArrayView<FName>& InCategoriesToHide)
{
	return MakeShared<FDetailsViewCategoryHiderCustomization>(InCategoriesToHide);
}

FDetailsViewCategoryHiderCustomization::FDetailsViewCategoryHiderCustomization(TArray<FName>&& InCategoriesToHide) 
	: CategoriesToHide(MoveTemp(InCategoriesToHide))
{
}

FDetailsViewCategoryHiderCustomization::FDetailsViewCategoryHiderCustomization(const TArrayView<FName>& InCategoriesToHide)
	: CategoriesToHide(InCategoriesToHide)
{
}

void FDetailsViewCategoryHiderCustomization::CustomizeDetails(class IDetailLayoutBuilder& DetailLayoutBuilder)
{
	for (const FName& CategoryName : CategoriesToHide)
	{
		DetailLayoutBuilder.HideCategory(CategoryName);
	}
}
