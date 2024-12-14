// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVM/ViewModels/TrackModel.h"

#include "MVVM/SharedViewModelData.h"
#include "MVVM/Extensions/IObjectBindingExtension.h"
#include "MVVM/Extensions/IRecyclableExtension.h"
#include "MVVM/Extensions/ITrackExtension.h"
#include "MVVM/ViewModels/ChannelModel.h"
#include "MVVM/ViewModels/FolderModel.h"
#include "MVVM/ViewModels/ViewModelIterators.h"
#include "MVVM/ViewModels/SectionModel.h"
#include "MVVM/ViewModels/SequenceModel.h"
#include "MVVM/ViewModels/TrackModelLayoutBuilder.h"
#include "MVVM/ViewModels/TrackRowModel.h"
#include "MVVM/SectionModelStorageExtension.h"
#include "MVVM/TrackRowModelStorageExtension.h"
#include "MVVM/Views/SOutlinerTrackView.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/Selection/Selection.h"

#include "MovieScene.h"
#include "MovieSceneFolder.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "MovieSceneNameableTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieScenePrimitiveMaterialTrack.h"
#include "EntitySystem/IMovieSceneBlenderSystemSupport.h"

#include "ISequencer.h"
#include "ISequencerSection.h"
#include "ISequencerTrackEditor.h"
#include "SSequencer.h"
#include "SequencerNodeTree.h"
#include "SequencerUtilities.h"
#include "SequencerCommonHelpers.h"

#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "TrackModel"

