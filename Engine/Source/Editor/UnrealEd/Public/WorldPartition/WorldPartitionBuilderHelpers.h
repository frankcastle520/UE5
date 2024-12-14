// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SourceControlHelpers.h"
#include "PackageSourceControlHelper.h" 
#include "WorldPartition/WorldPartition.h" // for ISourceControlHelper

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionBuilderSourceControlHelper, All, All);

struct FBuilderModifiedFiles
{
	enum EFileOperation
	{
		FileAdded,
		FileEdited,
		FileDeleted,
		NumFileOperations
	};

	void Add(EFileOperation FileOp, const FString& File);
	const TSet<FString>& Get(EFileOperation FileOp) const;
	void Append(EFileOperation FileOp, const TArray<FString>& InFiles);
	void Append(const FBuilderModifiedFiles& Other);
	void Empty();
	TArray<FString> GetAllFiles() const;

private:
	TSet<FString> Files[NumFileOperations];
};

class FSourceControlHelper : public ISourceControlHelper
{
public:
	FSourceControlHelper(FPackageSourceControlHelper& InPackageHelper, FBuilderModifiedFiles& InModifiedFiles);

	virtual ~FSourceControlHelper();
	virtual FString GetFilename(const FString& PackageName) const override;
	virtual FString GetFilename(UPackage* Package) const override;
	virtual bool Checkout(UPackage* Package) const override;
	virtual bool Add(UPackage* Package) const override;
	virtual bool Delete(const FString& PackageName) const override;
	virtual bool Delete(UPackage* Package) const override;
	virtual bool Save(UPackage* Package) const override;

private:
	FPackageSourceControlHelper& PackageHelper;
	FBuilderModifiedFiles& ModifiedFiles;
};
