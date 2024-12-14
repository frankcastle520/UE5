// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Curves/RichCurve.h"
#include "LensData.h"
#include "Tables/BaseLensTable.h"

#include "NodalOffsetTable.generated.h"


/**
 * Focus point for nodal offset curves
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FNodalOffsetFocusPoint : public FBaseFocusPoint
{
	GENERATED_BODY()

	using PointType = FNodalPointOffset;
	
public:
	//~ Begin FBaseFocusPoint Interface
	virtual float GetFocus() const override { return Focus; }
	virtual int32 GetNumPoints() const override;
	virtual float GetZoom(int32 Index) const override;
	//~ End FBaseFocusPoint Interface

	/** Returns data type copy value for a given float */
	bool GetPoint(float InZoom, FNodalPointOffset& OutData, float InputTolerance = KINDA_SMALL_NUMBER) const;

	/** Adds a new point at InZoom. Updates existing one if tolerance is met */
	bool AddPoint(float InZoom, const FNodalPointOffset& InData, float InputTolerance, bool bIsCalibrationPoint);

	/** Sets an existing point at InZoom. Updates existing one if tolerance is met */
	bool SetPoint(float InZoom, const FNodalPointOffset& InData, float InputTolerance = KINDA_SMALL_NUMBER);
	
	/** Gets whether the point at InZoom is a calibration point. */
	bool IsCalibrationPoint(float InZoom, float InputTolerance = KINDA_SMALL_NUMBER) { return false; }
	
	/** Removes a point corresponding to specified zoom */
	void RemovePoint(float InZoomValue);

	/** Returns true if there are no points */
	bool IsEmpty() const;

	/** Gets the curve for the specified parameter, or nullptr if the parameter index is invalid */
	const FRichCurve* GetCurveForParameter(int32 InParameterIndex) const;

	/** Gets the curve for the specified parameter, or nullptr if the parameter index is invalid */
	FRichCurve* GetCurveForParameter(int32 InParameterIndex);
	
public:

	/** Dimensions of our location offset curves */
	static constexpr uint32 LocationDimension = 3;
	
	/** Dimensions of our rotation offset curves */
	static constexpr uint32 RotationDimension = 3;

	/** Input focus for this point */
	UPROPERTY()
	float Focus = 0.0f;

	/** XYZ offsets curves mapped to zoom */
	UPROPERTY()
	FRichCurve LocationOffset[LocationDimension];

	/** Yaw, Pitch and Roll offset curves mapped to zoom */
	UPROPERTY()
	FRichCurve RotationOffset[RotationDimension];
};

