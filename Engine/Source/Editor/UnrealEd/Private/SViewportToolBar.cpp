// Copyright Epic Games, Inc. All Rights Reserved.


#include "SViewportToolBar.h"

#include "Internationalization/Internationalization.h"
#include "Styling/AppStyle.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealNames.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "Widgets/Input/SMenuAnchor.h"

struct FSlateBrush;

#define LOCTEXT_NAMESPACE "ViewportToolBar"

void SViewportToolBar::Construct(const FArguments& InArgs)
{
}

TWeakPtr<SMenuAnchor> SViewportToolBar::GetOpenMenu() const
{
	return OpenedMenu;
}

void SViewportToolBar::SetOpenMenu( TSharedPtr< SMenuAnchor >& NewMenu )
{
	if( OpenedMenu.IsValid() && OpenedMenu.Pin() != NewMenu )
	{
		// Close any other open menus
		OpenedMenu.Pin()->SetIsOpen( false );
	}
	OpenedMenu = NewMenu;
}

FText SViewportToolBar::GetCameraMenuLabelFromViewportType(const ELevelViewportType ViewportType) const
{
	return UE::UnrealEd::GetCameraSubmenuLabelFromViewportType(ViewportType);
}

const FSlateBrush* SViewportToolBar::GetCameraMenuLabelIconFromViewportType(const ELevelViewportType ViewportType) const
{
	const FName Icon = UE::UnrealEd::GetCameraSubmenuIconFNameFromViewportType(ViewportType);
	return FAppStyle::GetBrush(Icon);
}

bool SViewportToolBar::IsViewModeSupported(EViewModeIndex ViewModeIndex) const 
{
	switch (ViewModeIndex)
	{
	case VMI_PrimitiveDistanceAccuracy:
	case VMI_MaterialTextureScaleAccuracy:
	case VMI_RequiredTextureResolution:
		return false;
	default:
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
