// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuildPatchFileConstructor.h"
#include "IBuildManifestSet.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "HAL/RunnableThread.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/OutputDeviceRedirector.h"
#include "BuildPatchServicesPrivate.h"
#include "Interfaces/IBuildInstaller.h"
#include "Data/ChunkData.h"
#include "Common/StatsCollector.h"
#include "Common/SpeedRecorder.h"
#include "Common/FileSystem.h"
#include "Installer/ChunkSource.h"
#include "Installer/ChunkDbChunkSource.h"
#include "Installer/ChunkReferenceTracker.h"
#include "Installer/InstallerError.h"
#include "Installer/InstallerAnalytics.h"
#include "Installer/InstallerSharedContext.h"
#include "BuildPatchUtil.h"

using namespace BuildPatchServices;

// This define the number of bytes on a half-finished file that we ignore from the end
// incase of previous partial write.
#define NUM_BYTES_RESUME_IGNORE     1024

static int32 SleepTimeWhenFileSystemThrottledSeconds = 15;
static FAutoConsoleVariableRef CVarSleepTimeWhenFileSystemThrottledSeconds(
	TEXT("BuildPatchFileConstructor.SleepTimeWhenFileSystemThrottledSeconds"),
	SleepTimeWhenFileSystemThrottledSeconds,
	TEXT("The amount of time to sleep if the destination filesystem is throttled."),
	ECVF_Default);

static bool bStallWhenFileSystemThrottled = false;
static FAutoConsoleVariableRef CVarStallWhenFileSystemThrottled(
	TEXT("BuildPatchFileConstructor.bStallWhenFileSystemThrottled"),
	bStallWhenFileSystemThrottled,
	TEXT("Whether to stall if the file system is throttled"),
	ECVF_Default);


// Helper functions wrapping common code.
namespace FileConstructorHelpers
{
	void WaitWhilePaused(FThreadSafeBool& bIsPaused, FThreadSafeBool& bShouldAbort)
	{
		// Wait while paused
		while (bIsPaused && !bShouldAbort)
		{
			FPlatformProcess::Sleep(0.5f);
		}
	}

	bool CheckRemainingDiskSpace(const FString& InstallDirectory, uint64 RemainingBytesRequired, uint64& OutAvailableDiskSpace)
	{
		bool bContinueConstruction = true;
		uint64 TotalSize = 0;
		OutAvailableDiskSpace = 0;
		if (FPlatformMisc::GetDiskTotalAndFreeSpace(InstallDirectory, TotalSize, OutAvailableDiskSpace))
		{
			if (OutAvailableDiskSpace < RemainingBytesRequired)
			{
				bContinueConstruction = false;
			}
		}
		else
		{
			// If we can't get the disk space free then the most likely reason is the drive is no longer around...
			bContinueConstruction = false;
		}

		return bContinueConstruction;
	}

	uint64 CalculateRequiredDiskSpace(const FBuildPatchAppManifestPtr& CurrentManifest, const FBuildPatchAppManifestRef& BuildManifest, const EInstallMode& InstallMode, const TSet<FString>& InInstallTags)
	{
		// Make tags expected
		TSet<FString> InstallTags = InInstallTags;
		if (InstallTags.Num() == 0)
		{
			BuildManifest->GetFileTagList(InstallTags);
		}
		InstallTags.Add(TEXT(""));
		// Calculate the files that need constructing.
		TSet<FString> TaggedFiles;
		BuildManifest->GetTaggedFileList(InstallTags, TaggedFiles);
		FString DummyString;
		TSet<FString> FilesToConstruct;
		BuildManifest->GetOutdatedFiles(CurrentManifest.Get(), DummyString, TaggedFiles, FilesToConstruct);
		// Count disk space needed by each operation.
		int64 DiskSpaceDeltaPeak = 0;
		if (InstallMode == EInstallMode::DestructiveInstall && CurrentManifest.IsValid())
		{
			// The simplest method will be to run through each high level file operation, tracking peak disk usage delta.
			int64 DiskSpaceDelta = 0;

			// Loop through all files to be made next, in order.
			FilesToConstruct.Sort(TLess<FString>());
			for (const FString& FileToConstruct : FilesToConstruct)
			{
				// First we would need to make the new file.
				DiskSpaceDelta += BuildManifest->GetFileSize(FileToConstruct);
				if (DiskSpaceDeltaPeak < DiskSpaceDelta)
				{
					DiskSpaceDeltaPeak = DiskSpaceDelta;
				}
				// Then we can remove the current existing file.
				DiskSpaceDelta -= CurrentManifest->GetFileSize(FileToConstruct);
			}
		}
		else
		{
			// When not destructive, or no CurrentManifest, we always stage all new and changed files.
			DiskSpaceDeltaPeak = BuildManifest->GetFileSize(FilesToConstruct);
		}
		return FMath::Max<int64>(DiskSpaceDeltaPeak, 0);
	}
}

enum class EConstructionError : uint8
{
	None = 0,
	CannotCreateFile,
	OutOfDiskSpace,
	MissingChunk,
	SerializeError,
	TrackingError,
	OutboundDataError
};

/**
 * This struct handles loading and saving of simple resume information, that will allow us to decide which
 * files should be resumed from. It will also check that we are creating the same version and app as we expect to be.
 */
struct FResumeData
{
public:
	// File system dependency
	const IFileSystem* const FileSystem;

	// The manifests for the app we are installing
	const IBuildManifestSet* const ManifestSet;

	// Save the staging directory
	const FString StagingDir;

	// The filename to the resume data information
	const FString ResumeDataFilename;

	// The resume ids that we loaded from disk
	TSet<FString> LoadedResumeIds;