namespace UE::Sequencer
{

FTrackModel::FTrackModel(UMovieSceneTrack* Track)
	: SectionList(EViewModelListType::TrackArea)
	, TopLevelChannelList(GetTopLevelChannelGroupType())
	, WeakTrack(Track)
	, bNeedsUpdate(false)
{
	RegisterChildList(&SectionList);
	RegisterChildList(&TopLevelChannelList);

	SetIdentifier(Track->GetFName());
}

FTrackModel::~FTrackModel()
{
}

EViewModelListType FTrackModel::GetTopLevelChannelType()
{
	static EViewModelListType TopLevelChannel = RegisterCustomModelListType();
	return TopLevelChannel;
}

EViewModelListType FTrackModel::GetTopLevelChannelGroupType()
{
	static EViewModelListType TopLevelChannelGroup = RegisterCustomModelListType();
	return TopLevelChannelGroup;
}

FViewModelChildren FTrackModel::GetSectionModels()
{
	return GetChildrenForList(&SectionList);
}

FViewModelChildren FTrackModel::GetTopLevelChannels()
{
	return GetChildrenForList(&TopLevelChannelList);
}

UMovieSceneTrack* FTrackModel::GetTrack() const
{
	return WeakTrack.Get();
}

int32 FTrackModel::GetRowIndex() const
{
	return 0;
}

TSharedPtr<ISequencerTrackEditor> FTrackModel::GetTrackEditor() const
{
	return TrackEditor;
}

void FTrackModel::OnConstruct()
{
	UMovieSceneTrack* Track = GetTrack();
	TSharedPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>();
	check(SequenceModel && Track);

	if (!IsLinked())
	{
		Track->EventHandlers.Link(this);
	}

	TrackEditor = SequenceModel->GetSequencer()->GetTrackEditor(Track);

	ForceUpdate();
}

void FTrackModel::OnModifiedDirectly(UMovieSceneSignedObject*)
{
	if (!bNeedsUpdate)
	{
		bNeedsUpdate = true;
		UMovieSceneSignedObject::AddFlushSignal(SharedThis(this));
	}
}

void FTrackModel::OnModifiedIndirectly(UMovieSceneSignedObject* InSource)
{
	if (!bNeedsUpdate)
	{
		bNeedsUpdate = true;
		UMovieSceneSignedObject::AddFlushSignal(SharedThis(this));
	}
}

void FTrackModel::OnDeferredModifyFlush()
{
	if (bNeedsUpdate)
	{
		ForceUpdate();
		bNeedsUpdate = false;
	}
}

void FTrackModel::ForceUpdate()
{
	FViewModelHierarchyOperation HierarchyOperation(GetSharedData());

	FViewModelChildren OutlinerChildren = GetChildList(EViewModelListType::Outliner);
	FViewModelChildren SectionChildren  = GetChildList(EViewModelListType::TrackArea);
	FViewModelChildren TopLevelChannelChildren = GetChildList(GetTopLevelChannelGroupType());

	UMovieSceneTrack* Track = WeakTrack.Get();
	if (!Track)
	{
		// Free outliner and section children, this track is gone.
		OutlinerChildren.Empty();
		SectionChildren.Empty();
		TopLevelChannelChildren.Empty();
		return;
	}

	TSharedPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>();
	if (!SequenceModel)
	{
		// Not part of a full sequence hierarchy yet - wait for OnSetSharedData()
		return;
	}

	FSectionModelStorageExtension* SectionModelStorage = SequenceModel->CastDynamic<FSectionModelStorageExtension>();

	FGuid ObjectBinding;
	if (TSharedPtr<IObjectBindingExtension> ObjectBindingExtension = FindAncestorOfType<IObjectBindingExtension>())
	{
		ObjectBinding = ObjectBindingExtension->GetObjectGuid();
	}

	TBitArray<> PopulatedRows;

	for (UMovieSceneSection* Section : Track->GetAllSections())
	{
		const int32 RowIndex = Section->GetRowIndex();
		PopulatedRows.PadToNum(RowIndex + 1, false);
		PopulatedRows[RowIndex] = true;
	}

	const int32 NumRows = PopulatedRows.CountSetBits();

	if (NumRows == 0)
	{
		// Reset expansion state if this track can no longer be expanded
		SetExpansion(false);

		// Clear any left-over row models, layout models, or section models.
		OutlinerChildren.Empty();
		SectionChildren.Empty();
		TopLevelChannelChildren.Empty();
	}
	else if (NumRows == 1)
	{
		// Keep sections alive by retaining the previous list temporarily
		TSharedPtr<FViewModel> SectionsTail;

		FScopedViewModelListHead RecycledModels(AsShared(), EViewModelListType::Recycled);
		GetChildrenForList(&SectionList).MoveChildrenTo<IRecyclableExtension>(RecycledModels.GetChildren(), IRecyclableExtension::CallOnRecycle);
		OutlinerChildren.MoveChildrenTo<IRecyclableExtension>(RecycledModels.GetChildren(), IRecyclableExtension::CallOnRecycle);
		TopLevelChannelChildren.MoveChildrenTo<IRecyclableExtension>(RecycledModels.GetChildren(), IRecyclableExtension::CallOnRecycle);

		// Add all sections directly to this track row
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			TSharedPtr<FSectionModel> SectionModel = SectionModelStorage->FindModelForSection(Section);
			if (!SectionModel && TrackEditor)
			{
				TSharedRef<ISequencerSection> SectionInterface = TrackEditor->MakeSectionInterface(*Section, *Track, ObjectBinding);
				SectionModel = SectionModelStorage->CreateModelForSection(Section, SectionInterface);
			}

			if (ensure(SectionModel))
			{
				// Move the child back into the real section list
				SectionChildren.InsertChild(SectionModel, SectionsTail);
				SectionsTail = SectionModel;
			}
		}

		// Rebuild the outliner layout for this track. This will clear our children and rebuild them if needed
		// (with potentially recycled children), so if we went from, say, 2 rows to 1 row, it should correctly
		// discard any children we don't need anymore.
		FTrackModelLayoutBuilder LayoutBuilder(AsShared());

		for (TSharedPtr<FSectionModel> Section : TViewModelListIterator<FSectionModel>(&SectionList))
		{
			LayoutBuilder.RefreshLayout(Section);
		}

		if (OutlinerChildren.IsEmpty())
		{
			// Reset expansion state if this track can no longer be expanded
			SetExpansion(false);
		}
	}
	else
	{
		// Always expand parent tracks
		SetExpansion(true);

		// Keep sections alive by retaining the previous list temporarily
		// This should only be required if this track previously represented
		// a single row, but now there are multiple rows
		FScopedViewModelListHead RecycledModels(AsShared(), EViewModelListType::Recycled);
		GetChildrenForList(&SectionList).MoveChildrenTo<IRecyclableExtension>(RecycledModels.GetChildren(), IRecyclableExtension::CallOnRecycle);
		OutlinerChildren.MoveChildrenTo<IRecyclableExtension>(RecycledModels.GetChildren(), IRecyclableExtension::CallOnRecycle);
		TopLevelChannelChildren.MoveChildrenTo<IRecyclableExtension>(RecycledModels.GetChildren(), IRecyclableExtension::CallOnRecycle);

		// We need to build row models so let's grab the storage for that
		FTrackRowModelStorageExtension* TrackRowModelStorage = SequenceModel->CastDynamic<FTrackRowModelStorageExtension>();
		check(TrackRowModelStorage);

		// We will build some info about what sections go on what row
		// Note: the OldSections pointer is just to keep the row section models alive until we re-assign them
		struct FRowData
		{
			TSharedPtr<FTrackRowModel> Row;
			TSharedPtr<FViewModel> SectionsTail;
			TUniquePtr<FScopedViewModelListHead> RecycledModels;
		};
		TArray<FRowData, TInlineAllocator<8>> RowModels;
		RowModels.SetNum(PopulatedRows.Num());

		// Create track row models for all populated rows
		TSharedPtr<FTrackRowModel> LastTrackRowModel;
		for (TConstSetBitIterator<> It(PopulatedRows); It; ++It)
		{
			const int32 RowIndex = It.GetIndex();

			TSharedPtr<FTrackRowModel> TrackRowModel = TrackRowModelStorage->FindModelForTrackRow(Track, RowIndex);
			if (!TrackRowModel)
			{
				TrackRowModel = TrackRowModelStorage->CreateModelForTrackRow(Track, RowIndex);
			}

			if (ensure(TrackRowModel))
			{
				OutlinerChildren.InsertChild(TrackRowModel, LastTrackRowModel);
				LastTrackRowModel = TrackRowModel;

				RowModels[RowIndex].Row = TrackRowModel;

				// Recycle sections, outliner children, and more, while keeping them alive.
				RowModels[RowIndex].RecycledModels = MakeUnique<FScopedViewModelListHead>(TrackRowModel, EViewModelListType::Recycled);
				FViewModelChildren RecycledRowModels = RowModels[RowIndex].RecycledModels->GetChildren();
				TrackRowModel->GetSectionModels().MoveChildrenTo<IRecyclableExtension>(RecycledRowModels, IRecyclableExtension::CallOnRecycle);
				TrackRowModel->GetChildList(EViewModelListType::Outliner).MoveChildrenTo<IRecyclableExtension>(RecycledRowModels, IRecyclableExtension::CallOnRecycle);
				TrackRowModel->GetTopLevelChannels().MoveChildrenTo<IRecyclableExtension>(RecycledRowModels, IRecyclableExtension::CallOnRecycle);
			}
		}

		// Add all sections to both their appropriate track rows and ourselves
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			const int32 RowIndex = Section->GetRowIndex();

			TSharedPtr<FSectionModel> SectionModel = SectionModelStorage->FindModelForSection(Section);
			if (!SectionModel && TrackEditor)
			{
				TSharedRef<ISequencerSection> SectionInterface = TrackEditor->MakeSectionInterface(*Section, *Track, ObjectBinding);
				SectionModel = SectionModelStorage->CreateModelForSection(Section, SectionInterface);
			}

			RowModels[RowIndex].Row->GetSectionModels().InsertChild(SectionModel, RowModels[RowIndex].SectionsTail);
			RowModels[RowIndex].SectionsTail = SectionModel;
		}

