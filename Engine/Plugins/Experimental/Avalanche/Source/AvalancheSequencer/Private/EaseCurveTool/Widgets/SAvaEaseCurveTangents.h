// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EaseCurveTool/AvaEaseCurveTangents.h"
#include "Widgets/Input/SNumericEntryBox.h"

class SAvaEaseCurveTangents : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAvaEaseCurveTangents)
	{}
		SLATE_ARGUMENT(FAvaEaseCurveTangents, InitialTangents)
		SLATE_EVENT(SNumericEntryBox<double>::FOnValueChanged, OnStartTangentChanged)
		SLATE_EVENT(SNumericEntryBox<double>::FOnValueChanged, OnStartWeightChanged)
		SLATE_EVENT(SNumericEntryBox<double>::FOnValueChanged, OnEndTangentChanged)
		SLATE_EVENT(SNumericEntryBox<double>::FOnValueChanged, OnEndWeightChanged)
		SLATE_EVENT(FSimpleDelegate, OnBeginSliderMovement)
		SLATE_EVENT(SNumericEntryBox<double>::FOnValueChanged, OnEndSliderMovement)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	double GetStartTangent() const;
	double GetStartTangentWeight() const;
	double GetEndTangent() const;
	double GetEndTangentWeight() const;

protected:
	TSharedRef<SWidget> ConstructTangentNumBox(const FText& InLabel
		, const FText& InToolTip
		, const TAttribute<double>& InValue
		, const SNumericEntryBox<double>::FOnValueChanged& InOnValueChanged
		, const TOptional<double>& InMinSliderValue
		, const TOptional<double>& InMaxSliderValue) const;

	void OnStartTangentSpinBoxChanged(const double InNewValue);
	void OnStartTangentWeightSpinBoxChanged(const double InNewValue);
	void OnEndTangentSpinBoxChanged(const double InNewValue);
	void OnEndTangentWeightSpinBoxChanged(const double InNewValue);

	FAvaEaseCurveTangents Tangents;

	SNumericEntryBox<double>::FOnValueChanged OnStartTangentChanged;
	SNumericEntryBox<double>::FOnValueChanged OnStartWeightChanged;
	SNumericEntryBox<double>::FOnValueChanged OnEndTangentChanged;
	SNumericEntryBox<double>::FOnValueChanged OnEndWeightChanged;

	FSimpleDelegate OnBeginSliderMovement;
	SNumericEntryBox<double>::FOnValueChanged OnEndSliderMovement;
};
