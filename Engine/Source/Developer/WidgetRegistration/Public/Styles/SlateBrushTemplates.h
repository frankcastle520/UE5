// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/StyleColors.h"

/** A const FSlateBrush* Factory */
struct WIDGETREGISTRATION_API FSlateBrushTemplates
{
	// Images
	static const FSlateBrush* DragHandle();
	static const FSlateBrush* ThinLineHorizontal();

	// Colors
	static const FSlateBrush* Transparent();
	static const FSlateBrush* Panel();
	static const FSlateBrush* Recessed();

	/**
	 * gets a const FSlateBrush* with the color Color
	 *
	 *  @param the EStyleColor we need the slate brush for
	 */
	const FSlateBrush* GetBrushWithColor(EStyleColor Color);

	/**
	 * gets the FSlateBrushTemplates singleton
	 */
	static FSlateBrushTemplates& Get();

	/** the map of EStyleColor to const FSlateBrush */
	TMap<EStyleColor, const FSlateBrush> EStyleColorToSlateBrushMap;
};
