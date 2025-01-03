// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Styling/ISlateStyle.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Types/SlateStructs.h"
#include "MultiBoxDefs.generated.h"

class SToolTip;
class SWidget;

/**
 * Types of MultiBoxes
 */
UENUM(BlueprintType)
enum class EMultiBoxType : uint8
{
	/** Horizontal menu bar */
	MenuBar,

	/** Horizontal tool bar */
	ToolBar,

	/** Vertical tool bar */
	VerticalToolBar,

	/** Toolbar which is a slim version of the toolbar that aligns an icon and a text element horizontally */
	SlimHorizontalToolBar,

	/** A toolbar that tries to arrange all toolbar items uniformly (supports only horizontal toolbars for now) */
	UniformToolBar,

	/** Vertical menu (pull-down menu, or context menu) */
	Menu,

	/** Buttons arranged in rows, with a maximum number of buttons per row, like a toolbar but can have multiple rows*/
	ButtonRow,

	/** A toolbar with horizontally-oriented buttons that tries to arrange all toolbar items uniformly */
	SlimHorizontalUniformToolBar,
};


/**
 * Types of MultiBlocks
 */
UENUM(BlueprintType)
enum class EMultiBlockType : uint8
{
	None = 0,
	ButtonRow,
	EditableText,
	Heading,
	MenuEntry,
	Separator,
	ToolBarButton,
	ToolBarComboButton,
	Widget,
};


class FMultiBoxSettings
{
public:

	DECLARE_DELEGATE_RetVal_FourParams( TSharedRef< SToolTip >, FConstructToolTip, const TAttribute<FText>& /*ToolTipText*/, const TSharedPtr<SWidget>& /*OverrideContent*/, const TSharedPtr<const FUICommandInfo>& /*Action*/, bool /*ShowActionShortcut*/);

	/** Access to whether multiboxes use small icons or default sized icons */
	static SLATE_API TAttribute<bool> UseSmallToolBarIcons;
	static SLATE_API TAttribute<bool> DisplayMultiboxHooks;
	static SLATE_API FConstructToolTip ToolTipConstructor;
	
	static SLATE_API TAttribute<int> MenuSearchFieldVisibilityThreshold;

	SLATE_API FMultiBoxSettings();

	static SLATE_API TSharedRef< SToolTip > ConstructDefaultToolTip( const TAttribute<FText>& ToolTipText, const TSharedPtr<SWidget>& OverrideContent, const TSharedPtr<const FUICommandInfo>& Action, bool ShowActionShortcut = true );

	static SLATE_API void ResetToolTipConstructor();
};

struct FMultiBoxCustomization
{
	static SLATE_API const FMultiBoxCustomization None;

	static FMultiBoxCustomization AllowCustomization( FName InCustomizationName )
	{
		ensure( InCustomizationName != NAME_None );
		return FMultiBoxCustomization( InCustomizationName );
	}

	FName GetCustomizationName() const { return CustomizationName; }

	FMultiBoxCustomization( FName InCustomizationName )
		: CustomizationName( InCustomizationName )
	{}

private:
	/** The Name of the customization that uniquely identifies the multibox for saving and loading users data*/
	FName CustomizationName;
};

/** 
 * Block location information
 */
namespace EMultiBlockLocation
{
	enum Type
	{
		/** Default, either no other blocks in group or grouping style is disabled */
		None = -1,
		
		/** Denotes the beginning of a group, currently left most first */
		Start,

		/** Denotes a middle block(s) of a group */
		Middle,

		/** Denotes the end of a group, currently the right most */
		End,
	};

	/** returns the passed in style with the addition of the location information */
	static FName ToName(FName StyleName, Type InLocation)
	{
		switch(InLocation)
		{
		case Start:
			return ISlateStyle::Join(StyleName, ".Start");
		case Middle:
			return ISlateStyle::Join(StyleName, ".Middle");
		case End:
			return ISlateStyle::Join(StyleName, ".End");
		}
		return StyleName;
	}
}

/** Contains various Style parameters and overrides. Not all are applicable to a given entry */
struct FMenuEntryStyleParams
{
	FMenuEntryStyleParams() = default;

	/** If true, removes the padding from the left of the widget that lines it up with other menu items */
	bool bNoIndent = false;

	/** Horizontal alignment for this widget in its parent container. Note: only applies to toolbars */
	EHorizontalAlignment HorizontalAlignment = HAlign_Fill;

	/** (Optional) Vertical alignment for this widget in its parent container */
	TOptional<EVerticalAlignment> VerticalAlignment;

	/** (Optionally) override the size rule, where the default is generally Auto */
	TOptional<FSizeParam::ESizeRule> SizeRule;

	/** (Optionally) override the minimum size. This will apply to the width or height, depending on the menu orientation */
	TOptional<float> MinSize;

	/** (Optionally) override the maximum size. This will apply to the width or height, depending on the menu orientation */
	TOptional<float> MaxSize;
};
