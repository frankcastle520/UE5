// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailsCustomizations/ChaosVDQueryDataWrappersCustomizationDetails.h"

#include "ChaosVDEngine.h"
#include "ChaosVDModule.h"
#include "ChaosVDRecording.h"
#include "ChaosVDScene.h"
#include "CollisionQueryParams.h"
#include "DataWrappers/ChaosVDQueryDataWrappers.h"
#include "DetailsCustomizations/ChaosVDDetailsCustomizationUtils.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "Widgets/SChaosVDMainTab.h"
#include "Widgets/SChaosVDWarningMessageBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

TSharedRef<IPropertyTypeCustomization> FChaosVDQueryDataWrappersCustomizationDetails::MakeInstance()
{
	return MakeShareable(new FChaosVDQueryDataWrappersCustomizationDetails());
}

void FChaosVDQueryDataWrappersCustomizationDetails::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	uint32 NumChildren;
	StructPropertyHandle->GetNumChildren(NumChildren);

	if (NumChildren == 0)
	{
		return;
	}

	TArray<TSharedPtr<IPropertyHandle>> Handles;
	Handles.Reserve(NumChildren);

	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedRef<IPropertyHandle> Handle = StructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
		Handles.Add(Handle);
	}

	FChaosVDDetailsCustomizationUtils::HideInvalidCVDDataWrapperProperties(Handles);
}

TSharedRef<IDetailCustomization> FChaosVDQueryVisitDataCustomization::MakeInstance()
{
	return MakeShareable( new FChaosVDQueryVisitDataCustomization );
}

void FChaosVDQueryVisitDataCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TSharedRef<IPropertyHandle>> PotentialPropertiesToHide;
	
	constexpr int32 PropertiesToEvaluateNum = 2;
	PotentialPropertiesToHide.Reset(PropertiesToEvaluateNum);

	PotentialPropertiesToHide.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(FChaosVDQueryVisitStep, QueryFastData)));
	PotentialPropertiesToHide.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(FChaosVDQueryVisitStep, HitData)));

	FChaosVDDetailsCustomizationUtils::HideInvalidCVDDataWrapperProperties(PotentialPropertiesToHide, DetailBuilder);
}

TSharedRef<IDetailCustomization> FChaosVDQueryDataWrapperCustomization::MakeInstance()
{
	return MakeShareable( new FChaosVDQueryVisitDataCustomization );
}

void FChaosVDQueryDataWrapperCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TSharedRef<IPropertyHandle>> PotentialPropertiesToHide;

	constexpr int32 PropertiesToEvaluateNum = 3;
	PotentialPropertiesToHide.Reset(PropertiesToEvaluateNum);

	PotentialPropertiesToHide.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(FChaosVDQueryDataWrapper, CollisionQueryParams)));
	PotentialPropertiesToHide.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(FChaosVDQueryDataWrapper, CollisionResponseParams)));
	PotentialPropertiesToHide.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(FChaosVDQueryDataWrapper, CollisionObjectQueryParams)));

	FChaosVDDetailsCustomizationUtils::HideInvalidCVDDataWrapperProperties(PotentialPropertiesToHide, DetailBuilder);
}

FChaosVDCollisionChannelsCustomizationBase::FChaosVDCollisionChannelsCustomizationBase(const TWeakPtr<SChaosVDMainTab>& InMainTab)
{
	MainTabWeakPtr = InMainTab;
	// Fill with the Engine defaults. When a CVD file is loaded we will update it with any new data is available
	UpdateCollisionChannelsInfoCache({});
}

void FChaosVDCollisionChannelsCustomizationBase::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TSharedPtr<SChaosVDMainTab> MainTabPtr =  MainTabWeakPtr.Pin(); 
	TSharedPtr<FChaosVDScene> Scene = MainTabPtr ? MainTabPtr->GetChaosVDEngineInstance()->GetCurrentScene() : nullptr;

	if (TSharedPtr<FChaosVDRecording> CVDRecording = Scene ? Scene->LoadedRecording : nullptr)
	{
		UpdateCollisionChannelsInfoCache(CVDRecording->GetCollisionChannelsInfoContainer());
	}
	else
	{
		UpdateCollisionChannelsInfoCache(nullptr);
	}
}

void FChaosVDCollisionChannelsCustomizationBase::UpdateCollisionChannelsInfoCache(TSharedPtr<FChaosVDCollisionChannelsInfoContainer> NewCollisionChannelsInfo)
{
	if (NewCollisionChannelsInfo)
	{
		CachedCollisionChannelInfos = NewCollisionChannelsInfo;
		bChannelInfoBuiltFromDefaults = false;
	}
	else
	{
		// Fallback to engine channels name using the enum metadata
		CachedCollisionChannelInfos = FChaosVDDetailsCustomizationUtils::BuildDefaultCollisionChannelInfo();
		bChannelInfoBuiltFromDefaults = true;
	}
}

ECollisionResponse FChaosVDCollisionResponseParamsCustomization::GetCurrentCollisionResponseForChannel(int32 ChannelIndex) const
{
	constexpr int32 MaxChannels = FChaosVDDetailsCustomizationUtils::GetMaxCollisionChannelIndex();

	if (ChannelIndex >= MaxChannels)
	{
		return ECollisionResponse::ECR_MAX;
	}

	if (FChaosVDCollisionResponseParams* Data = CurrentPropertyDataHandle ? CurrentPropertyDataHandle->GetDataInstance() : nullptr)
	{
		return static_cast<ECollisionResponse>(Data->FlagsPerChannel[ChannelIndex]);
	}

	return ECollisionResponse::ECR_MAX;
}

