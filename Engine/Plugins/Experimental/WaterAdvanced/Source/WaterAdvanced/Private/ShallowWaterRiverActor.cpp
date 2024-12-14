// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShallowWaterRiverActor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "WaterBodyActor.h"
#include "WaterSplineComponent.h"
#include "BakedShallowWaterSimulationComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTarget2DArray.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "TextureResource.h"
#include "ShallowWaterCommon.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Math/Float16Color.h"
#include "Kismet/GameplayStatics.h"
#include "Landscape.h"

UShallowWaterRiverComponent::UShallowWaterRiverComponent(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	PrimaryComponentTick.bCanEverTick = true;	

#if WITH_EDITORONLY_DATA
	bTickInEditor = true;
#endif // WITH_EDITORONLY_DATA

	bIsInitialized = false;
	bTickInitialize = false;

	ResolutionMaxAxis = 512;
	SourceSize = 1000;

	// #todo(dmp): default river system should be set here
	//hap = LoadObject<UNiagaraSystem>(nullptr, TEXT("/WaterAdvanced/Niagara/Systems/Grid2D_SW_River.Grid2D_SW_River"));

#if WITH_EDITOR
	// Add all landscapes by default
	/*
	if (BottomContourActors.IsEmpty())
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscape::StaticClass(), FoundActors);

		for (AActor* CurrActor : FoundActors)
		{
			BottomContourActors.Add(CurrActor);
		}
	}
	*/

	// start with one empty element
	if (BottomContourActors.IsEmpty())
	{
		BottomContourActors.AddDefaulted();
	}
#endif

}

void UShallowWaterRiverComponent::PostLoad()
{
	Super::PostLoad();

	if (RenderState == EShallowWaterRenderState::LiveSim || RiverSimSystem == nullptr)
	{
	#if WITH_EDITOR
		bIsInitialized = false;
		bTickInitialize = false;

		Rebuild();
	#endif
	}
	else
	{
		RiverSimSystem->Activate();
	}

	UpdateRenderState();
}

void UShallowWaterRiverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
#if WITH_EDITOR
	// lots of tick ordering issues, so we try to initialize on the first tick too
	if (RiverSimSystem == nullptr || (RenderState == EShallowWaterRenderState::LiveSim && !bIsInitialized && !bTickInitialize))
	{
		bTickInitialize = true;
		Rebuild();
	}
	else
	{
		RiverSimSystem->Activate();	
	}
#endif
}

void UShallowWaterRiverComponent::BeginPlay()
{
	Super::BeginPlay();

	UpdateRenderState();
}

void UShallowWaterRiverComponent::OnUnregister()
{
	Super::OnUnregister();
}

#if WITH_EDITOR

void UShallowWaterRiverComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName;
	if (PropertyChangedEvent.Property)
	{
		PropertyName = PropertyChangedEvent.Property->GetFName();
	}
			
	// this should go before rebuild not after...something is wrong
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UShallowWaterRiverComponent, RenderState) && RiverSimSystem != nullptr && RiverSimSystem->IsActive())
	{
		RiverSimSystem->SetVariableBool(FName("ReadCachedSim"), RenderState == EShallowWaterRenderState::BakedSim);
	}
	else
	{
		Rebuild();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UShallowWaterRiverComponent, RenderState))
	{
		UpdateRenderState();
	}
}