	// The set of files that were started
	TSet<FString> FilesStarted;

	// The set of files that were completed, determined by expected file size
	TSet<FString> FilesCompleted;

	// The set of files that exist but are not able to assume resumable
	TSet<FString> FilesIncompatible;

	// Whether we have any resume data for this install
	bool bHasResumeData;

public:

	FResumeData(IFileSystem* InFileSystem, IBuildManifestSet* InManifestSet, const FString& InStagingDir, const FString& InResumeDataFilename)
		: FileSystem(InFileSystem)
		, ManifestSet(InManifestSet)
		, StagingDir(InStagingDir)
		, ResumeDataFilename(InResumeDataFilename)
		, bHasResumeData(false)
	{
		// Load data from previous resume file
		bHasResumeData = FileSystem->FileExists(*ResumeDataFilename);
		GLog->Logf(TEXT("BuildPatchResumeData file found: %s"), bHasResumeData ? TEXT("true") : TEXT("false"));
		if (bHasResumeData)
		{
			// Grab existing resume metadata.
			const bool bCullEmptyLines = true;
			FString PrevResumeData;
			TArray<FString> PrevResumeDataLines;
			FileSystem->LoadFileToString(*ResumeDataFilename, PrevResumeData);
			PrevResumeData.ParseIntoArrayLines(PrevResumeDataLines, bCullEmptyLines);
			// Grab current resume ids
			const bool bCheckLegacyIds = true;
			TSet<FString> NewResumeIds;
			ManifestSet->GetInstallResumeIds(NewResumeIds, bCheckLegacyIds);
			LoadedResumeIds.Reserve(PrevResumeDataLines.Num());
			// Check if any builds we are installing are a resume from previous run.
			for (FString& PrevResumeDataLine : PrevResumeDataLines)
			{
				PrevResumeDataLine.TrimStartAndEndInline();
				LoadedResumeIds.Add(PrevResumeDataLine);
				if (NewResumeIds.Contains(PrevResumeDataLine))
				{
					bHasResumeData = true;
					GLog->Logf(TEXT("BuildPatchResumeData version matched %s"), *PrevResumeDataLine);
				}
			}
		}
	}

	/**
	 * Saves out the resume data
	 */
	void SaveOut(const TSet<FString>& ResumeIds)
	{
		// Save out the patch versions
		FileSystem->SaveStringToFile(*ResumeDataFilename, FString::Join(ResumeIds, TEXT("\n")));
	}

	/**
	 * Checks whether the file was completed during last install attempt and adds it to FilesCompleted if so
	 * @param Filename    The filename to check
	 */
	void CheckFile(const FString& Filename)
	{
		// If we had resume data, check if this file might have been resumable
		if (bHasResumeData)
		{
			int64 DiskFileSize;
			const FString FullFilename = StagingDir / Filename;
			const bool bFileExists = FileSystem->GetFileSize(*FullFilename, DiskFileSize);
			const bool bCheckLegacyIds = true;
			TSet<FString> FileResumeIds;
			ManifestSet->GetInstallResumeIdsForFile(Filename, FileResumeIds, bCheckLegacyIds);
			if (LoadedResumeIds.Intersect(FileResumeIds).Num() > 0)
			{
				const FFileManifest* NewFileManifest = ManifestSet->GetNewFileManifest(Filename);
				if (NewFileManifest && bFileExists)
				{
					const uint64 UnsignedDiskFileSize = DiskFileSize;
					if (UnsignedDiskFileSize > 0 && UnsignedDiskFileSize <= NewFileManifest->FileSize)
					{
						FilesStarted.Add(Filename);
					}
					if (UnsignedDiskFileSize == NewFileManifest->FileSize)
					{
						FilesCompleted.Add(Filename);
					}
					if (UnsignedDiskFileSize > NewFileManifest->FileSize)
					{
						FilesIncompatible.Add(Filename);
					}
				}
			}
			else if (bFileExists)
			{
				FilesIncompatible.Add(Filename);
			}
		}
	}
};

/* FBuildPatchFileConstructor implementation
 *****************************************************************************/
FBuildPatchFileConstructor::FBuildPatchFileConstructor(
	FFileConstructorConfig InConfiguration, IFileSystem* InFileSystem, IChunkSource* InChunkSource, 
	IChunkDbChunkSource* InChunkDbChunkSource, IChunkReferenceTracker* InChunkReferenceTracker, IInstallerError* InInstallerError, 
	IInstallerAnalytics* InInstallerAnalytics, IFileConstructorStat* InFileConstructorStat)
	: Configuration(MoveTemp(InConfiguration))
	, bIsDownloadStarted(false)
	, bInitialDiskSizeCheck(false)
	, bIsPaused(false)
	, bShouldAbort(false)
	, ThreadLock()
	, ConstructionStack()
	, FileSystem(InFileSystem)
	, ChunkSource(InChunkSource)
	, ChunkDbSource(InChunkDbChunkSource)
	, ChunkReferenceTracker(InChunkReferenceTracker)
	, InstallerError(InInstallerError)
	, InstallerAnalytics(InInstallerAnalytics)
	, FileConstructorStat(InFileConstructorStat)
	, TotalJobSize(0)
	, ByteProcessed(0)
	, RequiredDiskSpace(0)
	, AvailableDiskSpace(0)
{
	// Count initial job size
	const int32 ConstructListNum = Configuration.ConstructList.Num();
	ConstructionStack.Reserve(ConstructListNum);
	ConstructionStack.AddDefaulted(ConstructListNum);

	// Track when we will complete files in the reference chain.
	int32 CurrentPosition = 0;
	FileCompletionPositions.Reserve(ConstructListNum);

	for (int32 ConstructListIdx = 0; ConstructListIdx < ConstructListNum ; ++ConstructListIdx)
	{
		const FString& ConstructListElem = Configuration.ConstructList[ConstructListIdx];
		const FFileManifest* FileManifest = Configuration.ManifestSet->GetNewFileManifest(ConstructListElem);
		if (FileManifest)
		{
			TotalJobSize += FileManifest->FileSize;
		
			// We will be advancing the chunk reference tracker by this many chunks.
			int32 AdvanceCount = FileManifest->ChunkParts.Num();
			CurrentPosition += AdvanceCount;

			FileCompletionPositions.Add(CurrentPosition);
		}

		ConstructionStack[(ConstructListNum - 1) - ConstructListIdx] = ConstructListElem;
	}

	WriteBuffers[0].Reserve(WriteBufferSize);
	WriteBuffers[1].Reserve(WriteBufferSize);

	WriteJobThread = Configuration.SharedContext->CreateThread();
	WriteJobCompleteEvent = FPlatformProcess::GetSynchEventFromPool();
	WriteJobStartEvent = FPlatformProcess::GetSynchEventFromPool();
	WriteJobThread->RunTask([this]() { WriteJobThreadRun(); });
}

