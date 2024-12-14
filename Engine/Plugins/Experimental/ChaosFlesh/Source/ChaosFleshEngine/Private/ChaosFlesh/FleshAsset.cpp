// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FleshAsset.cpp: UFleshAsset methods.
=============================================================================*/
#include "ChaosFlesh/FleshAsset.h"
#include "ChaosFlesh/FleshCollection.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dataflow/DataflowContent.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryCollection/TransformCollection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FleshAsset)

DEFINE_LOG_CATEGORY_STATIC(LogFleshAssetInternal, Log, All);


FFleshAssetEdit::FFleshAssetEdit(UFleshAsset* InAsset, FPostEditFunctionCallback InCallback)
	: PostEditCallback(InCallback)
	, Asset(InAsset)
{
}

FFleshAssetEdit::~FFleshAssetEdit()
{
	PostEditCallback();
}

FFleshCollection* FFleshAssetEdit::GetFleshCollection()
{
	if (Asset)
	{
		return Asset->FleshCollection.Get();
	}
	return nullptr;
}

UFleshAsset::UFleshAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FleshCollection(new FFleshCollection())
{
}

void UFleshAsset::SetCollection(FFleshCollection* InCollection)
{
	FleshCollection = TSharedPtr<FFleshCollection, ESPMode::ThreadSafe>(InCollection);
	Modify();
}


void UFleshAsset::PostEditCallback()
{
	//UE_LOG(LogFleshAssetInternal, Log, TEXT("UFleshAsset::PostEditCallback()"));
}

TManagedArray<FVector3f>& UFleshAsset::GetPositions()
{
	return FleshCollection->ModifyAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
}

const TManagedArray<FVector3f>* UFleshAsset::FindPositions() const
{
	return FleshCollection->FindAttributeTyped<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
}

/** Serialize */
void UFleshAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	bool bCreateSimulationData = false;
	Chaos::FChaosArchive ChaosAr(Ar);
	FleshCollection->Serialize(ChaosAr);
}

TObjectPtr<UDataflowBaseContent> UFleshAsset::CreateDataflowContent()
{
	TObjectPtr<UDataflowFleshContent> SkeletalContent = NewObject<UDataflowFleshContent>(this, UDataflowFleshContent::StaticClass());
	SkeletalContent->SetIsSaved(false);

	SkeletalContent->SetDataflowOwner(this);
	SkeletalContent->SetTerminalAsset(this);

	WriteDataflowContent(SkeletalContent);
	
	return SkeletalContent;
}

void UFleshAsset::WriteDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent) const
{
	if(const TObjectPtr<UDataflowFleshContent> SkeletalContent = Cast<UDataflowFleshContent>(DataflowContent))
	{
		SkeletalContent->SetDataflowAsset(DataflowAsset);
		SkeletalContent->SetDataflowTerminal(DataflowTerminal);
		
		SkeletalContent->SetSkeletalMesh(SkeletalMesh, true);

#if WITH_EDITORONLY_DATA
		SkeletalContent->SetAnimationAsset(PreviewAnimationAsset.Get());
		SkeletalContent->SolverTiming = PreviewSolverTiming;
		SkeletalContent->SolverEvolution = PreviewSolverEvolution;
		SkeletalContent->SolverCollisions = PreviewSolverCollisions;
		SkeletalContent->SolverConstraints = PreviewSolverConstraints;
		SkeletalContent->SolverForces = PreviewSolverForces;
		SkeletalContent->SolverDebugging = PreviewSolverDebugging;
		SkeletalContent->SolverMuscleActivation = PreviewSolverMuscleActivation;
#endif
	}
}

void UFleshAsset::ReadDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent)
{
	if(const TObjectPtr<UDataflowFleshContent> SkeletalContent = Cast<UDataflowFleshContent>(DataflowContent))
	{
#if WITH_EDITORONLY_DATA
		PreviewAnimationAsset = SkeletalContent->GetAnimationAsset();
		PreviewSolverTiming = SkeletalContent->SolverTiming;
		PreviewSolverEvolution = SkeletalContent->SolverEvolution;
		PreviewSolverCollisions = SkeletalContent->SolverCollisions;
		PreviewSolverConstraints = SkeletalContent->SolverConstraints;
		PreviewSolverForces = SkeletalContent->SolverForces;
		PreviewSolverDebugging = SkeletalContent->SolverDebugging;
		PreviewSolverMuscleActivation = SkeletalContent->SolverMuscleActivation;
#endif
	}
}

#if WITH_EDITOR
void UFleshAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property->GetFName();
    
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UFleshAsset, SkeletalMesh))
	{
		if(SkeletalMesh && (SkeletalMesh->GetSkeleton() != Skeleton))
		{
			Skeleton = SkeletalMesh->GetSkeleton();
		}
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UFleshAsset, Skeleton))
	{
		if(SkeletalMesh && (SkeletalMesh->GetSkeleton() != Skeleton))
		{
			SkeletalMesh = nullptr;
		}
	}
	InvalidateDataflowContents();
}
#endif //if WITH_EDITOR

UDataflowFleshContent::UDataflowFleshContent() : Super()
{
	bHideSkeletalMesh = false;
	bHideAnimationAsset = false;
}

void UDataflowFleshContent::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	UDataflowFleshContent* This = CastChecked<UDataflowFleshContent>(InThis);
	Super::AddReferencedObjects(InThis, Collector);
}

void UDataflowFleshContent::SetActorProperties(TObjectPtr<AActor>& PreviewActor) const
{
	Super::SetActorProperties(PreviewActor);
	OverrideStructProperty(PreviewActor, SolverTiming, TEXT("SolverTiming"));
	OverrideStructProperty(PreviewActor, SolverEvolution, TEXT("SolverEvolution"));
	OverrideStructProperty(PreviewActor, SolverCollisions, TEXT("SolverCollisions"));
	OverrideStructProperty(PreviewActor, SolverConstraints, TEXT("SolverConstraints"));
	OverrideStructProperty(PreviewActor, SolverForces, TEXT("SolverForces"));
	OverrideStructProperty(PreviewActor, SolverDebugging, TEXT("SolverDebugging"));
	OverrideStructProperty(PreviewActor, SolverMuscleActivation, TEXT("SolverMuscleActivation"));
}