void UShallowWaterRiverComponent::Rebuild()
{	
	if (RiverSimSystem != nullptr)
	{
		RiverSimSystem->SetActive(false);
		RiverSimSystem->DestroyComponent();
		RiverSimSystem = nullptr;
	}
	
	if (NiagaraRiverSimulation == nullptr)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::Rebuild() - null Niagara system asset"));
	}

	AllWaterBodies.Empty();

	// collect all the water bodies	
	if (SourceRiverWaterBody != nullptr)
	{
		AllWaterBodies.Add(SourceRiverWaterBody);
	}
	else	
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::Rebuild() - No source water body specified"));
		return;
	}
	
	if (SinkRiverWaterBody != nullptr)
	{
		AllWaterBodies.Add(SinkRiverWaterBody);
	}	

	for (TObjectPtr<AWaterBody > CurrWaterBody : AdditonalRiverWaterBodies)
	{
		AllWaterBodies.Add(CurrWaterBody);
	}

	if (AllWaterBodies.Num() == 0)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::Rebuild() - No water bodies specified"));
		return;
	}

	// accumulate bounding box for river water bodies
	FBoxSphereBounds::Builder CombinedWorldBoundsBuilder;
	for (TObjectPtr<AWaterBody > CurrWaterBody : AllWaterBodies)
	{
		if (CurrWaterBody != nullptr)
		{
			TObjectPtr<UWaterBodyComponent> CurrWaterBodyComponent = CurrWaterBody->GetWaterBodyComponent();

			if (CurrWaterBodyComponent != nullptr)
			{
				// accumulate bounds
				FBoxSphereBounds WorldBounds;
				CurrWaterBody->GetActorBounds(true, WorldBounds.Origin, WorldBounds.BoxExtent);				

				CombinedWorldBoundsBuilder += WorldBounds;
			}			
		}
		else
		{
			UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::Rebuild() - skipping null water body actor found"));
			continue;
		}
	}
	FBoxSphereBounds CombinedBounds(CombinedWorldBoundsBuilder);

	if (CombinedBounds.BoxExtent.Length() < SMALL_NUMBER)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::Rebuild() - river bodies have zero bounds"));
		return;
	}

	FVector SourcePos(0, 0, 0);
	float SourceWidth = 1;
	float SourceDepth = 1;
	FVector SourceDir(1, 0, 0);	

	// Get source
	if (!QueryWaterAtSplinePoint(SourceRiverWaterBody, 0, SourcePos, SourceDir, SourceWidth, SourceDepth))
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::Rebuild() - water source query failed"));
		return;
	}

	FVector SinkPos(0, 0, 0);
	float SinkWidth = 1;
	float SinkDepth = 1;
	FVector SinkDir(1, 0, 0);

	// Get Sink		
	TObjectPtr<AWaterBody> SinkToUse = SinkRiverWaterBody;

	// if no sink specified, use source
	if (SinkToUse == nullptr)
	{
		SinkToUse = SourceRiverWaterBody;
	}
	
	if (!QueryWaterAtSplinePoint(SinkToUse, -1, SinkPos, SinkDir, SinkWidth, SinkDepth))
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::Rebuild() - water sink query failed"));
		return;
	}

	SystemPos = CombinedBounds.Origin - FVector(0, 0, CombinedBounds.BoxExtent.Z);
	
	RiverSimSystem = NewObject<UNiagaraComponent>(this, NAME_None, RF_Public);
	RiverSimSystem->bUseAttachParentBound = false;
	RiverSimSystem->SetWorldLocation(SystemPos);

	if (GetWorld() && GetWorld()->bIsWorldInitialized)
	{
		if (!RiverSimSystem->IsRegistered())
		{
			RiverSimSystem->RegisterComponentWithWorld(GetWorld());
		}

		RiverSimSystem->SetVisibleFlag(true);
		RiverSimSystem->SetAsset(NiagaraRiverSimulation);
							
		// convert to raw ptr array for function library
		if (bUseCapture)
		{
			TArray<AActor*> BottomContourActorsRawPtr;
			for (TObjectPtr<AActor> CurrActor : BottomContourActors)
			{
				AActor* CurrActorRawPtr = CurrActor.Get();
				BottomContourActorsRawPtr.Add(CurrActorRawPtr);
			}

			FName DIName = "User.BottomCapture";

			UNiagaraFunctionLibrary::SetSceneCapture2DDataInterfaceManagedMode(RiverSimSystem, DIName,
				ESceneCaptureSource::SCS_SceneDepth,
				FIntPoint(ResolutionMaxAxis, ResolutionMaxAxis),
				ETextureRenderTargetFormat::RTF_R16f,
				ECameraProjectionMode::Orthographic,
				90.0f,
				FMath::Max(WorldGridSize.X, WorldGridSize.Y),
				true,
				false,
				BottomContourActorsRawPtr);

	
			// accumulate bounding box for river water bodies
			FBoxSphereBounds::Builder BottomContourCombinedWorldBoundsBuilder;
			for (TObjectPtr<AActor> BottomContourActor : BottomContourActors)
			{
				if (BottomContourActor != nullptr)
				{
					// accumulate bounds
					FBoxSphereBounds WorldBounds;
					BottomContourActor->GetActorBounds(false, WorldBounds.Origin, WorldBounds.BoxExtent);

					BottomContourCombinedWorldBoundsBuilder += WorldBounds;
				}
				else
				{
					UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::Rebuild() - skipping null bottom contour boundary actor found"));
					continue;
				}
			}
			FBoxSphereBounds CombinedBottomContourBounds(BottomContourCombinedWorldBoundsBuilder);				


			RiverSimSystem->ReinitializeSystem();
	
			RiverSimSystem->SetVariableFloat(FName("CaptureOffset"), BottomContourCaptureOffset + CombinedBottomContourBounds.Origin.Z + CombinedBottomContourBounds.BoxExtent.Z);
		}
		else
		{
			RiverSimSystem->ReinitializeSystem();
		}
	}
	else
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::Rebuild() - World not initialized"));
		return;
	}
	

	if (RiverSimSystem == nullptr)
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::Rebuild() - Cannot spawn river system"));
		return;
	}

	// look for the water info texture
	for (TObjectPtr<AWaterBody > CurrWaterBody : AllWaterBodies)
	{
		if (AWaterZone* WaterZone = CurrWaterBody->GetWaterBodyComponent()->GetWaterZone())
		{
			const TObjectPtr<UTextureRenderTarget2DArray> NewWaterInfoTexture = WaterZone->WaterInfoTextureArray;
			if (NewWaterInfoTexture == nullptr)
			{
				WaterZone->GetOnWaterInfoTextureArrayCreated().RemoveDynamic(this, &UShallowWaterRiverComponent::OnWaterInfoTextureArrayCreated);
				WaterZone->GetOnWaterInfoTextureArrayCreated().AddDynamic(this, &UShallowWaterRiverComponent::OnWaterInfoTextureArrayCreated);
			}
			else
			{
				OnWaterInfoTextureArrayCreated(NewWaterInfoTexture);
			}

			// The following index assume that there is no split screen support and will request the position of the first player's water view.
			const int32 PlayerIndex = 0;
			const FVector2D ZoneLocation = FVector2D(WaterZone->GetDynamicWaterInfoCenter(PlayerIndex));

			const FVector2D ZoneExtent = FVector2D(WaterZone->GetDynamicWaterInfoExtent());
			const FVector2D WaterHeightExtents = FVector2D(WaterZone->GetWaterHeightExtents());
			const float GroundZMin = WaterZone->GetGroundZMin();

			RiverSimSystem->SetVariableVec2(FName("WaterZoneLocation"), ZoneLocation);
			RiverSimSystem->SetVariableVec2(FName("WaterZoneExtent"), ZoneExtent);
			RiverSimSystem->SetVariableInt(FName("WaterZoneIdx"), WaterZone->GetWaterZoneIndex());

/*
			// generate high res texture signed distance to water body
			FVector2D::FReal CellSize = FMath::Max(WorldGridSize.X, WorldGridSize.Y) / ResolutionMaxAxis;

			FIntVector2 NumCells;
			NumCells.X = int32(FMath::FloorToInt(WorldGridSize.X / CellSize));
			NumCells.Y = int32(FMath::FloorToInt(WorldGridSize.Y / CellSize));

			// Pad grid by 1 voxel if our computed bounding box is too small
			if (WorldGridSize.X > WorldGridSize.Y && !FMath::IsNearlyEqual(CellSize * NumCells.Y, WorldGridSize.Y))
			{
				NumCells.Y++;
			}
			else if (WorldGridSize.X < WorldGridSize.Y && !FMath::IsNearlyEqual(CellSize * NumCells.X, WorldGridSize.X))
			{
				NumCells.X++;
			}

			UWaterSplineComponent* SplineComponent = CurrWaterBody->GetWaterBodyComponent()->GetWaterSpline();
			UWaterSplineMetadata* SplineMetadata = CurrWaterBody->GetWaterBodyComponent()->GetWaterSplineMetadata();
			FInterpCurveVector PositionCurve = SplineComponent->SplineCurves.Position;
			const FInterpCurveFloat& WidthCurve = SplineMetadata->RiverWidth;
			const Chaos::FRigidTransform3 WaterTransform = CurrWaterBody->GetWaterBodyComponent()->GetComponentTransform();

			TArray<FInterpCurvePointVector> &PositionPts = PositionCurve.Points;

			FInterpCurveVector PositionNew;
			for (FInterpCurvePointVector Pt : PositionPts)
			{
				FVector NewPos = Pt.OutVal;
				NewPos.Z = 0;

				PositionNew.AddPoint(Pt.InVal, NewPos);
			}			

			TArray<FFloat16> SignedDistanceValues;
			SignedDistanceValues.SetNum(NumCells.X * NumCells.Y);


			for (int y = 0; y < NumCells.Y; ++y) {
			for (int x = 0; x < NumCells.X; ++x) {
				FVector2D CurrIndex = FVector2D(x, y);
				

				const FVector2D UnitPos = (CurrIndex + .5) / FVector2D(NumCells.X, NumCells.Y);
				const FVector2D LocalPos = (UnitPos - .5) * WorldGridSize;
				const FVector LocalPos3D = FVector(LocalPos.X, LocalPos.Y, 0);
				FVector CellWorldPos = LocalPos3D + SystemPos;
				FVector WaterLocalPos = WaterTransform.InverseTransformPosition(CellWorldPos);


				// convert to world space
				float Dist;
				const float ClosestSplineKey = PositionCurve.FindNearest(WaterLocalPos, Dist);
				FVector ClosestPos = PositionCurve.Eval(ClosestSplineKey);
				float ClosestWidth = WidthCurve.Eval(ClosestSplineKey);

				// snap to xy plane
				//WaterLocalPos.Z = 0;
				//ClosestPos.Z = 0;
				
				const float SignedDistanceToWater = (WaterLocalPos - ClosestPos).Length() - ClosestWidth;

				SignedDistanceValues[x + NumCells.X * y] = SignedDistanceToWater < 0;
			}}

			SignedDistanceToSplineTexture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Public);
			SignedDistanceToSplineTexture->Source.Init(NumCells.X, NumCells.Y, 1, 1, TSF_R16F, (const uint8*) SignedDistanceValues.GetData());
			SignedDistanceToSplineTexture->PowerOfTwoMode = ETexturePowerOfTwoSetting::None;
			SignedDistanceToSplineTexture->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;			
			SignedDistanceToSplineTexture->UpdateResource();
			SignedDistanceToSplineTexture->PostEditChange();
*/

			break;
		}
	}

	RiverSimSystem->Activate();
	
	WorldGridSize = 2.0f * FVector2D(CombinedBounds.BoxExtent.X, CombinedBounds.BoxExtent.Y);
	RiverSimSystem->SetVariableVec2(FName("WorldGridSize"), WorldGridSize);
	RiverSimSystem->SetVariableInt(FName("ResolutionMaxAxis"), ResolutionMaxAxis);

	// pad out source's box height a so it intersects the sim plane.  This value doesn't matter much so we hardcode it
	float Overshoot = 1000.f;
	float FinalSourceHeight = 2. * CombinedBounds.BoxExtent.Z + Overshoot;

	RiverSimSystem->SetVariablePosition(FName("SourcePos"), SourcePos - FVector(0, 0, .5 * FinalSourceHeight) + FVector(SourceDir.X, SourceDir.Y, 0) * .5 * SourceSize);
	RiverSimSystem->SetVariableVec3(FName("SourceSize"), FVector(SourceWidth, SourceSize, FinalSourceHeight));
	RiverSimSystem->SetVariableFloat(FName("SourceAngle"), PI / 2.f + FMath::Acos(SourceDir.Dot(FVector(1,0,0))));
	
	// height of the sink box doesn't matter
	float SinkBoxHeight = 10000000;
	RiverSimSystem->SetVariablePosition(FName("SinkPos"), SinkPos);
	RiverSimSystem->SetVariableVec3(FName("SinkSize"), FVector(SinkWidth, SourceSize, SinkBoxHeight));
	RiverSimSystem->SetVariableFloat(FName("SinkAngle"), PI / 2.f + FMath::Acos(SinkDir.Dot(FVector(1, 0, 0))));

	RiverSimSystem->SetVariableFloat(FName("SimSpeed"), SimSpeed);
	RiverSimSystem->SetVariableInt(FName("NumSteps"), NumSteps);	
	
	BakedWaterSurfaceRT = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
	BakedWaterSurfaceRT->InitAutoFormat(1, 1);
	RiverSimSystem->SetVariableTextureRenderTarget(FName("SimGridRT"), BakedWaterSurfaceRT);
	RiverSimSystem->SetVariableBool(FName("ReadCachedSim"), RenderState == EShallowWaterRenderState::BakedSim || RenderState == EShallowWaterRenderState::WaterComponentWithBakedSim);

	if (BakedWaterSurfaceTexture != nullptr)
	{
		RiverSimSystem->SetVariableTexture(FName("BakedSimTexture"), BakedWaterSurfaceTexture);
	}

	bIsInitialized = true;
}

