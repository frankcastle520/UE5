// Copyright Epic Games, Inc. All Rights Reserved.

#include "Styles/SlateBrushTemplates.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"

const FSlateBrush* FSlateBrushTemplates::DragHandle()
{
	const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("VerticalBoxDragIndicatorShort");
	return Brush;
}

const FSlateBrush* FSlateBrushTemplates::ThinLineHorizontal()
{
	return FAppStyle::GetBrush( "ThinLine.Horizontal");		
}

const FSlateBrush* FSlateBrushTemplates::Transparent()
{
	static const FSlateColorBrush ColorBrush{ FLinearColor::Transparent };
	return &ColorBrush;
}

const FSlateBrush* FSlateBrushTemplates::Panel()
{
	static const FSlateColorBrush ColorBrush{ FStyleColors::Panel };
	return &ColorBrush;
}

const FSlateBrush* FSlateBrushTemplates::Recessed()
{
	static const FSlateColorBrush ColorBrush{ FStyleColors::Recessed };
	return &ColorBrush;
}

const FSlateBrush* FSlateBrushTemplates::GetBrushWithColor(EStyleColor Color)
{
	const FSlateBrush* ColorBrush = EStyleColorToSlateBrushMap.Find( Color );
	
	if ( ColorBrush )
	{
		return ColorBrush;
	}

	const FSlateColorBrush NewColorBrush{ Color };
	EStyleColorToSlateBrushMap.Add( Color, static_cast<const FSlateBrush>( NewColorBrush ) );

	return EStyleColorToSlateBrushMap.Find( Color );;
}

FSlateBrushTemplates& FSlateBrushTemplates::Get()
{
	static FSlateBrushTemplates Templates;
	return Templates;
}
