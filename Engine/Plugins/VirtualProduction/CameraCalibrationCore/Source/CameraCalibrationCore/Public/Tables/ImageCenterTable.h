// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Curves/RichCurve.h"
#include "LensData.h"
#include "Tables/BaseLensTable.h"

#include "ImageCenterTable.generated.h"

/**
 * ImageCenter focus point containing curves for CxCy 
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FImageCenterFocusPoint : public FBaseFocusPoint
{
	GENERATED_BODY()

	using PointType = FImageCenterInfo;
	
public:
	//~ Begin FBaseFocusPoint Interface
	virtual float GetFocus() const override { return Focus; }
	virtual int32 GetNumPoints() const override;
	virtual float GetZoom(int32 Index) const override;
	//~ End FBaseFocusPoint Interface

	/** Returns data type copy value for a given float */
	bool GetPoint(float InZoom, FImageCenterInfo& OutData, float InputTolerance = KINDA_SMALL_NUMBER) const;

	/** Adds a new point at InZoom. Updates existing one if tolerance is met */
	bool AddPoint(float InZoom, const FImageCenterInfo& InData, float InputTolerance, bool bIsCalibrationPoint);

	/** Sets an existing point at InZoom. Updates existing one if tolerance is met */
	bool SetPoint(float InZoom, const FImageCenterInfo& InData, float InputTolerance = KINDA_SMALL_NUMBER);
	
	/** Gets whether the point at InZoom is a calibration point. */
	bool IsCalibrationPoint(float InZoom, float InputTolerance = KINDA_SMALL_NUMBER) { return false; }
	
	/** Removes a point corresponding to specified zoom */
	void RemovePoint(float InZoomValue);

	/** Returns true if this point is empty */
	bool IsEmpty() const;

	/** Gets the curve for the specified parameter, or nullptr if the parameter index is invalid */
	const FRichCurve* GetCurveForParameter(int32 InParameterIndex) const;

	/** Gets the curve for the specified parameter, or nullptr if the parameter index is invalid */
	FRichCurve* GetCurveForParameter(int32 InParameterIndex);
	
public:

	/** Focus value of this point */
	UPROPERTY()
	float Focus = 0.0f;

	/** Curves representing normalized Cx over zoom */
	UPROPERTY()
	FRichCurve Cx;

	/** Curves representing normalized Cy over zoom */
	UPROPERTY()
	FRichCurve Cy;
};

/** A curve along the focus axis for a single zoom value */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FImageCenterFocusCurve : public FBaseFocusCurve
{
	GENERATED_BODY()

public:
	/** Adds a new point to the focus curve, or updates a matching existing point if one is found */
	void AddPoint(float InFocus, const FImageCenterInfo& InData, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Updates an existing point if one is found */
	void SetPoint(float InFocus, const FImageCenterInfo& InData, float InputTolerance = KINDA_SMALL_NUMBER);

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
	/** Focus curve for the x parameter of the image center */
	UPROPERTY()
	FRichCurve Cx;

	/** Focus curve for the y parameter of the image center */
	UPROPERTY()
	FRichCurve Cy;

	/** The fixed zoom value of the curve */
	UPROPERTY()
	float Zoom = 0.0f;
};

/**
 * Image Center table associating CxCy values to focus and zoom
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FImageCenterTable : public FBaseLensTable
{
	GENERATED_BODY()

	using FocusPointType = FImageCenterFocusPoint;
	using FocusCurveType = FImageCenterFocusCurve;
	
	/** Wrapper for indices of specific parameters for the image center table  */
	struct FParameters
	{
		static constexpr int32 Cx = 0;
		static constexpr int32 Cy = 1;

		/** Returns if a parameter index is valid */
		static bool IsValid(int32 InParameterIndex) { return InParameterIndex >= 0 && InParameterIndex < 2; }
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
	virtual bool BuildParameterCurveAtFocus(float InFocus, int32 ParameterIndex, FRichCurve& OutCurve) const override;
	virtual bool BuildParameterCurveAtZoom(float InZoom, int32 ParameterIndex, FRichCurve& OutCurve) const override;
	virtual void SetParameterCurveKeysAtFocus(float InFocus, int32 InParameterIndex, const FRichCurve& InSourceCurve, TArrayView<const FKeyHandle> InKeys) override;
	virtual void SetParameterCurveKeysAtZoom(float InZoom, int32 InParameterIndex, const FRichCurve& InSourceCurve, TArrayView<const FKeyHandle> InKeys) override;
	virtual bool CanEditCurveKeyPositions(int32 InParameterIndex) const override { return true; }
	virtual bool CanEditCurveKeyAttributes(int32 InParameterIndex) const override { return true; }
	virtual FText GetParameterValueLabel(int32 InParameterIndex) const override;
	//~ End FBaseDataTable Interface
	
	/** Returns const point for a given focus */
	const FImageCenterFocusPoint* GetFocusPoint(float InFocus, float InputTolerance = KINDA_SMALL_NUMBER) const;

	/** Returns const point for a given focus */
	FImageCenterFocusPoint* GetFocusPoint(float InFocus, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Gets the focus curve for the specified zoom, or nullptr if none were found */
	const FImageCenterFocusCurve* GetFocusCurve(float InZoom, float InputTolerance = KINDA_SMALL_NUMBER) const;

	/** Gets the focus curve for the specified zoom, or nullptr if none were found */
	FImageCenterFocusCurve* GetFocusCurve(float InZoom, float InputTolerance = KINDA_SMALL_NUMBER);
	
	/** Returns all focus points */
	TConstArrayView<FImageCenterFocusPoint> GetFocusPoints() const;

	/** Returns all focus points */
	TArray<FImageCenterFocusPoint>& GetFocusPoints();

	/** Returns all focus curves */
	TConstArrayView<FImageCenterFocusCurve> GetFocusCurves() const;

	/** Returns all focus curves */
	TArray<FImageCenterFocusCurve>& GetFocusCurves();
	
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
	bool AddPoint(float InFocus, float InZoom, const FImageCenterInfo& InData, float InputTolerance, bool bIsCalibrationPoint);

	/** Get the point from the table */
	bool GetPoint(const float InFocus, const float InZoom, FImageCenterInfo& OutData, float InputTolerance = KINDA_SMALL_NUMBER) const;

	/** Set a new point into the table */
	bool SetPoint(float InFocus, float InZoom, const FImageCenterInfo& InData, float InputTolerance = KINDA_SMALL_NUMBER);

	/** Builds the focus curves to match existing data in the table */
	void BuildFocusCurves();
	
public:		

	/** Lists of focus points */
	UPROPERTY()
	TArray<FImageCenterFocusPoint> FocusPoints;

	/** A list of curves along the focus axis for each zoom value */
	UPROPERTY()
	TArray<FImageCenterFocusCurve> FocusCurves;
};

