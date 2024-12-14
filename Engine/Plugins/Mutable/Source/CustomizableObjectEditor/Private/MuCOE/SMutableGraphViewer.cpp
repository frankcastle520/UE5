// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/SMutableGraphViewer.h"

#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Views/TableViewMetadata.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "MuCOE/CustomizableObjectCompileRunnable.h"
#include "MuCOE/CustomizableObjectEditorStyle.h"
#include "MuCOE/SMutableCodeViewer.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "Widgets/Views/STreeView.h"
#include "ScopedTransaction.h"
#include "MuT/NodeColourConstant.h"
#include "MuT/NodeColourFromScalars.h"
#include "MuT/NodeColourParameter.h"
#include "MuT/NodeColourSampleImage.h"
#include "MuT/NodeColourSwitch.h"
#include "MuT/NodeComponentEdit.h"
#include "MuT/NodeImageFormat.h"
#include "MuT/NodeImageInterpolate.h"
#include "MuT/NodeImageInvert.h"
#include "MuT/NodeImageLayer.h"
#include "MuT/NodeImageLayerColour.h"
#include "MuT/NodeImageMipmap.h"
#include "MuT/NodeImageMultiLayer.h"
#include "MuT/NodeImagePlainColour.h"
#include "MuT/NodeImageProject.h"
#include "MuT/NodeImageResize.h"
#include "MuT/NodeImageSwitch.h"
#include "MuT/NodeImageSwizzle.h"
#include "MuT/NodeImageTable.h"
#include "MuT/NodeObjectGroup.h"
#include "MuT/NodeObjectNew.h"
#include "MuT/NodeSurfaceNew.h"
#include "MuT/NodeSurfaceSwitch.h"
#include "MuT/NodeSurfaceVariation.h"
#include "MuT/NodeLOD.h"
#include "MuT/NodeMeshConstant.h"
#include "MuT/NodeMeshFormat.h"
#include "MuT/NodeMeshFragment.h"
#include "MuT/NodeMeshMakeMorph.h"
#include "MuT/NodeMeshMorph.h"
#include "MuT/NodeMeshTable.h"
#include "MuT/NodeModifierMeshClipDeform.h"
#include "MuT/NodeModifierMeshClipMorphPlane.h"
#include "MuT/NodeModifierMeshClipWithUVMask.h"
#include "MuT/NodeModifierMeshClipMorphPlane.h"
#include "MuT/NodeModifierSurfaceEdit.h"
#include "MuT/NodeScalarConstant.h"
#include "MuT/NodeScalarCurve.h"
#include "MuT/NodeScalarSwitch.h"
#include "MuT/NodeScalarTable.h"
#include "MuT/NodeObjectNew.h"

#include "MuT/NodeObjectGroupPrivate.h"
#include "MuT/NodeImageFormatPrivate.h"
#include "MuT/NodeMeshFormatPrivate.h"
#include "MuT/NodeMeshConstantPrivate.h"
#include "MuT/NodeImageSwitchPrivate.h"
#include "MuT/NodeImageLayerColourPrivate.h"
#include "MuT/NodeImageLayerPrivate.h"
#include "MuT/NodeImageMipmapPrivate.h"
#include "MuT/NodeImageResizePrivate.h"
#include "MuT/NodeImageInterpolatePrivate.h"
#include "MuT/NodeImagePlainColourPrivate.h"
#include "MuT/NodeImageProjectPrivate.h"
#include "MuT/NodeMeshMorphPrivate.h"
#include "MuT/NodeScalarEnumParameterPrivate.h"
#include "MuT/NodeScalarParameterPrivate.h"
#include "MuT/NodeMeshMakeMorphPrivate.h"
#include "MuT/NodeScalarCurvePrivate.h"
#include "MuT/NodeProjectorPrivate.h"
#include "MuT/NodeImageInvertPrivate.h"
#include "MuT/NodeImageSwizzlePrivate.h"
#include "MuT/NodeImageMultiLayerPrivate.h"
#include "MuT/NodeModifierMeshTransformInMesh.h"
#include "Widgets/MutableExpanderArrow.h"

class FExtender;
class FReferenceCollector;
class FUICommandList;
class ITableRow;
class SWidget;
struct FGeometry;
struct FSlateBrush;

#define LOCTEXT_NAMESPACE "SMutableDebugger"

// \todo: multi-column tree
namespace MutableGraphTreeViewColumns
{
	static const FName Name("Name");
};