		// Rebuild the outliner layout for each track row
		for (const FRowData& RowData : RowModels)
		{
			if (RowData.Row)
			{
				FTrackModelLayoutBuilder LayoutBuilder(RowData.Row->AsShared());
				for (TSharedPtr<FSectionModel> Section : RowData.Row->GetChildrenOfType<FSectionModel>(EViewModelListType::TrackArea))
				{
					LayoutBuilder.RefreshLayout(Section);
				}
			}
			// else: unset row... it should only happen while we are dragging sections, until
			//       we fixup row indices
		}
	}
}

FOutlinerSizing FTrackModel::GetOutlinerSizing() const
{
	FViewDensityInfo Density = GetEditor()->GetViewDensity();

	float Height = Density.UniformHeight.Get(SequencerLayoutConstants::SectionAreaDefaultHeight);
	for (TSharedPtr<FSectionModel> Section : SectionList.Iterate<FSectionModel>())
	{
		Height = Section->GetSectionInterface()->GetSectionHeight(Density);
		break;
	}
	return FOutlinerSizing(Height);
}

void FTrackModel::GetIdentifierForGrouping(TStringBuilder<128>& OutString) const
{
	FOutlinerItemModel::GetIdentifier().ToString(OutString);
}

FTrackAreaParameters FTrackModel::GetTrackAreaParameters() const
{
	FTrackAreaParameters Params;
	Params.LaneType = ETrackAreaLaneType::Nested;
	Params.TrackLanePadding.Bottom = 1.f;
	return Params;
}