FBuildPatchFileConstructor::~FBuildPatchFileConstructor()
{
	if (bWriteJobRunning)
	{
		GLog->Logf(TEXT("FBuildPatchFileConstructor: Write job active during destruction! Very bad."));
	}

	// Signal background thread to shut down.
	Abort();
	WriteJobStartEvent->Trigger();	
	WriteJobCompleteEvent->Wait();

	FPlatformProcess::ReturnSynchEventToPool(WriteJobCompleteEvent);
	WriteJobCompleteEvent = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(WriteJobStartEvent);
	WriteJobStartEvent = nullptr;

	Configuration.SharedContext->ReleaseThread(WriteJobThread);
}


void FBuildPatchFileConstructor::WriteJobThreadRun()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WriteJobThread);

	for (;;)
	{
		bool bSignalWasFired = WriteJobStartEvent->Wait(100 /* ms */);

		if (bSignalWasFired)
		{
			// (got signal) -- they launched a job - init to failed job
			bWriteJobCompleted = false;
		}

		if (bShouldAbort) // this is also used for graceful shutdown on completion.
		{
			// Leave WriteJobCompleted = false;
			WriteJobCompleteEvent->Trigger();
			return;
		}

		if (!bSignalWasFired)
		{
			// We hit the timeout checking for an abort signal, wait agian.
			continue;
		}

		FileConstructorStat->OnBeforeWrite();
		ISpeedRecorder::FRecord ActivityRecord;
		ActivityRecord.CyclesStart = FStatsCollector::GetCycles();
		
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WriteThread_Serialize)
			WriteJobArchive->Serialize(WriteJobBufferToWrite->GetData(), WriteJobBufferToWrite->Num());
		}

		ActivityRecord.Size = WriteJobBufferToWrite->Num();
		ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
		FileConstructorStat->OnAfterWrite(ActivityRecord);

		bWriteJobCompleted = true;
		WriteJobCompleteEvent->Trigger();
	}
}

