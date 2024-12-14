// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Curves/RichCurve.h"
#include "LensDistortionSceneViewExtension.h"
#include "LensFileRendering.h"
#include "Math/Vector2D.h"
#include "Misc/EnumClassFlags.h"
#include "Tables/DistortionParametersTable.h"
#include "Tables/LensTableUtils.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"


struct FRichCurve;

namespace LensInterpolationUtils
{
	template<typename Type>
	Type BlendValue(float InBlendWeight, const Type& A, const Type& B)
	{
		return FMath::Lerp(A, B, InBlendWeight);
	}
	
	void Interpolate(const UStruct* InStruct, float InBlendWeight, const void* InFrameDataA, const void* InFrameDataB, void* OutFrameData);

	template<typename Type>
	void Interpolate(float InBlendWeight, const Type* InFrameDataA, const Type* InFrameDataB, Type* OutFrameData)
	{
		Interpolate(Type::StaticStruct(), InBlendWeight, InFrameDataA, InFrameDataB, OutFrameData);
	}
	
	float GetBlendFactor(float InValue, float ValueA, float ValueB);

	/** A cubic bezier curve constructed from two points and their tangents */
	struct FTangentBezierCurve
	{
		FTangentBezierCurve(float InX0, float InX1, float InY0, float InY1, float InTangent0, float InTangent1)
			: X0(InX0)
			, X1(InX1)
			, Y0(InY0)
			, Y1(InY1)
			, Tangent0(InTangent0)
			, Tangent1(InTangent1)

		{}

		/** Evaluates the Bezier curve at the specified x value */
		float Eval(float InX);

		/** Minimum x value of the curve */
		float X0;

		/** Maximum x value of the curve */
		float X1;

		/** Y value of the curve at x0 */
		float Y0;

		/** Y value of the curve at x1 */
		float Y1;

		/** Tangent of the curve at x0 */
		float Tangent0;

		/** Tangent of the curve at x1 */
		float Tangent1;
	};
	
	/**
	 * A Coons patch (https://en.wikipedia.org/wiki/Coons_patch) for blending four boundary curves together
	 * into a surface patch. Takes in four rich curves for each side, two for x-axis and two for y-axis, as well as
	 * min and max values for x and y from which the corner points of the patch are formed.
	 */
	struct FCoonsPatch
	{
		FCoonsPatch(const FRichCurve& X0Curve, const FRichCurve& X1Curve, const FRichCurve& Y0Curve, const FRichCurve& Y1Curve, float X0, float X1, float Y0, float Y1)
			: X0Curve(X0Curve)
			, X1Curve(X1Curve)
			, Y0Curve(Y0Curve)
			, Y1Curve(Y1Curve)
			, X0(X0)
			, X1(X1)
			, Y0(Y0)
			, Y1(Y1)
		{ }
		
		/** Computes the value of the patch at the specified point by blending between the four edge curves */
		float Blend(const FVector2D& InPoint);
		
		/** Min x-axis curve of the patch */
		const FRichCurve& X0Curve;

		/** Max x-axis curve of the patch */
		const FRichCurve& X1Curve;

		/** Min y-axis curve of the patch */
		const FRichCurve& Y0Curve;

		/** Max y-axis curve of the patch */
		const FRichCurve& Y1Curve;

		/** Minimum x value of the patch */
		float X0;

		/** Maximum x value of the patch */
		float X1;

		/** Minimum y value of the patch */
		float Y0;

		/** Maximum y value of the patch */
		float Y1;
	};

	/**
	 * A version of the Coons patch that takes in four points that each have x and y tangent data, and performs
	 * a Bezier interpolation to create the four edge curves from the points and their tangents
	 */
	struct FTangentBezierCoonsPatch
	{
		/** Creates a new Coons patch from four corner points with x and y tangents. */
		FTangentBezierCoonsPatch(float X0, float X1, float Y0, float Y1, float XTangents[4], float YTangents[4]);
		
		/** Computes the value of the patch at the specified point by blending between the four edge curves */
		float Blend(const FVector2D& InPoint);

		/** Stores the x and y position and x and y tangents of a corner */
		struct FPatchCorner
		{
			float X;
			float Y;
			float TangentX;
			float TangentY;
			float Value;