void UShallowWaterRiverComponent::Bake()
{	
	EObjectFlags TextureObjectFlags = EObjectFlags::RF_Public;

	BakedWaterSurfaceTexture = BakedWaterSurfaceRT->ConstructTexture2D(this, "BakedRiverTexture", TextureObjectFlags);

	RiverSimSystem->SetVariableTexture(FName("BakedSimTexture"), BakedWaterSurfaceTexture);	

	// Readback to get the river texture values as an array
	TArray<FFloat16Color> TmpShallowWaterSimArrayValues;
	BakedWaterSurfaceRT->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(TmpShallowWaterSimArrayValues);
		
	TArray<FVector4> ShallowWaterSimArrayValues;
	ShallowWaterSimArrayValues.Empty();
	ShallowWaterSimArrayValues.AddZeroed(TmpShallowWaterSimArrayValues.Num());

	// cast all values to floats
	int Index = 0;
	for (FFloat16Color Val : TmpShallowWaterSimArrayValues)
	{
		const float WaterHeight = Val.R;
		const float WaterDepth = Val.G;
		const FVector2D WaterVelocity(Val.B, Val.A);
		
		FVector4 FloatVal;
		FloatVal.X = WaterHeight;
		FloatVal.Y = WaterDepth;
		FloatVal.Z = WaterVelocity.X;
		FloatVal.W = WaterVelocity.Y;

		ShallowWaterSimArrayValues[Index++] = FloatVal;
	}

	BakedSim = NewObject<UBakedShallowWaterSimulationComponent>(this, NAME_None, RF_Public);
	BakedSim->SimulationData = FShallowWaterSimulationGrid(ShallowWaterSimArrayValues, BakedWaterSurfaceTexture, FIntVector2(BakedWaterSurfaceRT->SizeX, BakedWaterSurfaceRT->SizeY), SystemPos, WorldGridSize);
	
	// set the sim texture on each water body that is in the simulated river.  
	for (TObjectPtr<AWaterBody > CurrWaterBody : AllWaterBodies)
	{
		TObjectPtr<UWaterBodyComponent> CurrWaterBodyComponent = CurrWaterBody->GetWaterBodyComponent();
				
		CurrWaterBodyComponent->SetBakedShallowWaterSimulation(BakedSim);
	}
}