class SMutableGraphTreeRow : public STableRow<TSharedPtr<FMutableGraphTreeElement>>
{
public:

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FMutableGraphTreeElement>& InRowItem)
	{
		RowItem = InRowItem;

		FText MainLabel = FText::GetEmpty();
		if (RowItem->MutableNode)
		{
			mu::Node::EType MutableType = RowItem->MutableNode->GetType()->Type;
			
			const FString LabelString = RowItem->Prefix.IsEmpty() 
				? FString::Printf(TEXT("%d"), int32(MutableType) )
				: FString::Printf( TEXT("%s : %d"), *RowItem->Prefix, int32(MutableType) );

			MainLabel = FText::FromString(LabelString);
			if (RowItem->DuplicatedOf)
			{
				MainLabel = FText::FromString( FString::Printf(TEXT("%d (Duplicated)"), int32(MutableType)));
			}
		}
		else
		{
			MainLabel = FText::FromString( *RowItem->Prefix);
		}


		this->ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SMutableExpanderArrow, SharedThis(this))
			]

			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.Text(MainLabel)
			]
		];

		STableRow< TSharedPtr<FMutableGraphTreeElement> >::ConstructInternal(
			STableRow::FArguments()
			//.Style(FAppStyle::Get(), "DetailsView.TreeView.TableRow")
			.ShowSelection(true)
			, InOwnerTableView
		);

	}


private:

	TSharedPtr<FMutableGraphTreeElement> RowItem;
};


void SMutableGraphViewer::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Add UObjects here if we own any at some point
	//Collector.AddReferencedObject(CustomizableObject);
}


FString SMutableGraphViewer::GetReferencerName() const
{
	return TEXT("SMutableGraphViewer");
}


void SMutableGraphViewer::Construct(const FArguments& InArgs, const mu::NodePtr& InRootNode)
{
	DataTag = InArgs._DataTag;
	ReferencedRuntimeTextures = InArgs._ReferencedRuntimeTextures;
	ReferencedCompileTextures = InArgs._ReferencedCompileTextures;
	RootNode = InRootNode;

	FToolBarBuilder ToolbarBuilder(TSharedPtr<const FUICommandList>(), FMultiBoxCustomization::None, TSharedPtr<FExtender>(), true);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Visible);
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), "SlimToolBar");

	ToolbarBuilder.AddWidget(SNew(STextBlock).Text(MakeAttributeLambda([this]() { return FText::FromString(DataTag); })));

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		[
			ToolbarBuilder.MakeWidget()
		]
		+ SVerticalBox::Slot()
		.VAlign(VAlign_Fill)
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)
			+ SSplitter::Slot()
			.Value(0.25f)
			[
				SNew(SBorder)
				.BorderImage(UE_MUTABLE_GET_BRUSH("ToolPanel.GroupBorder"))
				.Padding(FMargin(4.0f, 4.0f))
				[
					SAssignNew(TreeView, STreeView<TSharedPtr<FMutableGraphTreeElement>>)
					.TreeItemsSource(&RootNodes)
					.OnGenerateRow(this,&SMutableGraphViewer::GenerateRowForNodeTree)
					.OnGetChildren(this, &SMutableGraphViewer::GetChildrenForInfo)
					.OnSetExpansionRecursive(this, &SMutableGraphViewer::TreeExpandRecursive)
					.OnContextMenuOpening(this, &SMutableGraphViewer::OnTreeContextMenuOpening)
					.SelectionMode(ESelectionMode::Single)
					.HeaderRow
					(
						SNew(SHeaderRow)
						+ SHeaderRow::Column(MutableGraphTreeViewColumns::Name)
						.FillWidth(25.f)
						.DefaultLabel(LOCTEXT("Node Name", "Node Name"))
					)
				]
			]
			+ SSplitter::Slot()
			.Value(0.75f)
			[
				SNew(SBorder)
				.BorderImage(UE_MUTABLE_GET_BRUSH("ToolPanel.GroupBorder"))
				.Padding(FMargin(4.0f, 4.0f))
				//[
				//	SubjectsTreeView->AsShared()
				//]
			]
		]
	];
	
	RebuildTree();
}


void SMutableGraphViewer::RebuildTree()
{
	RootNodes.Reset();
	ItemCache.Reset();
	MainItemPerNode.Reset();

	RootNodes.Add(MakeShareable(new FMutableGraphTreeElement(RootNode)));
	TreeView->RequestTreeRefresh();
	TreeExpandUnique();
}


TSharedRef<ITableRow> SMutableGraphViewer::GenerateRowForNodeTree(TSharedPtr<FMutableGraphTreeElement> InTreeNode, const TSharedRef<STableViewBase>& InOwnerTable)
{
	TSharedRef<SMutableGraphTreeRow> Row = SNew(SMutableGraphTreeRow, InOwnerTable, InTreeNode);
	return Row;
}