			FPatchCorner(float InX, float InY, float InTangentX, float InTangentY, float InValue)
				: X(InX), Y(InY), TangentX(InTangentX), TangentY(InTangentY), Value(InValue)
			{}
		};

		/**
		 * The four corners of the patch, each of which contains and x and y coordinate, and x and y tangent, and a value.
		 * Indexed in the following order: (X0, Y0) -> (X1, Y0) -> (X1, Y1) -> (X0, Y1)
		 */
		FPatchCorner Corners[4];
	};
	
	/** Performs a Coons patch blend on an indexed list of parameters where each parameter has its own set of curves, and outputs all blended parameters */
	template<typename FocusPointType, typename FocusCurveType>
	bool IndexedParameterBlend(const TArray<FocusPointType>& FocusPoints, const TArray<FocusCurveType>& FocusCurves, float InFocus, float InZoom, int32 NumParameters, TArray<float>& OutBlendedParameters)
	{
		if (FocusPoints.Num() <= 0)
		{
			return false;
		}
		
		const LensDataTableUtils::FPointNeighbors PointNeighbors = LensDataTableUtils::FindFocusPoints(InFocus, TConstArrayView<FocusPointType>(FocusPoints));
		const LensDataTableUtils::FPointNeighbors CurveNeighbors = LensDataTableUtils::FindFocusCurves(InZoom, TConstArrayView<FocusCurveType>(FocusCurves));
		if (PointNeighbors.IsSinglePoint())
		{
			// We are on a zoom curve, or exactly on a corner. Either way, the value can be evaluated directly from the zoom curve
			for (int32 Index = 0; Index < NumParameters; ++Index)
			{
				const FRichCurve* Curve = FocusPoints[PointNeighbors.PreviousIndex].GetCurveForParameter(Index);
				if (!Curve)
				{
					return false;
				}
				
				OutBlendedParameters.Add(Curve->Eval(InZoom));
			}

			return true;
		}
		
		if (CurveNeighbors.IsSinglePoint())
		{
			// We are on one of the focus curves, so return the value evaluated on the focus curves at the specified focus
			for (int32 Index = 0; Index < NumParameters; ++Index)
			{
				const FRichCurve* Curve = FocusCurves[CurveNeighbors.PreviousIndex].GetCurveForParameter(Index);
				if (!Curve)
				{
					return false;
				}
				
				OutBlendedParameters.Add(Curve->Eval(InFocus));
			}

			return true;
		}

		const float X0 = FocusCurves[CurveNeighbors.PreviousIndex].Zoom;
		const float X1 = FocusCurves[CurveNeighbors.NextIndex].Zoom;
		const float Y0 = FocusPoints[PointNeighbors.PreviousIndex].Focus;
		const float Y1 = FocusPoints[PointNeighbors.NextIndex].Focus;
		
		for (int32 Index = 0; Index < NumParameters; ++Index)
		{
			const FRichCurve* X0Curve = FocusPoints[PointNeighbors.PreviousIndex].GetCurveForParameter(Index);
			const FRichCurve* X1Curve = FocusPoints[PointNeighbors.NextIndex].GetCurveForParameter(Index);
			const FRichCurve* Y0Curve = FocusCurves[CurveNeighbors.PreviousIndex].GetCurveForParameter(Index);
			const FRichCurve* Y1Curve = FocusCurves[CurveNeighbors.NextIndex].GetCurveForParameter(Index);

			if (!X0Curve || !X1Curve || !Y0Curve || !Y1Curve)
			{
				return false;
			}
			
			FCoonsPatch CoonsPatch(*X0Curve, *X1Curve, *Y0Curve, *Y1Curve, X0, X1, Y0, Y1);
			OutBlendedParameters.Add(CoonsPatch.Blend(FVector2D(InZoom, InFocus)));
		}
		
		return true;
	}

	/**
	 * Parameters for a distortion map blend that configure what values are computed for the blend and how to retrieve the necessary
	 * blending values from each point being blended
	 */
	template<typename TableType>
	struct FDistortionMapBlendParams
	{
		using FocusPointType = typename TableType::FocusPointType;
		using FocusCurveType = typename TableType::FocusCurveType;

		DECLARE_DELEGATE_FourParams(FGetDisplacementMaps,
			const FocusPointType& /* FocusPoint */,
			const FocusCurveType& /* FocusCurve */,
			UTextureRenderTarget2D*& /* OutUndistortedMap */,
			UTextureRenderTarget2D*& /* OutDistortedMap */);
		