FViewModelVariantIterator FTrackModel::GetTrackAreaModelList() const
{
	return &SectionList;
}

FViewModelVariantIterator FTrackModel::GetTopLevelChildTrackAreaModels() const
{
	return &TopLevelChannelList;
}

bool FTrackModel::CanRename() const
{
	UMovieSceneNameableTrack* NameableTrack = Cast<UMovieSceneNameableTrack>(GetTrack());
	return NameableTrack && NameableTrack->CanRename();
}

void FTrackModel::Rename(const FText& NewName)
{
	UMovieSceneNameableTrack* NameableTrack = ::Cast<UMovieSceneNameableTrack>(GetTrack());

	if (NameableTrack && !NameableTrack->GetDisplayName().EqualTo(NewName))
	{
		const FScopedTransaction Transaction(NSLOCTEXT("SequencerTrackNode", "RenameTrack", "Rename Track"));
		NameableTrack->SetDisplayName(NewName);

		SetIdentifier(FName(*NewName.ToString()));

		// HACK: this should not exist but is required to make renaming emitters work in niagara
		if (TSharedPtr<FSequenceModel> OwnerModel = FindAncestorOfType<FSequenceModel>())
		{
			OwnerModel->GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
		}
	}
}

bool FTrackModel::IsRenameValidImpl(const FText& NewName, FText& OutErrorMessage) const
{
	UMovieSceneNameableTrack* NameableTrack = ::Cast<UMovieSceneNameableTrack>(GetTrack());
	if (NameableTrack)
	{
		return NameableTrack->ValidateDisplayName(NewName, OutErrorMessage);
	}
	return false;
}

void FTrackModel::SortChildren()
{
	// Nothing to do
}

FSortingKey FTrackModel::GetSortingKey() const
{
	FSortingKey SortingKey;

	if (UMovieSceneTrack* Track = GetTrack())
	{
		SortingKey.DisplayName = Track->GetDisplayName();
		SortingKey.CustomOrder = Track->GetSortingOrder();
	}

	// When inside object bindings, we come after other object bindings. Elsewhere, we come before object bindings.
	const bool bHasParentObjectBinding = (CastParent<IObjectBindingExtension>() != nullptr);
	SortingKey.PrioritizeBy(bHasParentObjectBinding ? 1 : 2);

	return SortingKey;
}

void FTrackModel::SetCustomOrder(int32 InCustomOrder)
{
	if (UMovieSceneTrack* Track = GetTrack())
	{
		Track->SetSortingOrder(InCustomOrder);
	}
}

bool FTrackModel::HasCurves() const
{
	FViewModelChildren TopLevelChannels = const_cast<FTrackModel*>(this)->GetTopLevelChannels();
	for (auto It(TopLevelChannels.IterateSubList<FChannelGroupModel>()); It; ++It)
	{
		if (It->HasCurves())
		{
			return true;
		}
	}
	return false;
}

void FTrackModel::CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels)
{
	TViewModelPtr<FChannelGroupModel> ChannelGroup = TopLevelChannelList.GetHead().ImplicitCast();
	if (ChannelGroup)
	{
		ChannelGroup->CreateCurveModels(OutCurveModels);
	}
}

