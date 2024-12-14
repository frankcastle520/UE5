// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNodeModifierBaseDetails.h"
#include "IDetailCustomization.h"
#include "UObject/ObjectMacros.h"

enum class ECheckBoxState : uint8;
namespace ESelectInfo { enum Type : int; }

class FString;
class IDetailLayoutBuilder;
class IPropertyHandle;


class FCustomizableObjectNodeModifierClipMorphDetails : public FCustomizableObjectNodeModifierBaseDetails
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** ILayoutDetails interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

private:
	class UCustomizableObjectNodeModifierClipMorph* Node;
	class IDetailLayoutBuilder* DetailBuilderPtr = nullptr;
	TArray< TSharedPtr<FString> > BoneComboOptions;

	UPROPERTY()
	class USkeletalMesh* SkeletalMesh;

	void OnBoneComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> BoneProperty);

	void OnInvertNormalCheckboxChanged(ECheckBoxState CheckBoxState, TSharedRef<IPropertyHandle> InvertPlaneProperty);
	ECheckBoxState GetInvertNormalCheckBoxState() const;

	void OnReferenceSkeletonComponentChanged();
};