		DECLARE_DELEGATE_RetVal_TwoParams(TOptional<FDistortionInfo>, FGetDistortionParameters,
			const FocusPointType& /* FocusPoint */,
			const FocusCurveType& /* FocusCurve */);
		
		DECLARE_DELEGATE_RetVal_FourParams(float, FProcessDisplacementMaps,
			const FocusPointType& /* FocusPoint */,
			const FocusCurveType& /* FocusCurve */,
			UTextureRenderTarget2D* /* InUndistortedMap */,
			UTextureRenderTarget2D* /* InDistortedMap */);

		DECLARE_DELEGATE_ThreeParams(FGetDistortionState,
			const FocusPointType& /* FocusPoint */,
			const FocusCurveType& /* FocusCurve */,
			FLensDistortionState& /* OutState */);

		/**
		 * Callback to use to retrieve the distortion parameters for a specified point being blended.
		 * If unbound, no distortion parameters will be computed in the blending results
		 */
		FGetDistortionParameters GetDistortionParameters;

		/**
		 * Callback to retrieve the displacement map render targets to use for a specified point being blended.
		 * If unbound, use the provided displacement maps in the parameters
		 */
		FGetDisplacementMaps GetDisplacementMaps;
		
		/**
		 * Callback to use to perform any processing on the distortion maps for blending that returns the computed overscan for the maps.
		 * If unbound, no overscan blending will be computed in the blending results
		 */
		FProcessDisplacementMaps ProcessDisplacementMaps;

		/**
		 * Callback to use to retrieve the complete distortion state for a specified point being blended.
		 * If unbound, no distortion state will be added to the blending results
		 */
		FGetDistortionState GetDistortionState;

		/** Indicates that shader blending parameters should be calculated */
		bool bGenerateBlendingParams = false;
		
		/** The number of distortion parameters to blend */
		int32 DistortionParamNum = 0;

		/** When supplied, list of render targets to write the undistorted displacement maps to */
		TArray<UTextureRenderTarget2D*> UndistortedMaps;

		/** When supplied, list of render targets to write the distorted displacement maps to */
		TArray<UTextureRenderTarget2D*> DistortedMaps;
		
	};

	/** Blended results from a distortion map blend. Optionals are set when the blend input parameters indicated those blended values should be computed */
	struct FDistortionMapBlendResults
	{
		/** Indicates that a distortion map blend successfully occurred */
		bool bValid = false;

		/** The shader blending parameters, if they were computed for the blend */
		TOptional<FDisplacementMapBlendingParams> BlendingParams = TOptional<FDisplacementMapBlendingParams>();

		/** The blended distortion parameters, if they were computed for the blend */
		TOptional<FDistortionInfo> BlendedDistortionParams = TOptional<FDistortionInfo>();

		/** The undistorted maps for the blending, if they generated */
		TOptional<TArray<UTextureRenderTarget2D*>> UndistortedMaps = TOptional<TArray<UTextureRenderTarget2D*>>();

		/** The distorted maps for the blending, if they generated */
		TOptional<TArray<UTextureRenderTarget2D*>> DistortedMaps = TOptional<TArray<UTextureRenderTarget2D*>>();
		
		/** The blended overscan, if it was computed for the blend */
		TOptional<float> BlendedOverscan = TOptional<float>();
	};