bool FTrackModel::GetDefaultExpansionState() const
{
	TViewModelListIterator<ITrackExtension> It = GetChildrenOfType<ITrackExtension>();
	const bool bHasTrackRows = (bool)It;
	if (bHasTrackRows)
	{
		return true;
	}

	UMovieSceneTrack* Track = GetTrack();
	if (TrackEditor && Track)
	{
		return TrackEditor->GetDefaultExpansionState(Track);
	}

	return false;
}

bool FTrackModel::IsDimmed() const
{
	UMovieSceneTrack* Track = GetTrack();

	if (Track)
	{
		if (Track->IsEvalDisabled())
		{
			return true;
		}
		if (Track->ConditionContainer.Condition)
		{
			FGuid BindingID;
			FMovieSceneSequenceID SequenceID = MovieSceneSequenceID::Root;
			if (TViewModelPtr<FObjectBindingModel> ObjectBindingModel = FindAncestorOfType<FObjectBindingModel>())
			{
				BindingID = ObjectBindingModel->GetObjectGuid();
			}
			if (TViewModelPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>())
			{
				SequenceID = SequenceModel->GetSequenceID();

				if (TSharedPtr<FSequencerEditorViewModel> SequencerModel = SequenceModel->GetEditor())
				{
					if (!MovieSceneHelpers::EvaluateSequenceCondition(BindingID, SequenceID, Track->ConditionContainer.Condition, Track, SequencerModel->GetSequencer()->GetSharedPlaybackState()))
					{
						return true;
					}
				}
			}
		} 
	}

	return FOutlinerItemModel::IsDimmed();
}

FSlateFontInfo FTrackModel::GetLabelFont() const
{
	bool bAllAnimated = false;
	TViewModelPtr<FChannelGroupModel> TopLevelChannel = TopLevelChannelList.GetHead().ImplicitCast();
	if (TopLevelChannel)
	{
		for (const TViewModelPtr<FChannelModel>& ChannelModel : TopLevelChannel->GetTrackAreaModelListAs<FChannelModel>())
		{
			FMovieSceneChannel* Channel = ChannelModel->GetChannel();
			if (!Channel || Channel->GetNumKeys() == 0)
			{
				return FOutlinerItemModel::GetLabelFont();
			}
			else
			{
				bAllAnimated = true;
			}
		}
		if (bAllAnimated == true)
		{
			return FAppStyle::GetFontStyle("Sequencer.AnimationOutliner.ItalicFont");
		}
	}
	return FOutlinerItemModel::GetLabelFont();
}

const FSlateBrush* FTrackModel::GetIconBrush() const
{
	return TrackEditor ? TrackEditor->GetIconBrush() : nullptr;
}

FText FTrackModel::GetLabel() const
{
	UMovieSceneTrack* Track = GetTrack();
	return Track ? Track->GetDisplayName() : FText::GetEmpty();
}

FSlateColor FTrackModel::GetLabelColor() const
{
	UMovieSceneTrack* Track = GetTrack();
	if (!Track)
	{
		return FSlateColor::UseForeground();
	}

	FMovieSceneLabelParams LabelParams;
	LabelParams.bIsDimmed = IsDimmed();
	if (TViewModelPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>())
	{
		if (TSharedPtr<FSequencerEditorViewModel> SequencerModel = SequenceModel->GetEditor())
		{
			LabelParams.SequenceID = SequenceModel->GetSequenceID();
			LabelParams.Player = SequencerModel->GetSequencer().Get();
			if (LabelParams.Player)
			{
				if (TViewModelPtr<FObjectBindingModel> ObjectBindingModel = FindAncestorOfType<FObjectBindingModel>())
				{
					LabelParams.BindingID = ObjectBindingModel->GetObjectGuid();

					// If the object binding model has an invalid binding, we want to use its label color, as it may be red or gray depending on situation
					// and we want the children of that to have the same color.
					// Otherwise, we can use the track's label color below
					TArrayView<TWeakObjectPtr<> > BoundObjects = LabelParams.Player->FindBoundObjects(LabelParams.BindingID, LabelParams.SequenceID);
					if (BoundObjects.Num() == 0)
					{
						return ObjectBindingModel->GetLabelColor();
					}
				}
			}
		}
	}

	return Track->GetLabelColor(LabelParams);
}