TSharedRef<IPropertyTypeCustomization> FChaosVDCollisionResponseParamsCustomization::MakeInstance(TWeakPtr<SChaosVDMainTab> MainTab)
{
	return MakeShared<FChaosVDCollisionResponseParamsCustomization>(MainTab);
}

void FChaosVDCollisionResponseParamsCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FChaosVDCollisionChannelsCustomizationBase::CustomizeChildren(StructPropertyHandle, StructBuilder, StructCustomizationUtils);

	if (!CachedCollisionChannelInfos)
	{
		return;
	}
	
	CurrentPropertyDataHandle = MakeShared<FChaosVDDetailsPropertyDataHandle<FChaosVDCollisionResponseParams>>(StructPropertyHandle);
	FChaosVDCollisionResponseParams* CollisionResponseParams = CurrentPropertyDataHandle->GetDataInstance();

	if (!CollisionResponseParams)
	{
		CurrentPropertyDataHandle = nullptr;
		return;
	}

	IDetailGroup& CollisionGroup = StructBuilder.AddGroup(TEXT("CollisionResponseParams"), LOCTEXT("CollisionResponseQueryParamsLabel", "Collision Response Query Params"));
	CollisionGroup.EnableReset(false);

	CollisionGroup.HeaderRow()
	.NameContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		[ 
			SNew(STextBlock)
			.Text(LOCTEXT("CollisionResponsesLabel", "Collision Response Query params"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	];

	if (bChannelInfoBuiltFromDefaults)
	{
		CollisionGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(SChaosVDWarningMessageBox).WarningText(FChaosVDDetailsCustomizationUtils::GetDefaultCollisionChannelsUseWarningMessage())
		];
	}
	
	FChaosVDCollisionChannelStateGetter CollisionChannelStateGetter;
	CollisionChannelStateGetter.BindSP(this, &FChaosVDCollisionResponseParamsCustomization::GetCurrentCollisionResponseForChannel);
	
	FChaosVDDetailsCustomizationUtils::BuildCollisionChannelMatrix(CollisionChannelStateGetter,
																	{ CachedCollisionChannelInfos->CustomChannelsNames, UE_ARRAY_COUNT(CachedCollisionChannelInfos->CustomChannelsNames)},
																	CollisionGroup);
}

TSharedRef<IPropertyTypeCustomization> FChaosVDCollisionObjectParamsCustomization::MakeInstance(TWeakPtr<SChaosVDMainTab> MainTab)
{
	return MakeShared<FChaosVDCollisionObjectParamsCustomization>(MainTab);
}

void FChaosVDCollisionObjectParamsCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FChaosVDCollisionChannelsCustomizationBase::CustomizeChildren(StructPropertyHandle, StructBuilder, StructCustomizationUtils);
	
	if (!CachedCollisionChannelInfos)
	{
		return;
	}
	
	CurrentPropertyDataHandle = MakeShared<FChaosVDDetailsPropertyDataHandle<FChaosVDCollisionObjectQueryParams>>(StructPropertyHandle);
	FChaosVDCollisionObjectQueryParams* CollisionResponseParams = CurrentPropertyDataHandle->GetDataInstance();

	if (!CollisionResponseParams)
	{
		CurrentPropertyDataHandle = nullptr;
		return;
	}

	IDetailGroup& CollisionGroup = StructBuilder.AddGroup(TEXT("CollisionObjectResponseParams"), LOCTEXT("CollisionObjectResponseQueryParamsLabel", "Collision Response Query Params") );
	CollisionGroup.EnableReset(false);

	constexpr int32 MaxChannels = FChaosVDDetailsCustomizationUtils::GetMaxCollisionChannelIndex();
	for (int32 ChannelIndex = 0 ; ChannelIndex < MaxChannels; ChannelIndex++)
	{
		FChaosVDCollisionChannelInfo& ChannelInfo = CachedCollisionChannelInfos->CustomChannelsNames[ChannelIndex];

		if (ChannelInfo.bIsTraceType)
		{
			continue;
		}

		// Currently, all details panel in CVD are read only
		constexpr bool bIsEditable = false;

		CollisionGroup.AddWidgetRow()
		.NameContent()
		[
			SNew(STextBlock)
			.Text(FText::FromString(CachedCollisionChannelInfos->CustomChannelsNames[ChannelIndex].DisplayName))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SBox)
			.IsEnabled(bIsEditable)
			.WidthOverride(50)
			.Content()
			[
				SNew(SCheckBox)
				.IsChecked_Raw(this, &FChaosVDCollisionObjectParamsCustomization::GetCurrentObjectFlag, ChannelIndex)
			]
		];
	}
}

ECheckBoxState FChaosVDCollisionObjectParamsCustomization::GetCurrentObjectFlag(int32 ChannelIndex) const
{
	FChaosVDCollisionObjectQueryParams* CollisionObjectResponseParams = CurrentPropertyDataHandle ? CurrentPropertyDataHandle->GetDataInstance() : nullptr;
	if (!CollisionObjectResponseParams)
	{
		return ECheckBoxState::Undetermined;
	}

	return (CollisionObjectResponseParams->ObjectTypesToQuery & ECC_TO_BITFIELD(ChannelIndex)) != 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

#undef LOCTEXT_NAMESPACE
