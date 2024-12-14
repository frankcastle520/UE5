// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyElements.h"

class URigHierarchy;

class CONTROLRIG_API FRigHierarchyPoseAdapter : public TSharedFromThis<FRigHierarchyPoseAdapter>
{
public:

	virtual ~FRigHierarchyPoseAdapter()
	{
	}

protected:

	URigHierarchy* GetHierarchy() const;

	virtual void PostLinked(URigHierarchy* InHierarchy);
	virtual void PreUnlinked(URigHierarchy* InHierarchy);
	virtual void PostUnlinked(URigHierarchy* InHierarchy);

	TTuple<FRigComputedTransform*, FRigTransformDirtyState*> GetElementTransformStorage(const FRigElementKeyAndIndex& InKeyAndIndex, ERigTransformType::Type InTransformType, ERigTransformStorageType::Type InStorageType) const;

	bool RelinkTransformStorage(const FRigElementKeyAndIndex& InKeyAndIndex, ERigTransformType::Type InTransformType, ERigTransformStorageType::Type InStorageType, FTransform* InTransformStorage, bool* InDirtyFlagStorage);
	bool RestoreTransformStorage(const FRigElementKeyAndIndex& InKeyAndIndex, ERigTransformType::Type InTransformType, ERigTransformStorageType::Type InStorageType, bool bUpdateElementStorage);
	bool RelinkTransformStorage(const TArrayView<TTuple<FRigElementKeyAndIndex,ERigTransformType::Type,ERigTransformStorageType::Type,FTransform*,bool*>>& InData);
	bool RestoreTransformStorage(const TArrayView<TTuple<FRigElementKeyAndIndex,ERigTransformType::Type,ERigTransformStorageType::Type>>& InData, bool bUpdateElementStorage);

	bool RelinkCurveStorage(const FRigElementKeyAndIndex& InKeyAndIndex, float* InCurveStorage);
	bool RestoreCurveStorage(const FRigElementKeyAndIndex& InKeyAndIndex, bool bUpdateElementStorage);
	bool RelinkCurveStorage(const TArrayView<TTuple<FRigElementKeyAndIndex,float*>>& InData);
	bool RestoreCurveStorage(const TArrayView<FRigElementKeyAndIndex>& InData, bool bUpdateElementStorage);

	bool SortHierarchyStorage();
	bool ShrinkHierarchyStorage();
	bool UpdateHierarchyStorage();

	TWeakObjectPtr<URigHierarchy> WeakHierarchy;

	friend class URigHierarchy;
	friend class URigHierarchyController;
	friend class FControlRigHierarchyPoseAdapter;
};