FText FTrackModel::GetLabelToolTipText() const
{
	UMovieSceneTrack* Track = GetTrack();
	if (!Track)
	{
		return FText();
	}

	FMovieSceneLabelParams LabelParams;
	LabelParams.bIsDimmed = IsDimmed();
	if (TViewModelPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>())
	{
		if (TSharedPtr<FSequencerEditorViewModel> SequencerModel = SequenceModel->GetEditor())
		{
			LabelParams.SequenceID = SequenceModel->GetSequenceID();
			LabelParams.Player = SequencerModel->GetSequencer().Get();
			if (LabelParams.Player)
			{
				if (TViewModelPtr<FObjectBindingModel> ObjectBindingModel = FindAncestorOfType<FObjectBindingModel>())
				{
					LabelParams.BindingID = ObjectBindingModel->GetObjectGuid();
				}
				return Track->GetDisplayNameToolTipText(LabelParams);
			}
		}
	}
	return FText();
}

TSharedPtr<SWidget> FTrackModel::CreateOutlinerViewForColumn(const FCreateOutlinerViewParams& InParams, const FName& InColumnName)
{
	FBuildColumnWidgetParams Params(SharedThis(this), InParams);
	return TrackEditor->BuildOutlinerColumnWidget(Params, InColumnName);
}

bool FTrackModel::IsResizable() const
{
	UMovieSceneTrack* Track = GetTrack();
	return Track && TrackEditor->IsResizable(Track);
}

void FTrackModel::Resize(float NewSize)
{
	UMovieSceneTrack* Track = GetTrack();

	if (Track && TrackEditor->IsResizable(Track))
	{
		TrackEditor->Resize(NewSize, Track);
	}
}

ELockableLockState FTrackModel::GetLockState() const
{
	int32 NumSections = 0;
	int32 NumLockedSections = 0;

	for (const TViewModelPtr<FSectionModel>& Section : SectionList.Iterate<FSectionModel>())
	{
		++NumSections;

		UMovieSceneSection* SectionObject = Section->GetSection();
		if (SectionObject && SectionObject->IsLocked())
		{
			++NumLockedSections;
		}
	}

	if (NumSections == 0 || NumLockedSections == 0)
	{
		return ELockableLockState::None;
	}
	return NumLockedSections == NumSections ? ELockableLockState::Locked : ELockableLockState::PartiallyLocked;
}

void FTrackModel::SetIsLocked(bool bInIsLocked)
{
	for (const TViewModelPtr<FSectionModel>& Section : SectionList.Iterate<FSectionModel>())
	{
		UMovieSceneSection* SectionObject = Section->GetSection();
		if (SectionObject)
		{
			SectionObject->Modify();
			SectionObject->SetIsLocked(bInIsLocked);
		}
	}
}

const UMovieSceneCondition* FTrackModel::GetCondition() const
{
	UMovieSceneTrack* const Track = GetTrack();
	if (IsValid(Track))
	{
		return Track->ConditionContainer.Condition;
	}
	return nullptr;
}