void FBuildPatchFileConstructor::Run()
{
	FileConstructorStat->OnTotalRequiredUpdated(TotalJobSize);

	// Check for resume data, we need to also look for a legacy resume file to use instead in case we are resuming from an install of previous code version.
	const FString LegacyResumeDataFilename = Configuration.StagingDirectory / TEXT("$resumeData");
	const FString ResumeDataFilename = Configuration.MetaDirectory / TEXT("$resumeData");
	const bool bHasLegacyResumeData = FileSystem->FileExists(*LegacyResumeDataFilename);
	// If we find a legacy resume data file, lets move it first.
	if (bHasLegacyResumeData)
	{
		FileSystem->MoveFile(*ResumeDataFilename, *LegacyResumeDataFilename);
	}
	FResumeData ResumeData(FileSystem, Configuration.ManifestSet, Configuration.StagingDirectory, ResumeDataFilename);

	// Remove incompatible files
	if (ResumeData.bHasResumeData)
	{
		for (const FString& FileToConstruct : Configuration.ConstructList)
		{
			ResumeData.CheckFile(FileToConstruct);
			const bool bFileIncompatible = ResumeData.FilesIncompatible.Contains(FileToConstruct);
			if (bFileIncompatible)
			{
				GLog->Logf(TEXT("FBuildPatchFileConstructor: Deleting incompatible stage file %s"), *FileToConstruct);
				FileSystem->DeleteFile(*(Configuration.StagingDirectory / FileToConstruct));
			}
		}
	}

	// Save for started versions
	TSet<FString> ResumeIds;
	const bool bCheckLegacyIds = false;

	Configuration.ManifestSet->GetInstallResumeIds(ResumeIds, bCheckLegacyIds);
	ResumeData.SaveOut(ResumeIds);

	// Start resume progress at zero or one.
	FileConstructorStat->OnResumeStarted();

	// While we have files to construct, run.
	FString FileToConstruct;
	while (GetFileToConstruct(FileToConstruct) && !bShouldAbort)
	{
		// Get the file manifest.
		const FFileManifest* FileManifest = Configuration.ManifestSet->GetNewFileManifest(FileToConstruct);
		bool bFileSuccess = FileManifest != nullptr;
		if (bFileSuccess)
		{
			const int64 FileSize = FileManifest->FileSize;
			FileConstructorStat->OnFileStarted(FileToConstruct, FileSize);

			// Check resume status for this file.
			const bool bFilePreviouslyComplete = ResumeData.FilesCompleted.Contains(FileToConstruct);
			const bool bFilePreviouslyStarted = ResumeData.FilesStarted.Contains(FileToConstruct);

			// Construct or skip the file.
			if (bFilePreviouslyComplete)
			{
				bFileSuccess = true;
				CountBytesProcessed(FileSize);
				GLog->Logf(TEXT("FBuildPatchFileConstructor: Skipping completed file %s"), *FileToConstruct);
				// Go through each chunk part, and dereference it from the reference tracker.
				for (const FChunkPart& ChunkPart : FileManifest->ChunkParts)
				{
					bFileSuccess = ChunkReferenceTracker->PopReference(ChunkPart.Guid) && bFileSuccess;
				}
			}
			else
			{
				bFileSuccess = ConstructFileFromChunks(FileToConstruct, *FileManifest, bFilePreviouslyStarted);
			}
		}
		else
		{
			// Only report or log if the first error
			if (InstallerError->HasError() == false)
			{
				InstallerAnalytics->RecordConstructionError(FileToConstruct, INDEX_NONE, TEXT("Missing File Manifest"));
				UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Missing file manifest for %s"), *FileToConstruct);
			}
			// Always set
			InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::MissingFileInfo);
		}

		if (bFileSuccess)
		{
			// If we are destructive, remove the old file.
			if (Configuration.InstallMode == EInstallMode::DestructiveInstall)
			{
				const bool bRequireExists = false;
				const bool bEvenReadOnly = true;
				FString FileToDelete = Configuration.InstallDirectory / FileToConstruct;
				FPaths::NormalizeFilename(FileToDelete);
				FPaths::CollapseRelativeDirectories(FileToDelete);
				if (FileSystem->FileExists(*FileToDelete))
				{
					OnBeforeDeleteFile().Broadcast(FileToDelete);
					IFileManager::Get().Delete(*FileToDelete, bRequireExists, bEvenReadOnly);
				}
			}
		}
		else
		{
			// This will only record and log if a failure was not already registered.
			bShouldAbort = true;
			InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::UnknownFail);
			UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Failed to build %s "), *FileToConstruct);
		}
		FileConstructorStat->OnFileCompleted(FileToConstruct, bFileSuccess);

		// Wait while paused.
		FileConstructorHelpers::WaitWhilePaused(bIsPaused, bShouldAbort);
	}

	// Mark resume complete if we didn't have work to do.
	if (!bIsDownloadStarted)
	{
		FileConstructorStat->OnResumeCompleted();
	}
	FileConstructorStat->OnConstructionCompleted();
}

uint64 FBuildPatchFileConstructor::GetRequiredDiskSpace()
{
	return RequiredDiskSpace.load(std::memory_order_relaxed);
}

uint64 FBuildPatchFileConstructor::GetAvailableDiskSpace()
{
	return AvailableDiskSpace.load(std::memory_order_relaxed);
}

FBuildPatchFileConstructor::FOnBeforeDeleteFile& FBuildPatchFileConstructor::OnBeforeDeleteFile()
{
	return BeforeDeleteFileEvent;
}

void FBuildPatchFileConstructor::CountBytesProcessed( const int64& ByteCount )
{
	ByteProcessed += ByteCount;
	FileConstructorStat->OnProcessedDataUpdated(ByteProcessed);
}

bool FBuildPatchFileConstructor::GetFileToConstruct(FString& Filename)
{
	FScopeLock Lock(&ThreadLock);
	const bool bFileAvailable = ConstructionStack.Num() > 0;
	if (bFileAvailable)
	{
		Filename = ConstructionStack.Pop(EAllowShrinking::No);
	}
	return bFileAvailable;
}

int64 FBuildPatchFileConstructor::GetRemainingBytes()
{
	FScopeLock Lock(&ThreadLock);
	return Configuration.ManifestSet->GetTotalNewFileSize(ConstructionStack);
}

uint64 FBuildPatchFileConstructor::CalculateInProgressDiskSpaceRequired(const FFileManifest& InProgressFileManifest, uint64 InProgressFileAmountWritten)
{
	if (Configuration.InstallMode == EInstallMode::DestructiveInstall)
	{
		// The simplest method will be to run through each high level file operation, tracking peak disk usage delta.

		// We know we need enough space to finish writing this file
		uint64 RemainingThisFileSpace = InProgressFileManifest.FileSize - InProgressFileAmountWritten;
		
		int64 DiskSpaceDeltaPeak = RemainingThisFileSpace;
		int64 DiskSpaceDelta = RemainingThisFileSpace;

		// Then we move this file over.
		{
			const FFileManifest* OldFileManifest = Configuration.ManifestSet->GetCurrentFileManifest(InProgressFileManifest.Filename);
			if (OldFileManifest)
			{
				DiskSpaceDelta -= OldFileManifest->FileSize;
			}

			// We've already accounted for the new file above, so we could be pretty negative if we resumed the file
			// almost at the end and had an existing file we're deleting.
		}

		// Loop through all files to be made next, in order.
		for (int32 ConstructionStackIdx = ConstructionStack.Num() - 1; ConstructionStackIdx >= 0; --ConstructionStackIdx)
		{
			const FString& FileToConstruct = ConstructionStack[ConstructionStackIdx];
			const FFileManifest* NewFileManifest = Configuration.ManifestSet->GetNewFileManifest(FileToConstruct);
			const FFileManifest* OldFileManifest = Configuration.ManifestSet->GetCurrentFileManifest(FileToConstruct);
			// First we would need to make the new file.
			DiskSpaceDelta += NewFileManifest->FileSize;
			if (DiskSpaceDeltaPeak < DiskSpaceDelta)
			{
				DiskSpaceDeltaPeak = DiskSpaceDelta;
			}
			// Then we can remove the current existing file.
			if (OldFileManifest)
			{
				DiskSpaceDelta -= OldFileManifest->FileSize;
			}
		}
		return DiskSpaceDeltaPeak;
	}
	else
	{
		// When not destructive, we always stage all new and changed files.
		uint64 RemainingFilesSpace = Configuration.ManifestSet->GetTotalNewFileSize(ConstructionStack);
		uint64 RemainingThisFileSpace = InProgressFileManifest.FileSize - InProgressFileAmountWritten;
		return RemainingFilesSpace + RemainingThisFileSpace;
	}
}