void SMutableGraphViewer::GetChildrenForInfo(TSharedPtr<FMutableGraphTreeElement> InInfo, TArray<TSharedPtr<FMutableGraphTreeElement>>& OutChildren)
{
// This is necessary because of problems with rtti information in other platforms. In any case, this part of the debugger is only useful in the standard editor.
#if PLATFORM_WINDOWS
	if (!InInfo->MutableNode)
	{
		return;
	}

	// If this is a duplicated of another row, don't provide its children.
	if (InInfo->DuplicatedOf)
	{
		return;
	}

	mu::Node* ParentNode = InInfo->MutableNode.get();
	uint32 InputIndex = 0;

	auto AddChildFunc = [this, ParentNode, &InputIndex, &OutChildren](mu::Node* ChildNode, const FString& Prefix)
	{
		if (ChildNode)
		{
			FItemCacheKey Key = { ParentNode, ChildNode, InputIndex };
			TSharedPtr<FMutableGraphTreeElement>* CachedItem = ItemCache.Find(Key);

			if (CachedItem)
			{
				OutChildren.Add(*CachedItem);
			}
			else
			{
				TSharedPtr<FMutableGraphTreeElement>* MainItemPtr = MainItemPerNode.Find(ChildNode);
				TSharedPtr<FMutableGraphTreeElement> Item = MakeShareable(new FMutableGraphTreeElement(ChildNode, MainItemPtr, Prefix));
				OutChildren.Add(Item);
				ItemCache.Add(Key, Item);

				if (!MainItemPtr)
				{
					MainItemPerNode.Add(ChildNode, Item);
				}
			}
		}
		else
		{
			// No mutable node has been provided so create a dummy tree element
			TSharedPtr<FMutableGraphTreeElement> Item = MakeShareable(new FMutableGraphTreeElement(nullptr, nullptr , Prefix));
			OutChildren.Add(Item);
		}
		++InputIndex;
	};

	if (ParentNode->GetType() == mu::NodeObjectNew::GetStaticType())
	{
		mu::NodeObjectNew* ObjectNew = StaticCast<mu::NodeObjectNew*>(ParentNode);
		for (int32 l = 0; l < ObjectNew->Components.Num(); ++l)
		{
			AddChildFunc(ObjectNew->Components[l].get(), TEXT("COMP") );
		}

		for (int32 Modifier = 0; Modifier < ObjectNew->Modifiers.Num(); Modifier++)
		{
			AddChildFunc(ObjectNew->Modifiers[Modifier].get(), FString::Printf(TEXT("MOD [%d]"), Modifier));
		}

		for (int32 l = 0; l < ObjectNew->Children.Num(); ++l)
		{
			AddChildFunc(ObjectNew->Children[l].get(), TEXT("CHILD"));
		}
	}

	else if (ParentNode->GetType() == mu::NodeObjectGroup::GetStaticType())
	{
		mu::NodeObjectGroup* ObjectGroup = StaticCast<mu::NodeObjectGroup*>(ParentNode);
		mu::NodeObjectGroup::Private* Private = ObjectGroup->GetPrivate();
		for (int32 l = 0; l < Private->m_children.Num(); ++l)
		{
			AddChildFunc(Private->m_children[l].get(), TEXT("CHILD"));
		}
	}

	else if (ParentNode->GetType() == mu::NodeSurfaceNew::GetStaticType())
	{
		mu::NodeSurfaceNew* SurfaceNew = StaticCast<mu::NodeSurfaceNew*>(ParentNode);
		AddChildFunc(SurfaceNew->Mesh.get(), TEXT("MESH"));

		for (int32 l = 0; l < SurfaceNew->Images.Num(); ++l)
		{
			AddChildFunc(SurfaceNew->Images[l].Image.get(), FString::Printf(TEXT("IMAGE [%s]"), *SurfaceNew->Images[l].Name));
		}

		for (int32 l = 0; l < SurfaceNew->Vectors.Num(); ++l)
		{
			AddChildFunc(SurfaceNew->Vectors[l].Vector.get(), FString::Printf(TEXT("VECTOR [%s]"), *SurfaceNew->Vectors[l].Name));
		}

		for (int32 l = 0; l < SurfaceNew->Scalars.Num(); ++l)
		{
			AddChildFunc(SurfaceNew->Scalars[l].Scalar.get(), FString::Printf(TEXT("SCALAR [%s]"), *SurfaceNew->Scalars[l].Name));
		}

		for (int32 l = 0; l < SurfaceNew->Strings.Num(); ++l)
		{
			AddChildFunc(SurfaceNew->Strings[l].String.get(), FString::Printf(TEXT("STRING [%s]"), *SurfaceNew->Strings[l].Name));
		}
	}

	else if (ParentNode->GetType() == mu::NodeModifierSurfaceEdit::GetStaticType())
	{
		mu::NodeModifierSurfaceEdit* SurfaceEdit = StaticCast<mu::NodeModifierSurfaceEdit*>(ParentNode);

		AddChildFunc(SurfaceEdit->MorphFactor.get(), FString::Printf(TEXT("MORPH_FACTOR [%s]"), *SurfaceEdit->MeshMorph));

		for (int32 LODIndex = 0; LODIndex < SurfaceEdit->LODs.Num(); ++LODIndex)
		{
			AddChildFunc(SurfaceEdit->LODs[LODIndex].MeshAdd.get(), FString::Printf(TEXT("LOD%d MESH_ADD"), LODIndex));
			AddChildFunc(SurfaceEdit->LODs[LODIndex].MeshRemove.get(), FString::Printf(TEXT("LOD%d MESH_REMOVE"), LODIndex));

			for (int32 l = 0; l < SurfaceEdit->LODs[LODIndex].Textures.Num(); ++l)
			{
				AddChildFunc(SurfaceEdit->LODs[LODIndex].Textures[l].Extend.get(), FString::Printf(TEXT("LOD%d EXTEND [%d]"), LODIndex, l));
				AddChildFunc(SurfaceEdit->LODs[LODIndex].Textures[l].PatchImage.get(), FString::Printf(TEXT("LOD%d PATCH IMAGE [%d]"), LODIndex, l));
				AddChildFunc(SurfaceEdit->LODs[LODIndex].Textures[l].PatchMask.get(), FString::Printf(TEXT("LOD%d PATCH MASK [%d]"), LODIndex, l));

			}
		}
	}

	else if (ParentNode->GetType() == mu::NodeSurfaceSwitch::GetStaticType())
	{
		mu::NodeSurfaceSwitch* SurfaceSwitch = StaticCast<mu::NodeSurfaceSwitch*>(ParentNode);
		AddChildFunc(SurfaceSwitch->Parameter.get(), TEXT("PARAM"));
		for (int32 l = 0; l < SurfaceSwitch->Options.Num(); ++l)
		{
			AddChildFunc(SurfaceSwitch->Options[l].get(), FString::Printf(TEXT("OPTION [%d]"), l));
		}
	}

	else if (ParentNode->GetType() == mu::NodeSurfaceVariation::GetStaticType())
	{
		mu::NodeSurfaceVariation* SurfaceVar = StaticCast<mu::NodeSurfaceVariation*>(ParentNode);
		for (int32 l = 0; l < SurfaceVar->DefaultSurfaces.Num(); ++l)
		{
			AddChildFunc(SurfaceVar->DefaultSurfaces[l].get(), FString::Printf(TEXT("DEF SURF [%d]"), l));
		}
		for (int32 l = 0; l < SurfaceVar->DefaultModifiers.Num(); ++l)
		{
			AddChildFunc(SurfaceVar->DefaultModifiers[l].get(), FString::Printf(TEXT("DEF MOD [%d]"), l));
		}

		for (int32 v = 0; v < SurfaceVar->Variations.Num(); ++v)
		{
			const mu::NodeSurfaceVariation::FVariation Var = SurfaceVar->Variations[v];
			for (int32 l = 0; l < Var.Surfaces.Num(); ++l)
			{
				AddChildFunc(Var.Surfaces[l].get(), FString::Printf(TEXT("VAR [%s] SURF [%d]"), *Var.Tag, l));
			}
			for (int32 l = 0; l < Var.Modifiers.Num(); ++l)
			{
				AddChildFunc(Var.Modifiers[l].get(), FString::Printf(TEXT("VAR [%s] MOD [%d]"), *Var.Tag, l));
			}
		}
	}
	
	else if (ParentNode->GetType() == mu::NodeLOD::GetStaticType())
	{
		mu::NodeLOD* LodVar = StaticCast<mu::NodeLOD*>(ParentNode);

		for (int32 SurfaceIndex = 0; SurfaceIndex < LodVar->Surfaces.Num(); SurfaceIndex++)
		{
			AddChildFunc(LodVar->Surfaces[SurfaceIndex].get(), FString::Printf(TEXT("SURFACE [%d]"), SurfaceIndex));
		}
	}
	
	else if (ParentNode->GetType() == mu::NodeComponentNew::GetStaticType())
	{
		mu::NodeComponentNew* ComponentVar = StaticCast<mu::NodeComponentNew*>(ParentNode);
		for (int32 LODIndex = 0; LODIndex < ComponentVar->LODs.Num(); LODIndex++)
		{
			AddChildFunc(ComponentVar->LODs[LODIndex].get(), FString::Printf(TEXT("LOD [%d]"), LODIndex));
		}
	}

	else if (ParentNode->GetType() == mu::NodeComponentEdit::GetStaticType())
	{
		mu::NodeComponentEdit* ComponentVar = StaticCast<mu::NodeComponentEdit*>(ParentNode);
		for (int32 LODIndex = 0; LODIndex < ComponentVar->LODs.Num(); LODIndex++)
		{
			AddChildFunc(ComponentVar->LODs[LODIndex].get(), FString::Printf(TEXT("LOD [%d]"), LODIndex));
		}
	}

	else if (ParentNode->GetType() == mu::NodeMeshConstant::GetStaticType())
	{
		mu::NodeMeshConstant* MeshConstantVar = StaticCast<mu::NodeMeshConstant*>(ParentNode);
		mu::NodeMeshConstant::Private* Private = MeshConstantVar->GetPrivate();
		for (int32 LayoutIndex = 0; LayoutIndex < Private->Layouts.Num(); LayoutIndex++)
		{
			AddChildFunc(Private->Layouts[LayoutIndex].get(), FString::Printf(TEXT("LAYOUT [%d]"), LayoutIndex));
		}
	}

	else if (ParentNode->GetType() == mu::NodeImageFormat::GetStaticType())
	{
		mu::NodeImageFormat* ImageFormatVar = StaticCast<mu::NodeImageFormat*>(ParentNode);
		mu::NodeImageFormat::Private* Private = ImageFormatVar->GetPrivate();
		AddChildFunc(Private->m_source.get(), FString::Printf(TEXT("SOURCE IMAGE")));
	}

	else if (ParentNode->GetType() == mu::NodeMeshFormat::GetStaticType())
	{
		mu::NodeMeshFormat* MeshFormatVar = StaticCast<mu::NodeMeshFormat*>(ParentNode);
		mu::NodeMeshFormat::Private* Private = MeshFormatVar->GetPrivate();
		AddChildFunc(Private->Source.get(), FString::Printf(TEXT("SOURCE MESH")));
	}

	else if (ParentNode->GetType() == mu::NodeModifierMeshClipMorphPlane::GetStaticType())
	{
		// Nothing to show
	}

	else if (ParentNode->GetType() == mu::NodeModifierMeshClipWithMesh::GetStaticType())
	{
		mu::NodeModifierMeshClipWithMesh* ModifierMeshClipWithMeshVar = StaticCast<mu::NodeModifierMeshClipWithMesh*>(ParentNode);
		AddChildFunc(ModifierMeshClipWithMeshVar->ClipMesh.get(), FString::Printf(TEXT("CLIP MESH")));
	}

	else if (ParentNode->GetType() == mu::NodeModifierMeshClipDeform::GetStaticType())
	{
		mu::NodeModifierMeshClipDeform* ModifierMeshClipDeformVar = StaticCast<mu::NodeModifierMeshClipDeform*>(ParentNode);
		AddChildFunc(ModifierMeshClipDeformVar->ClipMesh.get(), FString::Printf(TEXT("CLIP MESH")));
	}

	else if (ParentNode->GetType() == mu::NodeModifierMeshClipWithUVMask::GetStaticType())
	{
		mu::NodeModifierMeshClipWithUVMask* ModifierMeshClipWithUVMaskVar = StaticCast<mu::NodeModifierMeshClipWithUVMask*>(ParentNode);
		AddChildFunc(ModifierMeshClipWithUVMaskVar->ClipMask.get(), FString::Printf(TEXT("CLIP MASK")));
		AddChildFunc(ModifierMeshClipWithUVMaskVar->ClipLayout.get(), FString::Printf(TEXT("CLIP LAYOUT")));
	}

	else if (ParentNode->GetType() == mu::NodeModifierMeshTransformInMesh::GetStaticType())
	{
		mu::NodeModifierMeshTransformInMesh* ModifierMeshTransformInMeshVar = StaticCast<mu::NodeModifierMeshTransformInMesh*>(ParentNode);
		AddChildFunc(ModifierMeshTransformInMeshVar->BoundingMesh.get(), FString::Printf(TEXT("BOUNDING MESH")));
		AddChildFunc(ModifierMeshTransformInMeshVar->MatrixNode.get(), FString::Printf(TEXT("MESH TRANSFORM")));
	}

	else if (ParentNode->GetType() == mu::NodeImageSwitch::GetStaticType())
	{
		mu::NodeImageSwitch* ImageSwitchVar = StaticCast<mu::NodeImageSwitch*>(ParentNode);
		mu::NodeImageSwitch::Private* Private = ImageSwitchVar->GetPrivate();
		AddChildFunc(Private->m_pParameter.get(), FString::Printf(TEXT("PARAM")));
		for (int32 OptionIndex = 0; OptionIndex < Private->m_options.Num(); OptionIndex++)
		{
			AddChildFunc(Private->m_options[OptionIndex].get(), FString::Printf(TEXT("OPTION [%d]"), OptionIndex));
		}
	}

	else if (ParentNode->GetType() == mu::NodeImageMipmap::GetStaticType())
	{
		mu::NodeImageMipmap* ImageMipMapVar = StaticCast<mu::NodeImageMipmap*>(ParentNode);
		mu::NodeImageMipmap::Private* Private = ImageMipMapVar->GetPrivate();
		AddChildFunc(Private->m_pSource.get(), FString::Printf(TEXT("SOURCE")));
		AddChildFunc(Private->m_pFactor.get(), FString::Printf(TEXT("FACTOR")));
	}

	else if (ParentNode->GetType() == mu::NodeImageLayer::GetStaticType())
	{
		mu::NodeImageLayer* ImageLayerVar = StaticCast<mu::NodeImageLayer*>(ParentNode);
		mu::NodeImageLayer::Private* Private = ImageLayerVar->GetPrivate();
		AddChildFunc(Private->m_pBase.get(), FString::Printf(TEXT("BASE")));
		AddChildFunc(Private->m_pMask.get(), FString::Printf(TEXT("MASK")));
		AddChildFunc(Private->m_pBlended.get(), FString::Printf(TEXT("BLEND")));
	}
	
	else if (ParentNode->GetType() == mu::NodeImageLayerColour::GetStaticType())
	{
		mu::NodeImageLayerColour* ImageLayerColourVar = StaticCast<mu::NodeImageLayerColour*>(ParentNode);
		mu::NodeImageLayerColour::Private* Private = ImageLayerColourVar->GetPrivate();
		AddChildFunc(Private->m_pBase.get(), FString::Printf(TEXT("BASE")));
		AddChildFunc(Private->m_pMask.get(), FString::Printf(TEXT("MASK")));
		AddChildFunc(Private->m_pColour.get(), FString::Printf(TEXT("COLOR")));
	}

	else if (ParentNode->GetType() == mu::NodeImageResize::GetStaticType())
	{
		mu::NodeImageResize* ImageResizeVar = StaticCast<mu::NodeImageResize*>(ParentNode);
		mu::NodeImageResize::Private* Private = ImageResizeVar->GetPrivate();
		AddChildFunc(Private->m_pBase.get(), FString::Printf(TEXT("BASE")));
	}

	else if (ParentNode->GetType() == mu::NodeMeshMorph::GetStaticType())
	{
		mu::NodeMeshMorph* MeshMorphVar = StaticCast<mu::NodeMeshMorph*>(ParentNode);
		mu::NodeMeshMorph::Private* Private = MeshMorphVar->GetPrivate();
		AddChildFunc(Private->Base.get(), FString::Printf(TEXT("BASE")));
		AddChildFunc(Private->Morph.get(), FString::Printf(TEXT("MORPH")));
		AddChildFunc(Private->Factor.get(), FString::Printf(TEXT("FACTOR")));
	}

	else if (ParentNode->GetType() == mu::NodeImageProject::GetStaticType())
	{
		mu::NodeImageProject* ImageProjectVar = StaticCast<mu::NodeImageProject*>(ParentNode);
		mu::NodeImageProject::Private* Private = ImageProjectVar->GetPrivate();
		AddChildFunc(Private->m_pProjector.get(), FString::Printf(TEXT("PROJECTOR")));
		AddChildFunc(Private->m_pMesh.get(), FString::Printf(TEXT("MESH")));
		AddChildFunc(Private->m_pImage.get(), FString::Printf(TEXT("IMAGE")));
		AddChildFunc(Private->m_pMask.get(), FString::Printf(TEXT("MASK")));
		AddChildFunc(Private->m_pAngleFadeStart.get(), FString::Printf(TEXT("FADE START ANGLE")));
		AddChildFunc(Private->m_pAngleFadeEnd.get(), FString::Printf(TEXT("FADE END ANGLE")));
	}

	else if (ParentNode->GetType() == mu::NodeImagePlainColour::GetStaticType())
	{
		mu::NodeImagePlainColour* ImagePlainColourVar = StaticCast<mu::NodeImagePlainColour*>(ParentNode);
        mu::NodeImagePlainColour::Private* Private = ImagePlainColourVar->GetPrivate();
		AddChildFunc(Private->m_pColour.get(), FString::Printf(TEXT("COLOR")));
	}

	else if (ParentNode->GetType() == mu::NodeLayout::GetStaticType())
	{
		// Nothing to show
	}

	else if (ParentNode->GetType() == mu::NodeScalarEnumParameter::GetStaticType())
	{
		mu::NodeScalarEnumParameter* ScalarEnumParameterVar = StaticCast<mu::NodeScalarEnumParameter*>(ParentNode);
		mu::NodeScalarEnumParameter::Private* Private = ScalarEnumParameterVar->GetPrivate();
		for (int32 RangeIndex = 0; RangeIndex < Private->m_ranges.Num(); RangeIndex++)
		{
			AddChildFunc(Private->m_ranges[RangeIndex].get(), FString::Printf(TEXT("RANGE [%d]"), RangeIndex));
		}
	}

	else if (ParentNode->GetType() == mu::NodeMeshFragment::GetStaticType())
	{
		mu::NodeMeshFragment* MeshFragmentVar = StaticCast<mu::NodeMeshFragment*>(ParentNode);
		AddChildFunc(MeshFragmentVar->SourceMesh.get(), FString::Printf(TEXT("MESH")));
	}

	else if (ParentNode->GetType() == mu::NodeColourSampleImage::GetStaticType())
	{
		mu::NodeColourSampleImage* ColorSampleImageVar = StaticCast<mu::NodeColourSampleImage*>(ParentNode);
		AddChildFunc(ColorSampleImageVar->Image.get(), FString::Printf(TEXT("IMAGE")));
		AddChildFunc(ColorSampleImageVar->X.get(), FString::Printf(TEXT("X")));
		AddChildFunc(ColorSampleImageVar->Y.get(), FString::Printf(TEXT("Y")));
	}

	else if (ParentNode->GetType() == mu::NodeImageInterpolate::GetStaticType())
	{
		mu::NodeImageInterpolate* ImageInterpolateVar = StaticCast<mu::NodeImageInterpolate*>(ParentNode);
		mu::NodeImageInterpolate::Private* Private = ImageInterpolateVar->GetPrivate();
		AddChildFunc(Private->m_pFactor.get(), FString::Printf(TEXT("FACTOR")));
		for (int32 TargetIndex = 0; TargetIndex < Private->m_targets.Num(); TargetIndex++)
		{
			AddChildFunc(Private->m_targets[TargetIndex].get(), FString::Printf(TEXT("TARGET [%d]"), TargetIndex));
		}
	}
	
	else if (ParentNode->GetType() == mu::NodeScalarConstant::GetStaticType())
	{
		// Nothing to show
	}

	else if (ParentNode->GetType() == mu::NodeScalarParameter::GetStaticType())
	{
		mu::NodeScalarParameter* ScalarParameterVar = StaticCast<mu::NodeScalarParameter*>(ParentNode);
		mu::NodeScalarParameter::Private* Private = ScalarParameterVar->GetPrivate();
		for (int32 RangeIndex = 0; RangeIndex < Private->m_ranges.Num(); RangeIndex++)
		{
			AddChildFunc(Private->m_ranges[RangeIndex].get(), FString::Printf(TEXT("RANGE [%d]"), RangeIndex));
		}
	}

	else if (ParentNode->GetType() == mu::NodeColourParameter::GetStaticType())
	{
		mu::NodeColourParameter* ColorParameterVar = StaticCast<mu::NodeColourParameter*>(ParentNode);
		for (int32 RangeIndex = 0; RangeIndex < ColorParameterVar->Ranges.Num(); RangeIndex++)
		{
			AddChildFunc(ColorParameterVar->Ranges[RangeIndex].get(), FString::Printf(TEXT("RANGE [%d]"), RangeIndex));
		}
	}

	else if (ParentNode->GetType() == mu::NodeColourConstant::GetStaticType())
	{
		// Nothing to show
	}


	else if (ParentNode->GetType() == mu::NodeImageConstant::GetStaticType())
	{
		// Nothing to show
	}

	
	else if (ParentNode->GetType() == mu::NodeScalarCurve::GetStaticType())
	{
		mu::NodeScalarCurve* ScalarCurveVar = StaticCast<mu::NodeScalarCurve*>(ParentNode);
		AddChildFunc(ScalarCurveVar->CurveSampleValue.get(), FString::Printf(TEXT("INPUT")));
	}

	else if (ParentNode->GetType() == mu::NodeMeshMakeMorph::GetStaticType())
	{
		mu::NodeMeshMakeMorph* MeshMakeMorphVar = StaticCast<mu::NodeMeshMakeMorph*>(ParentNode);
		mu::NodeMeshMakeMorph::Private* Private = MeshMakeMorphVar->GetPrivate();
		AddChildFunc(Private->m_pBase.get(), FString::Printf(TEXT("BASE")));
		AddChildFunc(Private->m_pTarget.get(), FString::Printf(TEXT("TARGET")));
	}
	
	else if (ParentNode->GetType() == mu::NodeProjectorParameter::GetStaticType())
	{
		mu::NodeProjectorParameter* ProjectorParameterVar = StaticCast<mu::NodeProjectorParameter*>(ParentNode);
		mu::NodeProjectorParameter::Private* Private = ProjectorParameterVar->GetPrivate();
		for (int32 RangeIndex = 0; RangeIndex < Private->m_ranges.Num(); RangeIndex++)
		{
			AddChildFunc(Private->m_ranges[RangeIndex].get(), FString::Printf(TEXT("RANGE [%d]"), RangeIndex));
		}
	}

	else if (ParentNode->GetType() == mu::NodeProjectorConstant::GetStaticType())
	{
		// Nothing to show
	}

	else if (ParentNode->GetType() == mu::NodeColourSwitch::GetStaticType())
	{
		mu::NodeColourSwitch* ColorSwitchVar = StaticCast<mu::NodeColourSwitch*>(ParentNode);
		AddChildFunc(ColorSwitchVar->Parameter.get(), TEXT("PARAM"));
		for (int32 OptionIndex = 0; OptionIndex < ColorSwitchVar->Options.Num(); ++OptionIndex)
		{
			AddChildFunc(ColorSwitchVar->Options[OptionIndex].get(), FString::Printf(TEXT("OPTION [%d]"), OptionIndex));
		}
	}

	else if (ParentNode->GetType() == mu::NodeImageSwizzle::GetStaticType())
	{
		mu::NodeImageSwizzle* ImageSwizzleVar = StaticCast<mu::NodeImageSwizzle*>(ParentNode);
		mu::NodeImageSwizzle::Private* Private = ImageSwizzleVar->GetPrivate();
		for (int32 SourceIndex = 0; SourceIndex < Private->m_sources.Num(); ++SourceIndex)
		{
			AddChildFunc(Private->m_sources[SourceIndex].get(), FString::Printf(TEXT("SOURCE [%d]"), SourceIndex));
		}
	}
	
	else if (ParentNode->GetType() == mu::NodeImageInvert::GetStaticType())
	{
		mu::NodeImageInvert* ImageInvertVar = StaticCast<mu::NodeImageInvert*>(ParentNode);
		mu::NodeImageInvert::Private* Private = ImageInvertVar->GetPrivate();
		AddChildFunc(Private->m_pBase.get(), TEXT("BASE"));
	}

	else if (ParentNode->GetType() == mu::NodeImageMultiLayer::GetStaticType())
	{
		mu::NodeImageMultiLayer* ImageMultilayerVar = StaticCast<mu::NodeImageMultiLayer*>(ParentNode);
		mu::NodeImageMultiLayer::Private* Private = ImageMultilayerVar->GetPrivate();
		AddChildFunc(Private->m_pBase.get(), FString::Printf(TEXT("BASE")));
		AddChildFunc(Private->m_pMask.get(), FString::Printf(TEXT("MASK")));
		AddChildFunc(Private->m_pBlended.get(), FString::Printf(TEXT("BLEND")));
		AddChildFunc(Private->m_pRange.get(), FString::Printf(TEXT("RANGE")));
	}
	
	else if (ParentNode->GetType() == mu::NodeImageTable::GetStaticType())
	{
		// No nodes to show
	}

	else if (ParentNode->GetType() == mu::NodeMeshTable::GetStaticType())
	{
		mu::NodeMeshTable* MeshTableVar = StaticCast<mu::NodeMeshTable*>(ParentNode);
		for (int32 LayoutIndex = 0; LayoutIndex < MeshTableVar->Layouts.Num(); ++LayoutIndex)
		{
			AddChildFunc(MeshTableVar->Layouts[LayoutIndex].get(), FString::Printf(TEXT("LAYOUT [%d]"), LayoutIndex));
		}
	}

	else if (ParentNode->GetType() == mu::NodeScalarTable::GetStaticType())
	{
		// Nothing to show
	}

	else if (ParentNode->GetType() == mu::NodeScalarSwitch::GetStaticType())
	{
		mu::NodeScalarSwitch* ScalarSwitchVar = StaticCast<mu::NodeScalarSwitch*>(ParentNode);
		AddChildFunc(ScalarSwitchVar->Parameter.get(), TEXT("PARAM"));
		for (int32 OptionIndex = 0; OptionIndex < ScalarSwitchVar->Options.Num(); ++OptionIndex)
		{
			AddChildFunc(ScalarSwitchVar->Options[OptionIndex].get(), FString::Printf(TEXT("OPTION [%d]"), OptionIndex));
		}
	}
	
	else if (ParentNode->GetType() == mu::NodeColourFromScalars::GetStaticType())
	{
		mu::NodeColourFromScalars* ScalarTableVar = StaticCast<mu::NodeColourFromScalars*>(ParentNode);
		AddChildFunc(ScalarTableVar->X.get(), TEXT("X"));
		AddChildFunc(ScalarTableVar->Y.get(), TEXT("Y"));
		AddChildFunc(ScalarTableVar->Z.get(), TEXT("Z"));
		AddChildFunc(ScalarTableVar->W.get(), TEXT("W"));
	}
	
	else
	{
		UE_LOG(LogMutable,Error,TEXT("The node of type %d has not been implemented, so its children won't be added to the tree."), int32(ParentNode->GetType()->Type));

		// Add a placeholder to the tree
		const FString Prefix =  FString::Printf(TEXT("[%d] NODE TYPE NOT IMPLEMENTED"), int32(ParentNode->GetType()->Type));
		AddChildFunc(nullptr, Prefix);
	}
#endif
}