EConditionableConditionState FTrackModel::GetConditionState() const
{
	TSharedPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>();
	TSharedPtr<ISequencer> Sequencer = SequenceModel ? SequenceModel->GetSequencer() : nullptr;
	if (Sequencer)
	{
		FGuid BindingID;

		if (TSharedPtr<IObjectBindingExtension> ParentBinding = FindAncestorOfType<IObjectBindingExtension>())
		{
			BindingID = ParentBinding->GetObjectGuid();
		}
		UMovieSceneTrack* const Track = GetTrack();
		if (IsValid(Track))
		{
			if (Track->ConditionContainer.Condition)
			{
				if (Track->ConditionContainer.Condition->bEditorForceTrue)
				{
					return EConditionableConditionState::HasConditionEditorForceTrue;
				}

				if (MovieSceneHelpers::EvaluateSequenceCondition(BindingID, Sequencer->GetFocusedTemplateID(), Track->ConditionContainer.Condition, Track, Sequencer->GetSharedPlaybackState()))
				{
					return EConditionableConditionState::HasConditionEvaluatingTrue;
				}
				else
				{
					return EConditionableConditionState::HasConditionEvaluatingFalse;
				}
			}
		
			// Special case. If we support multiple rows, and there is only a single row, then we must also check track row metadata for a condition here, as there will be no track row model.
			if (Track->SupportsMultipleRows() && Track->GetMaxRowIndex() == 0)
			{
				if (const FMovieSceneTrackRowMetadata* TrackRowMetadata = Track->FindTrackRowMetadata(GetRowIndex()))
				{
					if (TrackRowMetadata->ConditionContainer.Condition)
					{
						if (TrackRowMetadata->ConditionContainer.Condition->bEditorForceTrue)
						{
							return EConditionableConditionState::HasConditionEditorForceTrue;
						}
						else if (MovieSceneHelpers::EvaluateSequenceCondition(BindingID, Sequencer->GetFocusedTemplateID(), TrackRowMetadata->ConditionContainer.Condition, Track, Sequencer->GetSharedPlaybackState()))
						{
							return EConditionableConditionState::HasConditionEvaluatingTrue;
						}
						else
						{
							return EConditionableConditionState::HasConditionEvaluatingFalse;
						}
					}
				}
			}
		}
	}
	return EConditionableConditionState::None;
}

void FTrackModel::SetConditionEditorForceTrue(bool bEditorForceTrue)
{
	UMovieSceneTrack* const Track = GetTrack();
	if (IsValid(Track))
	{
		if (Track->ConditionContainer.Condition)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("SequencerTrackNode", "ConditionEditorForceTrue", "Set Condition Editor Force True"));
			Track->ConditionContainer.Condition->Modify();
			Track->ConditionContainer.Condition->bEditorForceTrue = bEditorForceTrue;
		}
	}
}

bool FTrackModel::CanDrag() const
{
	// Can only drag root tracks at the moment
	TSharedPtr<IObjectBindingExtension> ObjectBindingExtension = FindAncestorOfType<IObjectBindingExtension>();
	return ObjectBindingExtension == nullptr;
}

void FTrackModel::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	const TSharedPtr<FSequencerEditorViewModel> EditorViewModel = GetEditor();
	if (!EditorViewModel.IsValid())
	{
		return;
	}

	const TSharedPtr<FSequencer> Sequencer = EditorViewModel->GetSequencerImpl();
	if (!Sequencer.IsValid())
	{
		return;
	}

	UMovieSceneTrack* const Track = GetTrack();
	if (!IsValid(Track))
	{
		return;
	}

	if (TrackEditor)
	{
		TrackEditor->BuildTrackContextMenu(MenuBuilder, Track);
	}

	TArray<TWeakObjectPtr<>> WeakTracks;
	WeakTracks.Add(Track);
	SequencerHelpers::BuildEditTrackMenu(Sequencer, WeakTracks, MenuBuilder, true);

	if (Track->GetSupportedBlendTypes().Num() > 0)
	{
		SequencerHelpers::BuildNewSectionMenu(Sequencer, GetRowIndex() + 1, GetTrack(), MenuBuilder);
	}

	SequencerHelpers::BuildBlendingMenu(Sequencer, Track, MenuBuilder);

	const TArray<TWeakObjectPtr<>> TrackAreaModels = SequencerHelpers::GetSectionObjectsFromTrackAreaModels(GetTrackAreaModelList());
	SequencerHelpers::BuildEditSectionMenu(Sequencer, TrackAreaModels, MenuBuilder, true);

	if (const TViewModelPtr<FChannelGroupModel> ChannelGroup = TopLevelChannelList.GetHead().ImplicitCast())
	{
		ChannelGroup->BuildChannelOverrideMenu(MenuBuilder);
	}

	FOutlinerItemModel::BuildContextMenu(MenuBuilder);
}