uint64 FBuildPatchFileConstructor::CalculateDiskSpaceRequirementsWithDeleteDuringInstall(const TArray<FString>& InBackwardsFilesLeftToConstruct)
{
	if (ChunkDbSource == nullptr)
	{
		// invalid use.
		return 0;
	}

	// These are the sizes at after each file that we _started_ with. This is the size after retirement for the
	// file at those positions.
	TArray<uint64> ChunkDbSizesAtPosition;
	uint64 TotalChunkDbSize = ChunkDbSource->GetChunkDbSizesAtIndexes(FileCompletionPositions, ChunkDbSizesAtPosition);

	// Strip off the files we've completed.
	int32 CompletedFileCount = Configuration.ConstructList.Num() - InBackwardsFilesLeftToConstruct.Num();

	// Since we are called after the first file is popped (but before it's actually done), we have one less completed.
	CompletedFileCount--;

	uint64 MaxDiskSize = FBuildPatchUtils::CalculateDiskSpaceRequirementsWithDeleteDuringInstall(
		Configuration.ConstructList, CompletedFileCount, Configuration.ManifestSet, ChunkDbSizesAtPosition, TotalChunkDbSize);

	// Strip off the data we already have on disk.
	uint64 PostDlSize = 0;
	if (MaxDiskSize > TotalChunkDbSize)
	{
		PostDlSize = MaxDiskSize - TotalChunkDbSize;
	}

	return PostDlSize;
}

