// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkHubSubjectSettingsDetailsCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "LiveLinkHubSubjectSettings.h"

#define LOCTEXT_NAMESPACE "LiveLinkHubSubjectSettingsDetailCustomization"

void FLiveLinkHubSubjectSettingsDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	//Get the current settings object being edited
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	InDetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() != 1)
	{
		return;
	}

	TSharedRef<IPropertyHandle> OutboundProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULiveLinkHubSubjectSettings, OutboundName));
	if (IDetailPropertyRow* PropertyRow = InDetailBuilder.EditDefaultProperty(OutboundProperty))
	{
		FResetToDefaultOverride ResetOverride;

		PropertyRow->CustomWidget()
			.OverrideResetToDefault(FResetToDefaultOverride::Create(
				FIsResetToDefaultVisible::CreateLambda([](TSharedPtr<IPropertyHandle> PropertyHandle)
					{
						TSharedPtr<IPropertyHandle> SubjectNameProperty = PropertyHandle->GetParentHandle()->GetChildHandle(GET_MEMBER_NAME_CHECKED(ULiveLinkHubSubjectSettings, SubjectName));
						
						if (SubjectNameProperty && SubjectNameProperty->IsValidHandle())
						{
							FText OutboundName;
							FText SubjectName;
							PropertyHandle->GetValueAsDisplayText(OutboundName);
							SubjectNameProperty->GetValueAsDisplayText(SubjectName);
							return !OutboundName.EqualTo(SubjectName);
						}
						return false;
					}),
				FResetToDefaultHandler::CreateLambda([](TSharedPtr<IPropertyHandle> PropertyHandle)
					{
						TSharedPtr<IPropertyHandle> SubjectNameProperty = PropertyHandle->GetParentHandle()->GetChildHandle(GET_MEMBER_NAME_CHECKED(ULiveLinkHubSubjectSettings, SubjectName));

						if (SubjectNameProperty && SubjectNameProperty->IsValidHandle())
						{
							FString SubjectName;
							SubjectNameProperty->GetValueAsDisplayString(SubjectName);
							PropertyHandle->SetValue(*SubjectName);
						}
					})
			))
			.NameContent()
			[
				OutboundProperty->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				OutboundProperty->CreatePropertyValueWidget()
			];
	}

	InDetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULiveLinkHubSubjectSettings, InterpolationProcessor), ULiveLinkSubjectSettings::StaticClass());
	InDetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULiveLinkHubSubjectSettings, FrameRate), ULiveLinkSubjectSettings::StaticClass());
	InDetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULiveLinkHubSubjectSettings, bRebroadcastSubject), ULiveLinkSubjectSettings::StaticClass());
	InDetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULiveLinkHubSubjectSettings, Translators), ULiveLinkSubjectSettings::StaticClass());
}

#undef LOCTEXT_NAMESPACE
