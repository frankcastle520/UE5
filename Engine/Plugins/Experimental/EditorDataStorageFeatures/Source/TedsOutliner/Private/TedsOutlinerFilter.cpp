// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOutlinerFilter.h"

#include "TedsOutlinerImpl.h"

namespace UE::Editor::Outliner
{
FTedsOutlinerFilter::FTedsOutlinerFilter(const FName& InFilterName, const FText& InFilterDisplayName,
	TSharedPtr<FFilterCategory> InCategory, TSharedRef<FTedsOutlinerImpl> InTedsOutlinerImpl,
	const UE::Editor::DataStorage::FQueryDescription& InFilterQuery)
	: FFilterBase(InCategory)
	, FilterName(InFilterName)
	, FilterDisplayName(InFilterDisplayName)
	, TedsOutlinerImpl(InTedsOutlinerImpl)
	, FilterQuery(InFilterQuery)
{
	
}

FString FTedsOutlinerFilter::GetName() const
{
	return FilterName.ToString();
}

FText FTedsOutlinerFilter::GetDisplayName() const
{
	return FilterDisplayName;
}

FText FTedsOutlinerFilter::GetToolTipText() const
{
	return FText::FromName(FilterName);
}

FLinearColor FTedsOutlinerFilter::GetColor() const
{
	return FLinearColor();	
}

FName FTedsOutlinerFilter::GetIconName() const
{
	return FName();
}

bool FTedsOutlinerFilter::IsInverseFilter() const
{
	return false;
}

void FTedsOutlinerFilter::ActiveStateChanged(bool bActive)
{
	if(bActive)
	{
		TedsOutlinerImpl->AddExternalQuery(FilterName, FilterQuery);
	}
	else
	{
		TedsOutlinerImpl->RemoveExternalQuery(FilterName);
	}
}

void FTedsOutlinerFilter::ModifyContextMenu(FMenuBuilder& MenuBuilder)
{
	
}

void FTedsOutlinerFilter::SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const
{
	
}

void FTedsOutlinerFilter::LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString)
{
	
}

bool FTedsOutlinerFilter::PassesFilter(SceneOutliner::FilterBarType InItem) const
{
	// If this item is not compatible with the owning Table Viewer - it does not pass any filter queries
	// If it is compatible, this is simply a dummy filter for the UI while the actual filter is applied through the TEDS query
	if(TedsOutlinerImpl->IsItemCompatible().IsBound())
	{
		return TedsOutlinerImpl->IsItemCompatible().Execute(InItem);
	}

	// The filter is applied through a TEDS query and this is just a dummy to activate it, so we can simply return true otherwise
	return false;
}
} // namespace UE::Editor::Outliner