bool FBuildPatchFileConstructor::ConstructFileFromChunks(const FString& BuildFilename, const FFileManifest& FileManifest, bool bResumeExisting)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ConstructFileFromChunks);

	bool bSuccess = true;
	EConstructionError ConstructionError = EConstructionError::None;
	uint32 LastError = 0;
	FString NewFilename = Configuration.StagingDirectory / BuildFilename;

	// Calculate the hash as we write the data
	FSHA1 HashState;
	FSHAHash HashValue;

	if (!FileManifest.SymlinkTarget.IsEmpty())
	{
#if PLATFORM_MAC
		bSuccess = symlink(TCHAR_TO_UTF8(*FileManifest.SymlinkTarget), TCHAR_TO_UTF8(*NewFilename)) == 0;
#else
		const bool bSymlinkNotImplemented = false;
		check(bSymlinkNotImplemented);
		bSuccess = false;
#endif
		return bSuccess;
	}

	// Check for resuming of existing file
	int64 StartPosition = 0;
	int32 StartChunkPart = 0;
	if (bResumeExisting)
	{
		// We have to read in the existing file so that the hash check can still be done.
		TUniquePtr<FArchive> NewFileReader(IFileManager::Get().CreateFileReader(*NewFilename));
		if (NewFileReader.IsValid())
		{
			// Start with a sensible buffer size for reading. 4 MiB.
			const int32 ReadBufferSize = 4 * 1024 * 1024;
			// Read buffer
			TArray<uint8> ReadBuffer;
			ReadBuffer.Empty(ReadBufferSize);
			ReadBuffer.SetNumUninitialized(ReadBufferSize);

			// Reuse the entire file. Previously this truncated to size - 1kb but that's unlikely to catch our actual
			// issue because its less than a sector size and makes it so that a graceful resume requires potentially retired
			// chunks.
			StartPosition = NewFileReader->TotalSize();

			// We'll also find the correct chunkpart to start writing from
			int64 ByteCounter = 0;
			for (int32 ChunkPartIdx = StartChunkPart; ChunkPartIdx < FileManifest.ChunkParts.Num() && !bShouldAbort; ++ChunkPartIdx)
			{
				const FChunkPart& ChunkPart = FileManifest.ChunkParts[ChunkPartIdx];
				const int64 NextBytePosition = ByteCounter + ChunkPart.Size;
				if (NextBytePosition <= StartPosition)
				{
					// Ensure buffer is large enough
					ReadBuffer.SetNumUninitialized(ChunkPart.Size, EAllowShrinking::No);
					ISpeedRecorder::FRecord ActivityRecord;
					// Read data for hash check
					FileConstructorStat->OnBeforeRead();
					ActivityRecord.CyclesStart = FStatsCollector::GetCycles();
					NewFileReader->Serialize(ReadBuffer.GetData(), ChunkPart.Size);
					ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
					ActivityRecord.Size = ChunkPart.Size;
					HashState.Update(ReadBuffer.GetData(), ChunkPart.Size);
					FileConstructorStat->OnAfterRead(ActivityRecord);
					// Count bytes read from file
					ByteCounter = NextBytePosition;
					// Set to resume from next chunk part
					StartChunkPart = ChunkPartIdx + 1;
					// Inform the reference tracker of the chunk part skip
					bSuccess = ChunkReferenceTracker->PopReference(ChunkPart.Guid) && bSuccess;
					CountBytesProcessed(ChunkPart.Size);
					FileConstructorStat->OnFileProgress(BuildFilename, NewFileReader->Tell());
					// Wait if paused
					FileConstructorHelpers::WaitWhilePaused(bIsPaused, bShouldAbort);
				}
				else
				{
					// No more parts on disk
					break;
				}
			}
			// Set start position to the byte we got up to
			StartPosition = ByteCounter;
			// Close file
			NewFileReader->Close();
		}
	}

	// If we haven't done so yet, make the initial disk space check. We do this after resume
	// so that we know how much to discount from our current file size.
	if (!bInitialDiskSizeCheck)
	{
		bInitialDiskSizeCheck = true;

		// Normal operation can just use the classic calculation
		uint64 LocalDiskSpaceRequired = CalculateInProgressDiskSpaceRequired(FileManifest, StartPosition);

		// If we are delete-during-install this gets more complicated because we'll be freeing up
		// space as we add.
		if (Configuration.bDeleteChunkDBFilesAfterUse)
		{
			LocalDiskSpaceRequired = CalculateDiskSpaceRequirementsWithDeleteDuringInstall(ConstructionStack);
		}

		uint64 LocalDiskSpaceAvailable = 0;
		{
			uint64 TotalSize = 0;
			uint64 AvailableSpace = 0;
			if (FPlatformMisc::GetDiskTotalAndFreeSpace(Configuration.InstallDirectory, TotalSize, AvailableSpace))
			{
				LocalDiskSpaceAvailable = AvailableSpace;
			}
		}

		AvailableDiskSpace.store(LocalDiskSpaceAvailable, std::memory_order_release);
		RequiredDiskSpace.store(LocalDiskSpaceRequired, std::memory_order_release);	

		if (!FileConstructorHelpers::CheckRemainingDiskSpace(Configuration.InstallDirectory, LocalDiskSpaceRequired, LocalDiskSpaceAvailable))
		{
			UE_LOG(LogBuildPatchServices, Error, TEXT("Out of HDD space. Needs %llu bytes, Free %llu bytes"), LocalDiskSpaceRequired, LocalDiskSpaceAvailable);
			InstallerError->SetError(
				EBuildPatchInstallError::OutOfDiskSpace,
				DiskSpaceErrorCodes::InitialSpaceCheck,
				0,
				BuildPatchServices::GetDiskSpaceMessage(Configuration.InstallDirectory, LocalDiskSpaceRequired, LocalDiskSpaceAvailable));
			return false;
		}
	}

	// Now we can make sure the chunk cache knows to start downloading chunks
	if (!bIsDownloadStarted)
	{
		bIsDownloadStarted = true;
		FileConstructorStat->OnResumeCompleted();
	}

	// Returns false if the write failed in some way (almost certainly disk space, could be drive disconnection)
	auto FlushToAsyncWriter = [this](FArchive& DestinationFile, FSHA1& HashState)
	{
		if (bStallWhenFileSystemThrottled)
		{
			int64 AvailableBytes = FileSystem->GetAllowedBytesToWriteThrottledStorage(*DestinationFile.GetArchiveName());
			while (WriteBuffers[CurrentFillBuffer].Num() > AvailableBytes)
			{
				UE_LOG(LogBuildPatchServices, Display, TEXT("Avaliable write bytes to write throttled storage exhausted (%s).  Sleeping %ds.  Bytes needed: %u, bytes available: %lld")
					, *DestinationFile.GetArchiveName(), SleepTimeWhenFileSystemThrottledSeconds, WriteBuffers[CurrentFillBuffer].Num(), AvailableBytes);
				FPlatformProcess::Sleep(SleepTimeWhenFileSystemThrottledSeconds);
				AvailableBytes = FileSystem->GetAllowedBytesToWriteThrottledStorage(*DestinationFile.GetArchiveName());
			}
		}
		
		// Wait for the last write to complete.
		if (bWriteJobRunning)
		{
			// We can potentially wait here a while if we are FS throttled.
			// \todo old code didn't check for abort during throttling, should we add?
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ConstructFileFromChunks_WaitForLastWrite);
				WriteJobCompleteEvent->Wait();
			}
			bWriteJobRunning = false;

			if (DestinationFile.IsError())
			{
				return false;
			}

			// !CurrentFillBuffer is now available for use.
		}

		// Kick off the write on another thread while we hash the data here.
		WriteJobBufferToWrite = &WriteBuffers[CurrentFillBuffer];
		WriteJobArchive = &DestinationFile;
		bWriteJobRunning = true;
		WriteJobStartEvent->Trigger();

		// Hash the buffer we are writing while it's writing.
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ConstructFileFromChunks_Hash);
			HashState.Update(WriteBuffers[CurrentFillBuffer].GetData(), WriteBuffers[CurrentFillBuffer].Num());
		}

		// Start filling the next buffer.
		CurrentFillBuffer = !CurrentFillBuffer;
		WriteBuffers[CurrentFillBuffer].SetNumUninitialized(0, EAllowShrinking::No);

		return true;
	};

	// Attempt to create the file
	ISpeedRecorder::FRecord ActivityRecord;
	FileConstructorStat->OnBeforeAdminister();
	ActivityRecord.CyclesStart = FStatsCollector::GetCycles();
	TUniquePtr<FArchive> NewFile = FileSystem->CreateFileWriter(*NewFilename, bResumeExisting ? EWriteFlags::Append : EWriteFlags::None);
	LastError = FPlatformMisc::GetLastError();
	ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
	ActivityRecord.Size = 0;
	FileConstructorStat->OnAfterAdminister(ActivityRecord);
	bSuccess = NewFile != nullptr;
	if (bSuccess)
	{
		// Seek to file write position
		if (NewFile->Tell() != StartPosition)
		{
			FileConstructorStat->OnBeforeAdminister();
			ActivityRecord.CyclesStart = FStatsCollector::GetCycles();
			NewFile->Seek(StartPosition);
			ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
			ActivityRecord.Size = 0;
			FileConstructorStat->OnAfterAdminister(ActivityRecord);
		}

		// For each chunk, load it, and place it's data into the file
		for (int32 ChunkPartIdx = StartChunkPart; ChunkPartIdx < FileManifest.ChunkParts.Num() && bSuccess && !bShouldAbort; ++ChunkPartIdx)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ConstructFileFromChunks_Chunk);

			const FChunkPart& ChunkPart = FileManifest.ChunkParts[ChunkPartIdx];

			// If we can't fit in the buffer, flush. Conditional arranged to avoid overflow risk.
			if (ChunkPart.Size > (WriteBufferSize - WriteBuffers[CurrentFillBuffer].Num()))
			{
				if (!FlushToAsyncWriter(*NewFile, HashState))
				{
					bSuccess = false;
					InstallerAnalytics->RecordConstructionError(BuildFilename, INDEX_NONE, TEXT("Serialization Error"));
					UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Failed %s due to serialization error"), *BuildFilename);
					ConstructionError = EConstructionError::SerializeError;
					break;
				}
			}

			bSuccess = AppendChunkData(ChunkPart, WriteBuffers[CurrentFillBuffer], ConstructionError);

			FileConstructorStat->OnFileProgress(BuildFilename, NewFile->Tell() + WriteBuffers[CurrentFillBuffer].Num());
			if (bSuccess)
			{
				CountBytesProcessed(ChunkPart.Size);
				// Wait while paused
				FileConstructorHelpers::WaitWhilePaused(bIsPaused, bShouldAbort);
			}
			// Only report or log if this is the first error
			else if (InstallerError->HasError() == false)
			{
				if (ConstructionError == EConstructionError::MissingChunk)
				{
					InstallerAnalytics->RecordConstructionError(BuildFilename, INDEX_NONE, TEXT("Missing Chunk"));
					UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Failed %s due to missing chunk %s"), *BuildFilename, *ChunkPart.Guid.ToString());
				}
				else if (ConstructionError == EConstructionError::TrackingError)
				{
					InstallerAnalytics->RecordConstructionError(BuildFilename, INDEX_NONE, TEXT("Tracking Error"));
					UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Failed %s due to untracked chunk %s"), *BuildFilename, *ChunkPart.Guid.ToString());
				}
			}
		}

		if (WriteBuffers[CurrentFillBuffer].Num())
		{
			if (!FlushToAsyncWriter(*NewFile, HashState))
			{
				bSuccess = false;
				InstallerAnalytics->RecordConstructionError(BuildFilename, INDEX_NONE, TEXT("Serialization Error"));
				UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Failed %s due to serialization error"), *BuildFilename);
				ConstructionError = EConstructionError::SerializeError;
			}
		}

		// Wait for the last write if there is one
		if (bWriteJobRunning)
		{
			WriteJobCompleteEvent->Wait();
			bWriteJobRunning = false;
		}

		bSuccess = !NewFile->IsError();

		// Update this for disk space requirements tracking below on error
		StartPosition = NewFile->Tell();

		// Close the file writer
		FileConstructorStat->OnBeforeAdminister();
		ActivityRecord.CyclesStart = FStatsCollector::GetCycles();

		const bool bArchiveSuccess = NewFile->Close();
		NewFile.Reset();
		ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
		ActivityRecord.Size = 0;
		FileConstructorStat->OnAfterAdminister(ActivityRecord);

		// Check for final success
		if (ConstructionError == EConstructionError::None && !bArchiveSuccess)
		{
			ConstructionError = EConstructionError::SerializeError;
			bSuccess = false;
		}
	}
	else
	{
		ConstructionError = EConstructionError::CannotCreateFile;
	}

	// Check for error state
	if (!bSuccess)
	{
		if (ConstructionError == EConstructionError::SerializeError)
		{
			// Serialize error is our catchall file error right now. This should probably get
			// migrated such that it's when we fail to load an existing chunk (i.e. corruption)
			// but instead that shows up as a missing chunk.


			uint64 TotalSize = 0;
			uint64 FreeSize = 0;
			if (FPlatformMisc::GetDiskTotalAndFreeSpace(Configuration.InstallDirectory, TotalSize, FreeSize))
			{
				// We're responding to an actual failure, which would have happened because we literally weren't able
				// to write our write butter. Because of transient stuff this might not be correct so we double our
				// write buffer size for this check.
				if (FreeSize < (2 * WriteBufferSize))
				{
					// We've already failed so it makes sense to reevaluate how much extra we need. 
					// I'm not sure I like using the same error wording for initial and ongoing disk space failure, but whatevs
					{
						uint64 LocalDiskSpaceRequired = CalculateInProgressDiskSpaceRequired(FileManifest, StartPosition);

						// If we are delete-during-install this gets more complicated because we'll be freeing up
						// space as we add.
						if (Configuration.bDeleteChunkDBFilesAfterUse)
						{
							LocalDiskSpaceRequired = CalculateDiskSpaceRequirementsWithDeleteDuringInstall(ConstructionStack);
						}

						AvailableDiskSpace.store(FreeSize, std::memory_order_release);
						RequiredDiskSpace.store(LocalDiskSpaceRequired, std::memory_order_release);

					}

					ConstructionError = EConstructionError::OutOfDiskSpace;
				}
				else
				{
					// If it looks like we had enough disk space to write the last buffer, then 
					// leave it as serialize.
				}
			}
			else
			{
				// If we can't get the free space then likely the disk has disconnected or otherwise had a Bad Error, leave
				// as serialize.
			}
		}

		// \todo not exactly sure why this only reports on a file creation error?
		const bool bReportAnalytic = InstallerError->HasError() == false;
		switch (ConstructionError)
		{
		case EConstructionError::OutOfDiskSpace:
			{
				uint64 LocalAvailableDiskSpace = AvailableDiskSpace.load(std::memory_order_acquire);
				uint64 LocalRequiredDiskSpace = RequiredDiskSpace.load(std::memory_order_acquire);
				UE_LOG(LogBuildPatchServices, Error, TEXT("Out of HDD space. Needs %llu bytes, Free %llu bytes"), LocalRequiredDiskSpace, LocalAvailableDiskSpace);
				InstallerError->SetError(
					EBuildPatchInstallError::OutOfDiskSpace,
					DiskSpaceErrorCodes::DuringInstallation,
					0,
					BuildPatchServices::GetDiskSpaceMessage(Configuration.InstallDirectory, LocalRequiredDiskSpace, LocalAvailableDiskSpace));
				break;
			}
		case EConstructionError::CannotCreateFile:
			{
				if (bReportAnalytic)
				{
					InstallerAnalytics->RecordConstructionError(BuildFilename, LastError, TEXT("Could Not Create File"));
					UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Could not create %s"), *BuildFilename);
				}
				InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::FileCreateFail, LastError);
				break;
			}
		case EConstructionError::MissingChunk:
			{
				InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::MissingChunkData);
				break;
			}
		case EConstructionError::SerializeError:
			{
				InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::SerializationError);
				break;
			}
		case EConstructionError::TrackingError:
			{
				InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::TrackingError);
				break;
			}
		}
	}

	// Verify the hash for the file that we created
	if (bSuccess)
	{
		HashState.Final();
		HashState.GetHash(HashValue.Hash);
		bSuccess = HashValue == FileManifest.FileHash;
		if (!bSuccess)
		{
			ConstructionError = EConstructionError::OutboundDataError;
			// Only report or log if the first error
			if (InstallerError->HasError() == false)
			{
				InstallerAnalytics->RecordConstructionError(BuildFilename, INDEX_NONE, TEXT("Serialised Verify Fail"));
				UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Verify failed after constructing %s"), *BuildFilename);
			}
			// Always set
			InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::OutboundCorrupt);
		}
	}

