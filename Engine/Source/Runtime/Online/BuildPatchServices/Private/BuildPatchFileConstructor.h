// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "HAL/Runnable.h"
#include "BuildPatchProgress.h"
#include "BuildPatchManifest.h"
#include "Installer/Controllable.h"
#include "Common/SpeedRecorder.h"
#include "BuildPatchInstall.h"

// Forward declarations
class FBuildPatchAppManifest;
enum class EConstructionError : uint8;

class IBuildInstallerSharedContext;

namespace BuildPatchServices
{
	

	struct FChunkPart;
	class IFileSystem;
	class IChunkSource;
	class IChunkReferenceTracker;
	class IInstallerError;
	class IInstallerAnalytics;
	class IFileConstructorStat;
	class IBuildManifestSet;
	class IBuildInstallerThread;
	class IChunkDbChunkSource;

	/**
	 * A struct containing the configuration values for a file constructor.
	 */
	struct FFileConstructorConfig
	{
		// The manifest set class for details on the installation files.
		IBuildManifestSet* ManifestSet;

		// The location for the installation.
		FString InstallDirectory;

		// The location where new installation files will be constructed.
		FString StagingDirectory;

		// The location where temporary files for tracking can be stored.
		FString MetaDirectory;

		// The list of files to be constructed, filename paths should match those contained in manifest.
		TArray<FString> ConstructList;

		// The install mode used for this installation.
		EInstallMode InstallMode;

		IBuildInstallerSharedContext* SharedContext;

		bool bDeleteChunkDBFilesAfterUse = false;
	};

	/**
	 * FBuildPatchFileConstructor
	 * This class controls a thread that constructs files from a file list, given install details, and chunk availability notifications
	 */
	class FBuildPatchFileConstructor : public IControllable
	{
	public:

		/**
		 * Constructor
		 * @param Configuration             The configuration for the constructor.
		 * @param FileSystem                The service used to open files.
		 * @param ChunkSource               Pointer to the chunk source.
		 * @param ChunkReferenceTracker     Pointer to the chunk reference tracker.
		 * @param InstallerError            Pointer to the installer error class for reporting fatal errors.
		 * @param InstallerAnalytics        Pointer to the installer analytics handler for reporting events.
		 * @param FileConstructorStat       Pointer to the stat class for receiving updates.
		 */
		FBuildPatchFileConstructor(
			FFileConstructorConfig Configuration, IFileSystem* FileSystem, IChunkSource* ChunkSource, 
			IChunkDbChunkSource* ChunkDbChunkSource, IChunkReferenceTracker* ChunkReferenceTracker, IInstallerError* InstallerError, 
			IInstallerAnalytics* InstallerAnalytics, IFileConstructorStat* FileConstructorStat);

		/**
		 * Default Destructor, will delete the allocated Thread
		 */
		~FBuildPatchFileConstructor();

		void Run();

		// IControllable interface begin.
		virtual void SetPaused(bool bInIsPaused) override
		{
			bIsPaused = bInIsPaused;
		}

		virtual void Abort() override
		{
			bShouldAbort = true;
		}
		// IControllable interface end.

		/**
		 * Get the disk space that was required to perform the installation. This can change over time and indicates the required
		 * space to _finish_ the installation from the current state. It is not initialized until after resume is processed and returns
		 * zero until that time. Note that since this and GetAvailableDiskSpace are separate accessors there's no guarantee that they
		 * match - e.g. if you call GetRequiredDiskSpace and then GetAvailableDiskSpace immediately afterwards, it's possible the Available
		 * Disk Space value is from a later call. This is highly unlikely due to how rare these updates are, but it's possible. Use these
		 * for UI purposes only.
		 */
		uint64 GetRequiredDiskSpace();

		/**
		 * Get the disk space that was available when last updating RequiredDiskSpace. See notes with GetRequiredDiskSpace.
		 * It's possible for this to return 0 due to the underlying operating system being unable to report a value in cases of
		 * e.g. the drive being disconnected.
		 */
		uint64 GetAvailableDiskSpace();

		/**
		 * Broadcasts with full filepath to file that the constructor is about to delete in order to free up space.
		 * @return	Reference to the event object.
		 */
		DECLARE_EVENT_OneParam(FBuildPatchFileConstructor, FOnBeforeDeleteFile, const FString& /*BuildFile*/);
		FOnBeforeDeleteFile& OnBeforeDeleteFile();