/** A curve along the focus axis for a single zoom value */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FNodalOffsetFocusCurve : public FBaseFocusCurve
{
	GENERATED_BODY()

public:
	/** Adds a new point to the focus curve, or updates a matching existing point if one is found */
	void AddPoint(float InFocus, const FNodalPointOffset& InData, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Updates an existing point if one is found */
	void SetPoint(float InFocus, const FNodalPointOffset& InData, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Removes the point at the specified focus if one is found */
	void RemovePoint(float InFocus, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Changes the focus value of the point at the specified focus, if one is found */
	void ChangeFocus(float InExistingFocus, float InNewFocus, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Changes the focus value of the point at the specified focus and optionally replaces any point at the new focus with the old point */
	void MergeFocus(float InExistingFocus, float InNewFocus, bool bReplaceExisting, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Gets whether the curve is empty */
	bool IsEmpty() const;

	/** Gets the curve for the specified parameter, or nullptr if the parameter index is invalid */
	const FRichCurve* GetCurveForParameter(int32 InParameterIndex) const;

	/** Gets the curve for the specified parameter, or nullptr if the parameter index is invalid */
	FRichCurve* GetCurveForParameter(int32 InParameterIndex);
	
public:
	/** Focus curve for the location parameters of the nodal offset */
	UPROPERTY()
	FRichCurve LocationOffset[FNodalOffsetFocusPoint::LocationDimension];

	/** Focus curve for the rotation parameters of the nodal offset */
	UPROPERTY()
	FRichCurve RotationOffset[FNodalOffsetFocusPoint::RotationDimension];
	
	/** The fixed zoom value of the curve */
	UPROPERTY()
	float Zoom = 0.0f;
};

/**
 * Table containing nodal offset mapping to focus and zoom
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FNodalOffsetTable : public FBaseLensTable
{
	GENERATED_BODY()

	using FocusPointType = FNodalOffsetFocusPoint;
	using FocusCurveType = FNodalOffsetFocusCurve;

	/** Wrapper for indices of specific parameters for the nodal offset table  */
	struct FParameters
	{
		static constexpr int32 Location = 0;
		static constexpr int32 Rotation = 1;

		/** Composes the parameter and axis indices into a single value */
		static int32 Compose(int32 InParameterIndex, EAxis::Type Axis) { return InParameterIndex * 3 + (Axis - 1); }

		/** Composes a combined index into a parameter index and an axis */
		static void Decompose(int32 InComposedIndex, int32& OutParameterIndex, EAxis::Type& OutAxis)
		{
			OutParameterIndex = InComposedIndex / 3;
			OutAxis = (EAxis::Type)(InComposedIndex % 3 + 1);
		}
		
		/** Returns if a composed parameter index is valid */
		static bool IsValidComposed(int32 InComposedIndex) { return InComposedIndex >= 0 && InComposedIndex < 6; }
	};
	
protected:
	//~ Begin FBaseDataTable Interface
	virtual TMap<ELensDataCategory, FLinkPointMetadata> GetLinkedCategories() const override;
	virtual bool DoesFocusPointExists(float InFocus, float InputTolerance = KINDA_SMALL_NUMBER) const override;
	virtual bool DoesZoomPointExists(float InFocus, float InZoom, float InputTolerance = KINDA_SMALL_NUMBER) const override;
	virtual const FBaseFocusPoint* GetBaseFocusPoint(int32 InIndex) const override;
	//~ End FBaseDataTable Interface
	
public:
	//~ Begin FBaseDataTable Interface
	virtual void ForEachPoint(FFocusPointCallback InCallback) const override;
	virtual int32 GetFocusPointNum() const override { return FocusPoints.Num(); }
	virtual int32 GetTotalPointNum() const override;
	virtual UScriptStruct* GetScriptStruct() const override;
	virtual bool BuildParameterCurveAtFocus(float InFocus, int32 InParameterIndex, FRichCurve& OutCurve) const override;
	virtual bool BuildParameterCurveAtZoom(float InZoom, int32 InParameterIndex, FRichCurve& OutCurve) const override;
	virtual void SetParameterCurveKeysAtFocus(float InFocus, int32 InParameterIndex, const FRichCurve& InSourceCurve, TArrayView<const FKeyHandle> InKeys) override;
	virtual void SetParameterCurveKeysAtZoom(float InZoom, int32 InParameterIndex, const FRichCurve& InSourceCurve, TArrayView<const FKeyHandle> InKeys) override;
	virtual bool CanEditCurveKeyPositions(int32 InParameterIndex) const override { return true; }
	virtual bool CanEditCurveKeyAttributes(int32 InParameterIndex) const override { return true; }
	virtual FText GetParameterValueLabel(int32 InParameterIndex) const override;
	virtual FText GetParameterValueUnitLabel(int32 InParameterIndex) const override;
	//~ End FBaseDataTable Interface

	/** Returns const point for a given focus */
	const FNodalOffsetFocusPoint* GetFocusPoint(float InFocus, float InputTolerance = KINDA_SMALL_NUMBER) const;

	/** Returns point for a given focus */
	FNodalOffsetFocusPoint* GetFocusPoint(float InFocus, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Gets the focus curve for the specified zoom, or nullptr if none were found */
	const FNodalOffsetFocusCurve* GetFocusCurve(float InZoom, float InputTolerance = KINDA_SMALL_NUMBER) const;

	/** Gets the focus curve for the specified zoom, or nullptr if none were found */
	FNodalOffsetFocusCurve* GetFocusCurve(float InZoom, float InputTolerance = KINDA_SMALL_NUMBER);
	
	/** Returns all focus points */
	TConstArrayView<FNodalOffsetFocusPoint> GetFocusPoints() const;

	/** Returns all focus points */
	TArray<FNodalOffsetFocusPoint>& GetFocusPoints();

	/** Returns all focus curves */
	TConstArrayView<FNodalOffsetFocusCurve> GetFocusCurves() const;

	/** Returns all focus curves */
	TArray<FNodalOffsetFocusCurve>& GetFocusCurves();
	
	/** Removes a focus point identified as InFocusIdentifier */
	void RemoveFocusPoint(float InFocus);

	/** Checks to see if there exists a focus point matching the specified focus value */
	bool HasFocusPoint(float InFocus, float InputTolerance = KINDA_SMALL_NUMBER) const;

	/** Changes the value of a focus point */
	void ChangeFocusPoint(float InExistingFocus, float InNewFocus, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Merges the points in the specified source focus into the specified destination focus */
	void MergeFocusPoint(float InSrcFocus, float InDestFocus, bool bReplaceExistingZoomPoints, float InputTolerance = KINDA_SMALL_NUMBER);
	
	/** Removes a zoom point from a focus point*/
	void RemoveZoomPoint(float InFocus, float InZoom);

	/** Checks to see if there exists a zoom point matching the specified zoom and focus values */
	bool HasZoomPoint(float InFocus, float InZoom, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Changes the value of a zoom point */
	void ChangeZoomPoint(float InFocus, float InExistingZoom, float InNewZoom, float InputTolerance = KINDA_SMALL_NUMBER);
	
	/** Adds a new point in the table */
	bool AddPoint(float InFocus, float InZoom, const FNodalPointOffset& InData,  float InputTolerance, bool bIsCalibrationPoint);

	/** Get the point from the table */
	bool GetPoint(const float InFocus, const float InZoom, FNodalPointOffset& OutData, float InputTolerance = KINDA_SMALL_NUMBER) const;

	/** Set a new point into the table */
	bool SetPoint(float InFocus, float InZoom, const FNodalPointOffset& InData, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Builds the focus curves to match existing data in the table */
	void BuildFocusCurves();
	
public:

	/** Lists of focus points */
	UPROPERTY()
	TArray<FNodalOffsetFocusPoint> FocusPoints;

	/** A list of curves along the focus axis for each zoom value */
	UPROPERTY()
	TArray<FNodalOffsetFocusCurve> FocusCurves;
};