#if PLATFORM_MAC
	if (bSuccess && EnumHasAllFlags(FileManifest.FileMetaFlags, EFileMetaFlags::UnixExecutable))
	{
		// Enable executable permission bit
		struct stat FileInfo;
		if (stat(TCHAR_TO_UTF8(*NewFilename), &FileInfo) == 0)
		{
			bSuccess = chmod(TCHAR_TO_UTF8(*NewFilename), FileInfo.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) == 0;
		}
	}
#endif

#if PLATFORM_ANDROID
	if (bSuccess)
	{
		IFileManager::Get().SetTimeStamp(*NewFilename, FDateTime::UtcNow());
	}
#endif

	if (bSuccess)
	{
		ChunkSource->ReportFileCompletion();
	}

	// Delete the staging file if unsuccessful by means of any failure that could leave the file in unknown state.
	if (!bSuccess)
	{
		switch (ConstructionError)
		{
		case EConstructionError::CannotCreateFile:
		case EConstructionError::SerializeError:
		case EConstructionError::TrackingError:
		case EConstructionError::OutboundDataError:
			if (!FileSystem->DeleteFile(*NewFilename))
			{
				UE_LOG(LogBuildPatchServices, Warning, TEXT("FBuildPatchFileConstructor: Error deleting file: %s (Error Code %i)"), *NewFilename, FPlatformMisc::GetLastError());
			}
			break;
		}
	}

	return bSuccess;
}