bool UShallowWaterRiverComponent::QueryWaterAtSplinePoint(TObjectPtr<AWaterBody> WaterBody, int SplinePoint, FVector& OutPos, FVector& OutTangent, float& OutWidth, float& OutDepth)
{	
	if (WaterBody != nullptr)
	{
		TObjectPtr<UWaterBodyComponent> CurrWaterBodyComponent = WaterBody->GetWaterBodyComponent();

		UWaterSplineComponent* CurrSpline = WaterBody->GetWaterSpline();
		
		if (CurrSpline != nullptr)
		{
			// -1 means last spline point
			if (SplinePoint == -1)
			{
				SplinePoint = CurrSpline->GetNumberOfSplinePoints() - 1;
			}

			UWaterSplineMetadata* Metadata = WaterBody->GetWaterSplineMetadata();

			if (Metadata != nullptr)
			{
				OutPos = CurrSpline->SplineCurves.Position.Points[SplinePoint].OutVal;
				OutPos = WaterBody->GetActorTransform().TransformPosition(OutPos);

				OutWidth = Metadata->RiverWidth.Points[SplinePoint].OutVal;
				OutDepth = Metadata->Depth.Points[SplinePoint].OutVal;

				OutTangent = CurrSpline->SplineCurves.Position.Points[SplinePoint].LeaveTangent;

				OutTangent = WaterBody->GetActorTransform().TransformVector(OutTangent);

				OutTangent.Normalize();
			}
			else
			{
				UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::QueryWaterAtSplinePoint() - Water spline metadata is null"));
				return false;
			}
		}
		else
		{
			UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::QueryWaterAtSplinePoint() - Water spline component is null"));
			return false;
		}
	}
	else
	{
		UE_LOG(LogShallowWater, Warning, TEXT("UShallowWaterRiverComponent::QueryWaterAtSplinePoint() - Water actor is null"));
		return false;
	}

	return true;
}