	private:

		/**
		 * Count additional bytes processed, and set new install progress value
		 * @param ByteCount		Number of bytes to increment by
		 */
		void CountBytesProcessed(const int64& ByteCount);

		/**
		 * Function to fetch a chunk from the download list
		 * @param Filename		Receives the filename for the file to construct from the manifest
		 * @return true if there was a chunk guid in the list
		 */
		bool GetFileToConstruct(FString& Filename);

		/**
		 * @return the total bytes size of files not yet started construction
		 */
		int64 GetRemainingBytes();

		/**
		 * Calculates the minimum required disk space for the remaining work to be completed, based on a current file, and the list of files left in ConstructionStack.
		 * @param InProgressFileManifest	The manifest for the file currently being constructed.
		 * @param InProgressFileSize		The remaining size required for the file currently being constructed.
		 * @return the number of bytes required on disk to complete the installation.
		 */
		uint64 CalculateInProgressDiskSpaceRequired(const FFileManifest& InProgressFileManifest, uint64 InProgressFileSize);

		// Calculates the amount of disk space we need to finish the install, needs to be called on file boundaries.
		uint64 CalculateDiskSpaceRequirementsWithDeleteDuringInstall(const TArray<FString>& InConstructionStack);

		/**
		 * Constructs a particular file referenced by the given BuildManifest. The function takes an interface to a class that can provide availability information of chunks so that this
		 * file construction process can be ran alongside chunk acquisition threads. It will Sleep while waiting for chunks that it needs.
		 * @param BuildFilename		The relative build filename to use for construction.
		 * @param FileManifest		The FFileManifest for the file to construct.
		 * @param bResumeExisting	Whether we should resume from an existing file
		 * @return	true if no file errors occurred
		 */
		bool ConstructFileFromChunks(const FString& BuildFilename, const FFileManifest& FileManifest, bool bResumeExisting);

		/**
		 * Adds the data from a chunk to the given buffer.
		 * @param ChunkPart          The chunk part details.
		 * @param ConstructionError  Will be set to the error type that ocurred or EConstructionError::None.
		 * @return true if no errors were detected
		 */
		bool AppendChunkData(const FChunkPart& ChunkPart, TArray<uint8>& DestinationBuffer, EConstructionError& ConstructionError);

		/**
		 * Delete all contents of a directory
		 * @param RootDirectory	 	Directory to make empty
		 */
		void DeleteDirectoryContents(const FString& RootDirectory);

	private:
		// The configuration for the constructor.
		const FFileConstructorConfig Configuration;

		// A flag marking that we told the chunk cache to queue required downloads.
		bool bIsDownloadStarted;

		// A flag marking that we have made the initial disk space check following resume logic complete.
		bool bInitialDiskSizeCheck;

		// A flag marking whether we should be paused.
		FThreadSafeBool bIsPaused;

		// A flag marking whether we should abort operations and exit.
		FThreadSafeBool bShouldAbort;

		// A critical section to protect the flags and variables.
		FCriticalSection ThreadLock;

		// A stack of filenames for files that need to be constructed.
		TArray<FString> ConstructionStack;

		// Pointer to the file system.
		IFileSystem* FileSystem;

		// Pointer to chunk source.
		IChunkSource* ChunkSource;
		IChunkDbChunkSource* ChunkDbSource; // can be null if not using.

		// Pointer to the chunk reference tracker.
		IChunkReferenceTracker* ChunkReferenceTracker;

		// Pointer to the installer error class.
		IInstallerError* InstallerError;

		// Pointer to the installer analytics handler.
		IInstallerAnalytics* InstallerAnalytics;

		// Pointer to the stat class.
		IFileConstructorStat* FileConstructorStat;

		// Total job size for tracking progress.
		int64 TotalJobSize;

		// Byte processed so far for tracking progress.
		int64 ByteProcessed;

		// The amount of disk space requirement that was calculated when beginning the process. 0 if the install process was not started, or no additional space was needed.
		std::atomic_uint64_t RequiredDiskSpace;

		// The amount of disk space available when beginning the process. 0 if the install process was not started.
		std::atomic_uint64_t AvailableDiskSpace;