bool FBuildPatchFileConstructor::AppendChunkData(const FChunkPart& ChunkPart, TArray<uint8>& DestinationBuffer, EConstructionError& ConstructionError)
{
	ConstructionError = EConstructionError::None;
	
	FileConstructorStat->OnChunkGet(ChunkPart.Guid);
	IChunkDataAccess* ChunkDataAccess = nullptr;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetChunkData);
		ChunkDataAccess = ChunkSource->Get(ChunkPart.Guid);
	}
	if (ChunkDataAccess != nullptr)
	{
		uint8* Data;
		ChunkDataAccess->GetDataLock(&Data, nullptr);

		uint8* DataStart = &Data[ChunkPart.Offset];

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GetChunkData_Copy);
			DestinationBuffer.Append(DataStart, ChunkPart.Size);
		}

		ChunkDataAccess->ReleaseDataLock();
		const bool bPopReferenceOk = ChunkReferenceTracker->PopReference(ChunkPart.Guid);
		if (!bPopReferenceOk)
		{
			ConstructionError = EConstructionError::TrackingError;
		}
	}
	else
	{
		// We'd really like to know if this was because it's missing or because it failed in some way.
		ConstructionError = EConstructionError::MissingChunk;
	}
	return ConstructionError == EConstructionError::None;
}

void FBuildPatchFileConstructor::DeleteDirectoryContents(const FString& RootDirectory)
{
	TArray<FString> SubDirNames;
	IFileManager::Get().FindFiles(SubDirNames, *(RootDirectory / TEXT("*")), false, true);
	for (const FString& DirName : SubDirNames)
	{
		IFileManager::Get().DeleteDirectory(*(RootDirectory / DirName), false, true);
	}

	TArray<FString> SubFileNames;
	IFileManager::Get().FindFiles(SubFileNames, *(RootDirectory / TEXT("*")), true, false);
	for (const FString& FileName : SubFileNames)
	{
		IFileManager::Get().Delete(*(RootDirectory / FileName), false, true);
	}
}