void FTrackModel::BuildSidebarMenu(FMenuBuilder& MenuBuilder)
{
	const TSharedPtr<FSequencerEditorViewModel> EditorViewModel = GetEditor();
	if (!EditorViewModel.IsValid())
	{
		return;
	}

	const TSharedPtr<FSequencer> Sequencer = EditorViewModel->GetSequencerImpl();
	if (!Sequencer.IsValid())
	{
		return;
	}

	UMovieSceneTrack* const Track = GetTrack();
	if (!IsValid(Track))
	{
		return;
	}

	if (TrackEditor)
	{
		TrackEditor->BuildTrackSidebarMenu(MenuBuilder, Track);
	}

	TArray<TWeakObjectPtr<>> WeakTracks;
	WeakTracks.Add(Track);
	SequencerHelpers::BuildEditTrackMenu(Sequencer, WeakTracks, MenuBuilder, false);


	if (Track->GetSupportedBlendTypes().Num() > 0)
	{
		SequencerHelpers::BuildNewSectionMenu(Sequencer, GetRowIndex() + 1, GetTrack(), MenuBuilder);
	}

	SequencerHelpers::BuildBlendingMenu(Sequencer, Track, MenuBuilder);

	const TArray<TWeakObjectPtr<>> TrackAreaModels = SequencerHelpers::GetSectionObjectsFromTrackAreaModels(GetTrackAreaModelList());
	SequencerHelpers::BuildEditSectionMenu(Sequencer, TrackAreaModels, MenuBuilder, false);

	if (const TViewModelPtr<FChannelGroupModel> ChannelGroup = TopLevelChannelList.GetHead().ImplicitCast())
	{
		ChannelGroup->BuildChannelOverrideMenu(MenuBuilder);
	}

	FOutlinerItemModel::BuildSidebarMenu(MenuBuilder);
}

bool FTrackModel::CanDelete(FText* OutErrorMessage) const
{
	return true;
}

void FTrackModel::Delete()
{
	UMovieSceneTrack* Track = GetTrack();
	if (!Track)
	{
		return;
	}

	// Remove from a parent folder if necessary.
	if (TViewModelPtr<FFolderModel> ParentFolder = CastParent<FFolderModel>())
	{
		ParentFolder->GetFolder()->Modify();
		ParentFolder->GetFolder()->RemoveChildTrack(Track);
	}

	TSharedPtr<FSequenceModel> OwnerModel = FindAncestorOfType<FSequenceModel>();
	TSharedPtr<IObjectBindingExtension> ParentObjectBinding = FindAncestorOfType<IObjectBindingExtension>();

	check(OwnerModel);

	UMovieScene* MovieScene = OwnerModel->GetMovieScene();

	MovieScene->Modify();
	if (ParentObjectBinding)
	{
		FMovieSceneBinding* Binding = MovieScene->FindBinding(ParentObjectBinding->GetObjectGuid());
		if (Binding)
		{
			Binding->RemoveTrack(*Track, MovieScene);
		}
	}
	else if (MovieScene->GetCameraCutTrack() == Track)
	{
		MovieScene->RemoveCameraCutTrack();
	}
	else
	{
		MovieScene->RemoveTrack(*Track);
	}
}

bool FTrackModel::FindBoundObjects(TArray<UObject*>& OutBoundObjects) const
{
	TSharedPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>();
	TSharedPtr<ISequencer> Sequencer = SequenceModel ? SequenceModel->GetSequencer() : nullptr;
	if (!Sequencer)
	{
		return false;
	}

	TSharedPtr<IObjectBindingExtension> ParentBinding = FindAncestorOfType<IObjectBindingExtension>();
	if (!ParentBinding)
	{
		return false;
	}

	TArrayView<TWeakObjectPtr<>> FoundBoundObjects = Sequencer->FindBoundObjects(ParentBinding->GetObjectGuid(), Sequencer->GetFocusedTemplateID());
	OutBoundObjects.Reserve(OutBoundObjects.Num() + FoundBoundObjects.Num());
	for (TWeakObjectPtr<> WeakObject : FoundBoundObjects)
	{
		if (UObject* Object = WeakObject.Get())
		{
			OutBoundObjects.Add(Object);
		}
	}
	return true;
}

} // namespace UE::Sequencer

#undef LOCTEXT_NAMESPACE