void UShallowWaterRiverComponent::OnWaterInfoTextureArrayCreated(const UTextureRenderTarget2DArray* InWaterInfoTexture)
{
	if (InWaterInfoTexture == nullptr)
	{
		ensureMsgf(false, TEXT("UShallowWaterRiverComponent::OnWaterInfoTextureCreated was called with NULL WaterInfoTexture"));
		return;
	}

	WaterInfoTexture = InWaterInfoTexture;
	if (RiverSimSystem)
	{
		UTexture* WITTextureArray = Cast<UTexture>(const_cast<UTextureRenderTarget2DArray*>(WaterInfoTexture.Get()));
		if (WITTextureArray == nullptr)
		{
			ensureMsgf(false, TEXT("UShallowWaterRiverComponent::OnWaterInfoTextureCreated was called with Water Info Texture that isn't valid"));
			return;
		}

		RiverSimSystem->SetVariableTexture(FName("WaterInfoTexture"), WITTextureArray);
	}
	else
	{
		ensureMsgf(false, TEXT("UShallowWaterRiverComponent::OnWaterInfoTextureCreated was called with NULL ShallowWaterNiagaraSimulation"));
		return;
	}
}

#endif

void UShallowWaterRiverComponent::UpdateRenderState()
{
	bool RenderWaterBody = RenderState == EShallowWaterRenderState::WaterComponent || RenderState == EShallowWaterRenderState::WaterComponentWithBakedSim;

	RiverSimSystem->SetVisibility(!RenderWaterBody);

	for (AWaterBody* CurrWaterBody : AllWaterBodies)
	{
		TObjectPtr<UWaterBodyComponent> CurrWaterBodyComponent = CurrWaterBody->GetWaterBodyComponent();

		if (CurrWaterBodyComponent != nullptr)
		{
			CurrWaterBodyComponent->SetVisibility(RenderWaterBody);

			UMaterialInstanceDynamic* WaterMID = CurrWaterBodyComponent->GetWaterMaterialInstance();
			UMaterialInstanceDynamic* WaterInfoMID = CurrWaterBodyComponent->GetWaterInfoMaterialInstance();
			if (RenderState == EShallowWaterRenderState::WaterComponentWithBakedSim)
			{				
				// override materials on water bodies							
				WaterMID->SetTextureParameterValue("BakedWaterSimTex", BakedWaterSurfaceTexture);
				WaterMID->SetVectorParameterValue("BakedWaterSimLocation", SystemPos);
				WaterMID->SetVectorParameterValue("BakedWaterSimSize", FVector(WorldGridSize.X, WorldGridSize.Y, 1));

				WaterInfoMID->SetTextureParameterValue("BakedWaterSimTex", BakedWaterSurfaceTexture);
				WaterInfoMID->SetVectorParameterValue("BakedWaterSimLocation", SystemPos);
				WaterInfoMID->SetVectorParameterValue("BakedWaterSimSize", FVector(WorldGridSize.X, WorldGridSize.Y, 1));
			}

			CurrWaterBodyComponent->SetUseBakedSimulationForQueriesAndPhysics(
				RenderState == EShallowWaterRenderState::WaterComponentWithBakedSim || RenderState == EShallowWaterRenderState::BakedSim);

			/*
			#todo(dmp): I'd prefer if we could set an editor time only static switch to control using baked sims in the material or not
			TArray<FMaterialParameterInfo> OutMaterialParameterInfos;
			TArray<FGuid> Guids;
			WaterMID->GetAllStaticSwitchParameterInfo(OutMaterialParameterInfos, Guids);

			for (FMaterialParameterInfo& MaterialParameterInfo : OutMaterialParameterInfos)
			{
				if (MaterialParameterInfo.Name == "UseBakedSim")
				{
					WaterMID->SetStaticSwitchParameterValueEditorOnly(MaterialParameterInfo, RenderState == EShallowWaterRenderState::WaterComponentWithBakedSim);
				}
			}
			*/

			WaterMID->SetScalarParameterValue("UseBakedSimHack", RenderState == EShallowWaterRenderState::WaterComponentWithBakedSim ? 1 : 0);
		}
	}
}

AShallowWaterRiver::AShallowWaterRiver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ShallowWaterRiverComponent = CreateDefaultSubobject<UShallowWaterRiverComponent>(TEXT("ShallowWaterRiverComponent"));
	RootComponent = ShallowWaterRiverComponent;

	PrimaryActorTick.bCanEverTick = true;
	SetHidden(false);
}