		// Event executed before deleting an old installation file.
		FOnBeforeDeleteFile BeforeDeleteFileEvent;

		//
		// Async write management.
		IBuildInstallerThread* WriteJobThread;
		void WriteJobThreadRun();

		// We ping pong between two buffers, filling/hashing one, and writing the other.
		TArray<uint8> WriteBuffers[2];
		int32 CurrentFillBuffer = 0;
		uint32 WriteBufferSize = (4 << 20); // Default write buffer size 4MB.

		FEvent* WriteJobCompleteEvent = nullptr;
		FEvent* WriteJobStartEvent = nullptr;
		TArray<uint8>* WriteJobBufferToWrite = nullptr;
		FArchive* WriteJobArchive = nullptr;
		std::atomic_bool bWriteJobCompleted = false; // Only set to true if the Serialize() call was completed.
		bool bWriteJobRunning = false; // Foreground thread only - have we dispatched a job?

		// Where we are in the chunk consumption list after each file.
		TArray<int32> FileCompletionPositions;
	};

	/**
	 * This interface defines the statistics class required by the file constructor. It should be implemented in order to collect
	 * desired information which is being broadcast by the system.
	 */
	class IFileConstructorStat
	{
	public:
		virtual ~IFileConstructorStat() {}

		/**
		 * Called when the resume process begins.
		 */
		virtual void OnResumeStarted() = 0;

		/**
		 * Called when the resume process completes.
		 */
		virtual void OnResumeCompleted() = 0;

		/**
		 * Called for each Get made to the chunk source.
		 * @param ChunkId       The id for the chunk required.
		 */
		virtual void OnChunkGet(const FGuid& ChunkId) = 0;

		/**
		 * Called when a file construction has started.
		 * @param Filename      The filename of the file.
		 * @param FileSize      The size of the file being constructed.
		 */
		virtual void OnFileStarted(const FString& Filename, int64 FileSize) = 0;

		/**
		 * Called during a file construction with the current progress.
		 * @param Filename      The filename of the file.
		 * @param TotalBytes    The number of bytes processed so far.
		 */
		virtual void OnFileProgress(const FString& Filename, int64 TotalBytes) = 0;

		/**
		 * Called when a file construction has completed.
		 * @param Filename      The filename of the file.
		 * @param bSuccess      True if the file construction succeeded.
		 */
		virtual void OnFileCompleted(const FString& Filename, bool bSuccess) = 0;

		/**
		 * Called when the construction process completes.
		 */
		virtual void OnConstructionCompleted() = 0;

		/**
		 * Called to update the total amount of bytes which have been constructed.
		 * @param TotalBytes    The number of bytes constructed so far.
		 */
		virtual void OnProcessedDataUpdated(int64 TotalBytes) = 0;

		/**
		 * Called to update the total number of bytes to be constructed.
		 * @param TotalBytes    The total number of bytes to be constructed.
		 */
		virtual void OnTotalRequiredUpdated(int64 TotalBytes) = 0;

		/**
		 * Called when we are beginning a file administration, such as open, close, seek.
		 */
		virtual void OnBeforeAdminister() = 0;

		/**
		 * Called upon completing an admin operation, with activity recording.
		 * @param Record        The activity record.
		 */
		virtual void OnAfterAdminister(const ISpeedRecorder::FRecord& Record) = 0;

		/**
		 * Called when we are beginning a read operation.
		 */
		virtual void OnBeforeRead() = 0;

		/**
		 * Called upon completing a read operation, with activity recording.
		 * @param Record        The activity record.
		 */
		virtual void OnAfterRead(const ISpeedRecorder::FRecord& Record) = 0;

		/**
		 * Called when we are beginning a write operation.
		 */
		virtual void OnBeforeWrite() = 0;

		/**
		 * Called upon completing a write operation, with activity recording.
		 * @param Record        The activity record.
		 */
		virtual void OnAfterWrite(const ISpeedRecorder::FRecord& Record) = 0;
	};
}

/**
 * Helpers for calculations that are useful for other classes or operations.
 */
namespace FileConstructorHelpers
{
	uint64 CalculateRequiredDiskSpace(const FBuildPatchAppManifestPtr& CurrentManifest, const FBuildPatchAppManifestRef& BuildManifest, const BuildPatchServices::EInstallMode& InstallMode, const TSet<FString>& InstallTags);
}