TSharedPtr<SWidget> SMutableGraphViewer::OnTreeContextMenuOpening()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Graph_Expand_Instance", "Expand Instance-Level Operations"),
		LOCTEXT("Graph_Expand_Instance_Tooltip", "Expands all the operations in the tree that are instance operations (not images, meshes, booleans, etc.)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SMutableGraphViewer::TreeExpandUnique)
			//, FCanExecuteAction::CreateSP(this, &SMutableCodeViewer::HasAnyItemInPalette)
		)
	);

	return MenuBuilder.MakeWidget();
}


void SMutableGraphViewer::TreeExpandRecursive(TSharedPtr<FMutableGraphTreeElement> InInfo, bool bExpand)
{
	if (bExpand)
	{
		TreeExpandUnique();
	}
}


void SMutableGraphViewer::TreeExpandUnique()
{
	TArray<TSharedPtr<FMutableGraphTreeElement>> Pending = RootNodes;

	TSet<TSharedPtr<FMutableGraphTreeElement>> Processed;

	TArray<TSharedPtr<FMutableGraphTreeElement>> Children;

	while (!Pending.IsEmpty())
	{
		TSharedPtr<FMutableGraphTreeElement> Item = Pending.Pop();
		TreeView->SetItemExpansion(Item, true);

		Children.SetNum(0);
		GetChildrenForInfo(Item, Children);
		Pending.Append(Children);
	}
}


#undef LOCTEXT_NAMESPACE 


