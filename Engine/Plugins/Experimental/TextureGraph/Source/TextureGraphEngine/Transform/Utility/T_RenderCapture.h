// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Transform/BlobTransform.h"
#include "Job/Job.h"
#include "Model/Mix/MixUpdateCycle.h"

class TEXTUREGRAPHENGINE_API T_BeginRenderCapture : public BlobTransform
{
public:
	T_BeginRenderCapture();
	virtual							~T_BeginRenderCapture() override;

	virtual Device* TargetDevice(size_t index) const override;
	virtual AsyncTransformResultPtr	Exec(const TransformArgs& args) override;

	virtual bool					GeneratesData() const override { return false; }

	////////////////////////////////////////////////////////////////////////// 
	/// Static functions
	//////////////////////////////////////////////////////////////////////////
	static JobUPtr					CreateJob(MixUpdateCyclePtr cycle, int32 targetId);
};


class TEXTUREGRAPHENGINE_API T_EndRenderCapture : public BlobTransform
{
public:
	T_EndRenderCapture();
	virtual							~T_EndRenderCapture() override;

	virtual Device* TargetDevice(size_t index) const override;
	virtual AsyncTransformResultPtr	Exec(const TransformArgs& args) override;

	virtual bool					GeneratesData() const override { return false; }

	////////////////////////////////////////////////////////////////////////// 
	/// Static functions
	//////////////////////////////////////////////////////////////////////////
	static JobUPtr					CreateJob(MixUpdateCyclePtr cycle, int32 targetId);
};