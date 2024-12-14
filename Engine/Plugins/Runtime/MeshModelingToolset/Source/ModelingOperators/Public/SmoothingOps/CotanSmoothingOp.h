// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmoothingOpBase.h"
#include "CoreMinimal.h"

namespace UE
{
namespace Geometry
{

class MODELINGOPERATORS_API  FCotanSmoothingOp : public FSmoothingOpBase
{
public:
	FCotanSmoothingOp(const FDynamicMesh3* Mesh, const FSmoothingOpBase::FOptions& OptionsIn);
	
	// Support for smoothing only selected geometry
	FCotanSmoothingOp(const FDynamicMesh3* Mesh, const FSmoothingOpBase::FOptions& OptionsIn, const FDynamicSubmesh3& Submesh);

	~FCotanSmoothingOp() override {};

	void CalculateResult(FProgressCancel* Progress) override;

private:
	// Compute the smoothed result by using Cotan Biharmonic
	void Smooth(FProgressCancel* Progress);

	double GetSmoothPower(int32 VertexID, bool bIsBoundary);
};

} // end namespace UE::Geometry
} // end namespace UE