	/** Performs a Coons patch blend on the distortion map parameters for the specified table at the specified focus and zoom */
	template<typename TableType>
	FDistortionMapBlendResults DistortionMapBlend(const TableType& Table, float InFocus, float InZoom, const FDistortionMapBlendParams<TableType>& InParams)
	{
		FDistortionMapBlendResults Results;
		
		using FocusPointType = typename TableType::FocusPointType;
		using FocusCurveType = typename TableType::FocusCurveType;

		// Function to get the correct tangents of the curves safely, even in edge cases where there isn't a rich curve key defined for the specified point
		auto SafeGetTangents = [](const FRichCurve& Curve, const LensDataTableUtils::FPointNeighbors& Points, float& OutPrevTangent, float& OutNextTangent)
		{
			bool bHasPrevious = Curve.Keys.IsValidIndex(Points.PreviousIndex);
			bool bHasNext = Curve.Keys.IsValidIndex(Points.NextIndex);
		
			if (bHasPrevious && bHasNext)
			{
				const FRichCurveKey& Prev = Curve.Keys[Points.PreviousIndex];
				const FRichCurveKey& Next = Curve.Keys[Points.NextIndex];
				if (Prev.InterpMode == ERichCurveInterpMode::RCIM_Cubic)
				{
					OutPrevTangent = Prev.LeaveTangent;
					OutNextTangent = Next.ArriveTangent;
				}
				else
				{
					float Slope = (Next.Value - Prev.Value) / (Next.Time - Prev.Time);
					OutPrevTangent = Slope;
					OutNextTangent = Slope;
				}
			}
			else
			{
				// If one or both points don't have a key in the curve, the curve between those two points
				// will necessarily be flat, and as such both tangents are 0
				OutPrevTangent = 0;
				OutNextTangent = 0;
			}
		};
		
		TConstArrayView<FocusPointType> FocusPoints = Table.GetFocusPoints();
		TConstArrayView<FocusCurveType> FocusCurves = Table.GetFocusCurves();
		
		if (FocusPoints.Num() <= 0)
		{
			return Results;
		}

		Results.bValid = true;

		if (InParams.bGenerateBlendingParams)
		{
			Results.BlendingParams = FDisplacementMapBlendingParams();
			Results.BlendingParams->EvalFocus = InFocus;
			Results.BlendingParams->EvalZoom = InZoom;
		}

		if (InParams.ProcessDisplacementMaps.IsBound())
		{
			// If the callback to retrieve the displacement maps for each blended point is set, use that as a source for the displacement maps
			// Otherwise, if any displacement maps were passed in the parameters, use them
			if (InParams.GetDisplacementMaps.IsBound())
			{
				Results.UndistortedMaps = TArray<UTextureRenderTarget2D*>();
				Results.DistortedMaps = TArray<UTextureRenderTarget2D*>();

				Results.UndistortedMaps->AddZeroed(4);
				Results.DistortedMaps->AddZeroed(4);
			}
			else if (InParams.UndistortedMaps.Num() == 4 && InParams.DistortedMaps.Num() == 4)
			{
				Results.UndistortedMaps = InParams.UndistortedMaps;
				Results.DistortedMaps = InParams.DistortedMaps;
			}
		}
		
		if (InParams.GetDistortionParameters.IsBound())
		{
			Results.BlendedDistortionParams = FDistortionInfo();
			Results.BlendedDistortionParams->Parameters.AddDefaulted(InParams.DistortionParamNum);
		}
		
		const LensDataTableUtils::FPointNeighbors PointNeighbors = LensDataTableUtils::FindFocusPoints(InFocus, FocusPoints);
		const LensDataTableUtils::FPointNeighbors CurveNeighbors = LensDataTableUtils::FindFocusCurves(InZoom, FocusCurves);
			
		const FocusPointType& PrevFocusPoint = FocusPoints[PointNeighbors.PreviousIndex];
		const FocusPointType& NextFocusPoint = FocusPoints[PointNeighbors.NextIndex];
		const FocusCurveType& PrevFocusCurve = FocusCurves[CurveNeighbors.PreviousIndex];
		const FocusCurveType& NextFocusCurve = FocusCurves[CurveNeighbors.NextIndex];
		
		if (PointNeighbors.IsSinglePoint() && CurveNeighbors.IsSinglePoint())
		{
			// We are on a corner of the blending patch
			// Simply retrieve the value at that point
			if (Results.BlendingParams.IsSet())
			{
				Results.BlendingParams->BlendType = EDisplacementMapBlendType::OneFocusOneZoom;
				InParams.GetDistortionState.ExecuteIfBound(PrevFocusPoint, PrevFocusCurve, Results.BlendingParams->States[0]);
			}

			if (Results.BlendedDistortionParams.IsSet())
			{
				TOptional<FDistortionInfo> Parameters = InParams.GetDistortionParameters.Execute(PrevFocusPoint, PrevFocusCurve);
				if (Parameters.IsSet())
				{
					Results.BlendedDistortionParams = Parameters.GetValue();
				}
			}

			if (Results.UndistortedMaps.IsSet())
			{
				InParams.GetDisplacementMaps.ExecuteIfBound(PrevFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[0], (*Results.DistortedMaps)[0]);
				Results.BlendedOverscan = InParams.ProcessDisplacementMaps.Execute(PrevFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[0], (*Results.DistortedMaps)[0]);
			}
		}
		else if (PointNeighbors.IsSinglePoint())
		{
			// We are on a zoom curve edge of the blending patch
			float PrevTangent, NextTangent;
			SafeGetTangents(PrevFocusPoint.MapBlendingCurve, CurveNeighbors, PrevTangent, NextTangent);
			
			FTangentBezierCurve BlendCurve(PrevFocusCurve.Zoom, NextFocusCurve.Zoom, 0.0f, 0.0f, PrevTangent, NextTangent);
			
			if (Results.BlendingParams.IsSet())
			{
				Results.BlendingParams->BlendType = EDisplacementMapBlendType::OneFocusTwoZoom;
				Results.BlendingParams->PatchCorners[0] = FDisplacementMapBlendPatchCorner(PrevFocusCurve.Zoom, PrevFocusPoint.Focus, PrevTangent, 0.0);
				Results.BlendingParams->PatchCorners[1] = FDisplacementMapBlendPatchCorner(NextFocusCurve.Zoom, PrevFocusPoint.Focus, NextTangent, 0.0);

				InParams.GetDistortionState.ExecuteIfBound(PrevFocusPoint, PrevFocusCurve, Results.BlendingParams->States[0]);
				InParams.GetDistortionState.ExecuteIfBound(PrevFocusPoint, NextFocusCurve, Results.BlendingParams->States[1]);
			}

			if (Results.BlendedDistortionParams.IsSet())
			{
				// Blend all distortion parameters using the map blend curve
				TOptional<FDistortionInfo> PrevParameters = InParams.GetDistortionParameters.Execute(PrevFocusPoint, PrevFocusCurve);
				TOptional<FDistortionInfo> NextParameters = InParams.GetDistortionParameters.Execute(PrevFocusPoint, NextFocusCurve);
				
				const int32 NumParams = Results.BlendedDistortionParams->Parameters.Num();
				for (int Index = 0; Index < NumParams; ++Index)
				{
					BlendCurve.Y0 = PrevParameters.IsSet() ? PrevParameters.GetValue().Parameters[Index] : 0.0;
					BlendCurve.Y1 = NextParameters.IsSet() ? NextParameters.GetValue().Parameters[Index] : 0.0;

					Results.BlendedDistortionParams->Parameters[Index] = BlendCurve.Eval(InZoom);
				}
			}
			
			if (Results.UndistortedMaps.IsSet())
			{
				InParams.GetDisplacementMaps.ExecuteIfBound(PrevFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[0], (*Results.DistortedMaps)[0]);
				InParams.GetDisplacementMaps.ExecuteIfBound(PrevFocusPoint, NextFocusCurve, (*Results.UndistortedMaps)[1], (*Results.DistortedMaps)[1]);
				
				// Blend the overscan factor using the map blend curve
				BlendCurve.Y0 = InParams.ProcessDisplacementMaps.Execute(PrevFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[0], (*Results.DistortedMaps)[0]);
				BlendCurve.Y1 = InParams.ProcessDisplacementMaps.Execute(PrevFocusPoint, NextFocusCurve, (*Results.UndistortedMaps)[1], (*Results.DistortedMaps)[1]);
				
				Results.BlendedOverscan = BlendCurve.Eval(InZoom);
			}
		}
		else if (CurveNeighbors.IsSinglePoint())
		{
			// We are on a focus curve edge of the blending patch
			float PrevTangent, NextTangent;
			SafeGetTangents(PrevFocusCurve.MapBlendingCurve, PointNeighbors, PrevTangent, NextTangent);
			
			FTangentBezierCurve BlendCurve(PrevFocusPoint.Focus, NextFocusPoint.Focus, 0.0f, 0.0f, PrevTangent, NextTangent);
			
			if (Results.BlendingParams.IsSet())
			{
				Results.BlendingParams->BlendType = EDisplacementMapBlendType::TwoFocusOneZoom;
				Results.BlendingParams->PatchCorners[0] = FDisplacementMapBlendPatchCorner(PrevFocusCurve.Zoom, PrevFocusPoint.Focus, 0.0, PrevTangent);
				Results.BlendingParams->PatchCorners[1] = FDisplacementMapBlendPatchCorner(PrevFocusCurve.Zoom, NextFocusPoint.Focus, 0.0, NextTangent);

				InParams.GetDistortionState.ExecuteIfBound(PrevFocusPoint, PrevFocusCurve, Results.BlendingParams->States[0]);
				InParams.GetDistortionState.ExecuteIfBound(NextFocusPoint, PrevFocusCurve, Results.BlendingParams->States[1]);
			}
			
			if (Results.BlendedDistortionParams.IsSet())
			{
				// Blend all distortion parameters using the map blend curve
				TOptional<FDistortionInfo> PrevParameters = InParams.GetDistortionParameters.Execute(PrevFocusPoint, PrevFocusCurve);
				TOptional<FDistortionInfo> NextParameters = InParams.GetDistortionParameters.Execute(NextFocusPoint, PrevFocusCurve);
				
				const int32 NumParams = Results.BlendedDistortionParams->Parameters.Num();
				for (int Index = 0; Index < NumParams; ++Index)
				{
					BlendCurve.Y0 = PrevParameters.IsSet() ? PrevParameters.GetValue().Parameters[Index] : 0.0;
					BlendCurve.Y1 = NextParameters.IsSet() ? NextParameters.GetValue().Parameters[Index] : 0.0;

					Results.BlendedDistortionParams->Parameters[Index] = BlendCurve.Eval(InFocus);
				}
			}
			
			if (Results.UndistortedMaps.IsSet())
			{
				InParams.GetDisplacementMaps.ExecuteIfBound(PrevFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[0], (*Results.DistortedMaps)[0]);
				InParams.GetDisplacementMaps.ExecuteIfBound(NextFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[1], (*Results.DistortedMaps)[1]);
				
				// Blend the overscan factor using the map blend curve
				BlendCurve.Y0 = InParams.ProcessDisplacementMaps.Execute(PrevFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[0], (*Results.DistortedMaps)[0]);
				BlendCurve.Y1 = InParams.ProcessDisplacementMaps.Execute(NextFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[1], (*Results.DistortedMaps)[1]);
				
				Results.BlendedOverscan = BlendCurve.Eval(InFocus);
			}
		}
		else
		{
			// Otherwise, we are somewhere in the middle of the patch, and must do a full Coons patch blend
			float XTangents[4];
			SafeGetTangents(PrevFocusPoint.MapBlendingCurve, CurveNeighbors, XTangents[0], XTangents[1]);
			SafeGetTangents(NextFocusPoint.MapBlendingCurve, CurveNeighbors, XTangents[2], XTangents[3]);
		
			float YTangents[4];
			SafeGetTangents(PrevFocusCurve.MapBlendingCurve, PointNeighbors, YTangents[0], YTangents[3]);
			SafeGetTangents(NextFocusCurve.MapBlendingCurve, PointNeighbors, YTangents[1], YTangents[2]);
			
			FTangentBezierCoonsPatch CoonsPatch(PrevFocusCurve.Zoom, NextFocusCurve.Zoom, PrevFocusPoint.Focus, NextFocusPoint.Focus, XTangents, YTangents);

			if (Results.BlendingParams.IsSet())
			{
				Results.BlendingParams->BlendType = EDisplacementMapBlendType::TwoFocusTwoZoom;
				Results.BlendingParams->PatchCorners[0] = FDisplacementMapBlendPatchCorner(PrevFocusCurve.Zoom, PrevFocusPoint.Focus, XTangents[0], YTangents[0]);
				Results.BlendingParams->PatchCorners[1] = FDisplacementMapBlendPatchCorner(NextFocusCurve.Zoom, PrevFocusPoint.Focus, XTangents[1], YTangents[1]);
				Results.BlendingParams->PatchCorners[2] = FDisplacementMapBlendPatchCorner(NextFocusCurve.Zoom, NextFocusPoint.Focus, XTangents[2], YTangents[2]);
				Results.BlendingParams->PatchCorners[3] = FDisplacementMapBlendPatchCorner(PrevFocusCurve.Zoom, NextFocusPoint.Focus, XTangents[3], YTangents[3]);

				InParams.GetDistortionState.ExecuteIfBound(PrevFocusPoint, PrevFocusCurve, Results.BlendingParams->States[0]);
				InParams.GetDistortionState.ExecuteIfBound(PrevFocusPoint, NextFocusCurve, Results.BlendingParams->States[1]);
				InParams.GetDistortionState.ExecuteIfBound(NextFocusPoint, NextFocusCurve, Results.BlendingParams->States[2]);
				InParams.GetDistortionState.ExecuteIfBound(NextFocusPoint, PrevFocusCurve, Results.BlendingParams->States[3]);
			}
			
			if (Results.BlendedDistortionParams.IsSet())
			{
				TOptional<FDistortionInfo> Points[4] =
				{
					InParams.GetDistortionParameters.Execute(PrevFocusPoint, PrevFocusCurve),
					InParams.GetDistortionParameters.Execute(PrevFocusPoint, NextFocusCurve),
					InParams.GetDistortionParameters.Execute(NextFocusPoint, NextFocusCurve),
					InParams.GetDistortionParameters.Execute(NextFocusPoint, PrevFocusCurve)
				};

				// Handle edge case for when a corner point of the patch is not defined. If such a case happens, we
				// average its value from its two neighbor points, which are guaranteed to exist
				for (int32 Index = 0; Index < 4; ++Index)
				{
					if (!Points[Index].IsSet())
					{
						int32 PrevIndex = Index > 0 ? Index - 1 : 3;
						int32 NextIndex = Index < 3 ? Index + 1 : 0;

						Points[Index] = FDistortionInfo();
						
						int32 NumParams = Results.BlendedDistortionParams->Parameters.Num();
						for (int ParamIndex = 0; ParamIndex < NumParams; ++ParamIndex)
						{
							Points[Index]->Parameters.Add(0.5 * (Points[PrevIndex]->Parameters[ParamIndex] + Points[NextIndex]->Parameters[ParamIndex]));
						}
					}
				}
				
				const int32 NumParams = Results.BlendedDistortionParams->Parameters.Num();
				for (int Index = 0; Index < NumParams; ++Index)
				{
					CoonsPatch.Corners[0].Value = Points[0]->Parameters[Index];
					CoonsPatch.Corners[1].Value = Points[1]->Parameters[Index];
					CoonsPatch.Corners[2].Value = Points[2]->Parameters[Index];
					CoonsPatch.Corners[3].Value = Points[3]->Parameters[Index];
				
					Results.BlendedDistortionParams->Parameters[Index] = CoonsPatch.Blend(FVector2D(InZoom, InFocus));
				}
			}
			
			if (Results.UndistortedMaps.IsSet())
			{
				InParams.GetDisplacementMaps.ExecuteIfBound(PrevFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[0], (*Results.DistortedMaps)[0]);
				InParams.GetDisplacementMaps.ExecuteIfBound(PrevFocusPoint, NextFocusCurve, (*Results.UndistortedMaps)[1], (*Results.DistortedMaps)[1]);
				InParams.GetDisplacementMaps.ExecuteIfBound(NextFocusPoint, NextFocusCurve, (*Results.UndistortedMaps)[2], (*Results.DistortedMaps)[2]);
				InParams.GetDisplacementMaps.ExecuteIfBound(NextFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[3], (*Results.DistortedMaps)[3]);
				
				CoonsPatch.Corners[0].Value = InParams.ProcessDisplacementMaps.Execute(PrevFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[0], (*Results.DistortedMaps)[0]);
				CoonsPatch.Corners[1].Value = InParams.ProcessDisplacementMaps.Execute(PrevFocusPoint, NextFocusCurve, (*Results.UndistortedMaps)[1], (*Results.DistortedMaps)[1]);
				CoonsPatch.Corners[2].Value = InParams.ProcessDisplacementMaps.Execute(NextFocusPoint, NextFocusCurve, (*Results.UndistortedMaps)[2], (*Results.DistortedMaps)[2]);
				CoonsPatch.Corners[3].Value = InParams.ProcessDisplacementMaps.Execute(NextFocusPoint, PrevFocusCurve, (*Results.UndistortedMaps)[3], (*Results.DistortedMaps)[3]);

				Results.BlendedOverscan = CoonsPatch.Blend(FVector2D(InZoom, InFocus));
			}
		}

		return Results;
	}
};
