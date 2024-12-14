// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/Task.h"
#include "Containers/ContainersFwd.h"
#include "UObject/CoreRedirects.h"

/**
 * Delegate called on when patch operation completes
 * @param	SrcFilePath		Path of file being read for patching
 * @param	DstFilePath		Path of file being written to after patching
 */
DECLARE_DELEGATE_TwoParams(FAssetHeaderPatcherCompletionDelegate, const FString& /*SrcFilePath*/, const FString& /*DstFilePath*/)

UE_INTERNAL struct ASSETTOOLS_API FAssetHeaderPatcher
{
	enum class EResult
	{
		NotStarted,
		Cancelled,
		InProgress,
		Success,
		ErrorFailedToLoadSourceAsset,
		ErrorFailedToDeserializeSourceAsset,
		ErrorUnexpectedSectionOrder,
		ErrorBadOffset,
		ErrorUnkownSection,
		ErrorFailedToOpenDestinationFile,
		ErrorFailedToWriteToDestinationFile,
		ErrorEmptyRequireSection,
	};

	UE_INTERNAL struct ASSETTOOLS_API FContext
	{
		FContext() = default;

		/**
		 * Context used for patching. Contains all information for how object and package references
		 * will be changed as part of patching. 
		 * *
		 * When bInGatherDependentPackages is true, the provided long package name (/Root/Folder/Package) to 
		 * destination long package name mapping will be used to find any dependent packages that must also 
		 * be patched due to internal references. The mapping provided in InSrcAndDstPackagePaths will be used 
		 * to determine the filepath on disk to write when patching.
		 *
		 * @param InSrcAndDstPackagePaths Map of all long package names (/Root/Folder/Package) to be patched and to which new name they should be patched to.
		 * @param bInGatherDependentPackages If true (default), upon creating the context GatherDependentPackages() will be called.
		 **/
		FContext(const TMap<FString, FString>& InSrcAndDstPackagePaths, const bool bInGatherDependentPackages = true);

		/**
		 * Context used for patching. Contains all information for how object and package references
		 * will be changed as part of patching. 
		 * 
		 * When patching, package paths to patch will be deduced by the filepath mappings provided in InSrcAndDstFilePaths. All assets
		 * under InSrcRoot will be written as package paths under a mountpoint located at InSrcBaseDir.
		 * 
		 * e.g. Path "C:/User/Repo/Project/Content/Skeletons/Player.uasset" -> "/InSrcRoot/Skeletons/Player" when InSrcBaseDir=C:/User/Repo/Project (/Content is assumed internally)
		 * 
		 * @param InSrcRoot The root mount point for assets to be patched
		 * @param InDstRoot The new root mount point for patched assets to be placed under
		 * @param InSrcBaseDir Path to the directory holding the /Content/ directory for assets to patch
		 * @param InSrcAndDstFilePaths Map of filepaths for files to be patched and where to write the patched version to
		 * @param InMountPointReplacements Map of root mountpoints (name only, no "/" prefix or suffix) to replace when patching
		 **/
		FContext(const FString& InSrcRoot, const FString& InDstRoot, const FString& InSrcBaseDir, const TMap<FString, FString>& InSrcAndDstFilePaths, const TMap<FString, FString>& InMountPointReplacements);

		/*
		* Returns the mapping of source long package names to destination package paths used when patching.
		* This mapping may include more packages than initially supplied to the FContext
		* if GatherDependentPackages has already been called. 
		* Note, this map can be invalidated by calls to GatherDependentPackages()
		*/
		const TMap<FString, FString>& GetLongPackagePathRemapping() const
		{
			return PackagePathRenameMap;
		};

	protected:
		friend FAssetHeaderPatcher;

		void AddVerseMounts();
		void GatherDependentPackages();
		void GenerateFilePathsFromPackagePaths();
		void GeneratePackagePathsFromFilePaths(const FString& InSrcRoot, const FString& InDstRoot, const FString& InSrcBaseDir);
		void GenerateAdditionalRemappings();
	
		TArray<FString> VerseMountPoints;
		TMap<FString, FString> PackagePathRenameMap;
		TMap<FString, FString> FilePathRenameMap;

		// Todo: Make TSet once FCoreRedirect GetTypeHash is implemented
		TArray<FCoreRedirect> Redirects;

		// String mappings are only used for best-effort replacements. These will be error-prone 
		// and we should strive for more structured data formats to guard against errors here
		TMap<FString, FString> StringReplacements;	
		TMap<FString, FString> StringMountReplacements;
	};

	FAssetHeaderPatcher() = default;
	FAssetHeaderPatcher(FContext InContext) : Context(InContext) { Reset(); }
	
	/*
	* Resets the patcher state and sets a new patching context.
	* It is an error to call while patching is already in progress.
	*/
	void SetContext(FContext InContext);

	/*
	* Schedules the reading of source files determined by the patcher context, as well as the writing of the patched versions
	* of all source files read. 
	* 
	* @param InOutNumFilesToPatch Optional value to know how many files are expected to be read/written during patching
	* @param InOutNumFilesPatched Optional value used to know how the patcher is progressing (useful for progress bars)
	*/
	UE::Tasks::FTask PatchAsync(int32* InOutNumFilesToPatch = nullptr, int32* InOutNumFilesPatched = nullptr);
	UE::Tasks::FTask PatchAsync(int32* InOutNumFilesToPatch, int32* InOutNumFilesPatched, FAssetHeaderPatcherCompletionDelegate InOnSuccess, FAssetHeaderPatcherCompletionDelegate InOnError);

	/*
	* Returns the status of any inflight patching operations. In the case of multiple errors, the last seen error will be reported.
	* Per file error status codes can be returned with GetErrorFiles().
	*/
	EResult GetPatchResult() const
	{
		return Status;
	}

	/*
	* Returns source file -> destination mapping for all files that were patched successfully.
	*/
	TMap<FString, FString> GetPatchedFiles() const
	{
		if (IsPatching())
		{
			return TMap<FString, FString>();
		}

		return PatchedFiles;
	}

	/*
	* Returns true if the patcher encountered errors (even if patching was cancelled)
	*/
	bool HasErrors()
	{
		FScopeLock Lock(&ErroredFilesLock);
		return !!ErroredFiles.Num();
	}

	/*
	* Returns a map of all files that had an error during patching with an error code to provide context as to the cause of the error.
	*/
	TMap<FString, EResult> GetErrorFiles()
	{
		FScopeLock Lock(&ErroredFilesLock);
		return ErroredFiles;
	}

	/*
	* Returns true if the patcher is still in theprocess of patching.
	*/
	bool IsPatching() const 
	{ 
		return !PatchingTask.IsCompleted(); 
	}

	/*
	* Cancels an in-flight patching operation. Patching work on individual files that has already started will run to completion
	* however any files that have not started patching will be skipped. Even after cancelling, one must wait for the patcher to complete
	* by waiting on the GetPatchingTask() explciitly or until IsPatching returns false.
	* 
	* @return true if an in-flight patching operation was cancelled. If no patching operation is underway, returns false.
	*/
	bool CancelPatching() 
	{
		if (!IsPatching())
		{
			return false;
		}

		bCancelled = true;
		Status = EResult::Cancelled;

		return true;
	}

	/*
	* Returns the task for all patcher work underway. Waiting on this task will guarantee all patch work is completed.
	*/
	UE::Tasks::FTask GetPatchingTask() const
	{
		return PatchingTask;
	}

	/**
	 * Patches object and package references contained within InSrcAsset using the mapping provided to InContext. The 
	 * patched asset will be written to InDstAsset. 
	 *
	 * @param InSrcAsset Long package name (/Root/Folder/Package) to read in to be patched.
	 * @param InDstAsset Long package name (/Root/Folder/Package) where the patched package will be written to.
	 * @param InContext Context for how the patching will be performed. Contains all remapping information to the patcher.
	 * @return Success patching was successful and the InDstAsset package was written. Returns an error status otherwise.
	 **/
	static EResult DoPatch(const FString& InSrcAsset, const FString& InDstAsset, const FContext& InContext);

private:
	void Reset();

	FContext Context;

	TMap<FString, EResult> ErroredFiles;
	FCriticalSection  ErroredFilesLock;

	TMap<FString, FString> PatchedFiles;

	UE::Tasks::FTask PatchingTask;
	EResult Status = EResult::NotStarted;
	std::atomic<bool> bCancelled = false;
};
UE_INTERNAL ASSETTOOLS_API FString LexToString(FAssetHeaderPatcher::EResult InResult);
