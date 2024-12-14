// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCompilerUtil.cpp: Implements FShaderCompileUtilities.
=============================================================================*/

#include "ShaderCompilerPrivate.h"

#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Serialization/MemoryWriter.h"
#include "ShaderDiagnostics.h"


static TAutoConsoleVariable<bool> CVarDebugDumpWorkerInputs(
	TEXT("r.ShaderCompiler.DebugDumpWorkerInputs"),
	false,
	TEXT("If true, worker input files will be saved for each individual compile job alongside other debug data (note that r.DumpShaderDebugInfo must also be enabled for this to function)"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<bool> CVarShadersPropagateLocalWorkerOOMs(
	TEXT("r.Shaders.PropagateLocalWorkerOOMs"),
	false,
	TEXT("When set, out-of-memory conditions in a local shader compile worker will be treated as regular out-of-memory conditions and propagated to the main process.\n")
	TEXT("This is useful when running in environment with hard memory limits, where it does not matter which process in particular caused us to violate the memory limit."),
	ECVF_Default);


static void ModalErrorOrLog(const FString& Title, const FString& Text, int64 CurrentFilePos = 0, int64 ExpectedFileSize = 0, bool bIsErrorFatal = true)
{
	static FThreadSafeBool bModalReported;

	FString BadFile;
	if (CurrentFilePos > ExpectedFileSize)
	{
		// Corrupt file
		BadFile = FString::Printf(TEXT(" (Truncated or corrupt output file! Current file pos %lld, file size %lld)"), CurrentFilePos, ExpectedFileSize);
	}

	if (bIsErrorFatal)
	{
		// Ensure errors are logged before exiting
		GLog->Panic();

		if (FPlatformProperties::SupportsWindowedMode() && !FApp::IsUnattended())
		{
			if (!bModalReported.AtomicSet(true))
			{
				UE_LOG(LogShaderCompilers, Error, TEXT("%s\n%s"), *Text, *BadFile);

				// Show dialog box with error message and request exit
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Text), FText::FromString(Title));
				constexpr bool bForceExit = true;
				FPlatformMisc::RequestExit(bForceExit, TEXT("ShaderCompiler.ModalErrorOrLog"));
			}
			else
			{
				// Another thread already opened a dialog box and requests exit
				FPlatformProcess::SleepInfinite();
			}
		}
		else
		{
			UE_LOG(LogShaderCompilers, Fatal, TEXT("%s\n%s\n%s"), *Title, *Text, *BadFile);
		}
	}
	else
	{
		UE_LOG(LogShaderCompilers, Error, TEXT("%s\n%s\n%s"), *Title, *Text, *BadFile);
	}
}

static void ReissueShaderCompileJobs(const TArray<FShaderCommonCompileJob*>& SourceJobs)
{
	if (SourceJobs.Num())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ReissueShaderCompileJobs);

		TArray<FShaderCommonCompileJobPtr> ReissueJobs;
		ReissueJobs.Reserve(SourceJobs.Num());
		const uint32 JobId = FShaderCommonCompileJob::GetNextJobId();
		for (const FShaderCommonCompileJob* SourceJob : SourceJobs)
		{
			if (const FShaderCompileJob* SingleSourceJob = SourceJob->GetSingleShaderJob())
			{
				if (FShaderCompileJob* ReissueJob = GShaderCompilingManager->PrepareShaderCompileJob(JobId, SingleSourceJob->Key, SingleSourceJob->Priority))
				{
					ReissueJob->Input = SingleSourceJob->Input;
					ReissueJobs.Add(FShaderCommonCompileJobPtr(ReissueJob));
				}
			}
			else if (const FShaderPipelineCompileJob* PipelineSourceJob = SourceJob->GetShaderPipelineJob())
			{
				if (FShaderPipelineCompileJob* ReissueJob = GShaderCompilingManager->PreparePipelineCompileJob(JobId, PipelineSourceJob->Key, PipelineSourceJob->Priority))
				{
					ReissueJob->StageJobs = PipelineSourceJob->StageJobs;
					ReissueJobs.Add(FShaderCommonCompileJobPtr(ReissueJob));
				}
			}
			else
			{
				checkf(0, TEXT("Reissued shader compile job is neither a single nor a pipeline job"));
			}
		}

		GShaderCompilingManager->SubmitJobs(ReissueJobs, FString(""), FString(""));
	}
}

// Make functions so the crash reporter can disambiguate the actual error because of the different callstacks
namespace ShaderCompileWorkerError
{
	void HandleGeneralCrash(const TCHAR* ExceptionInfo, const TCHAR* Callstack)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker crashed"), *FString::Printf(TEXT("Exception:\n%s\n\nCallstack:\n%s"), ExceptionInfo, Callstack));
	}

	void HandleBadShaderFormatVersion(const TCHAR* Data)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), Data);
	}

	void HandleBadInputVersion(const TCHAR* Data)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), Data);
	}

	void HandleBadSingleJobHeader(const TCHAR* Data)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), Data);
	}

	void HandleBadPipelineJobHeader(const TCHAR* Data)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), Data);
	}

	void HandleCantDeleteInputFile(const TCHAR* Data)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), Data);
	}

	void HandleCantSaveOutputFile(const TCHAR* Data)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), Data);
	}

	void HandleNoTargetShaderFormatsFound(const TCHAR* Data)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), Data);
	}

	void HandleCantCompileForSpecificFormat(const TCHAR* Data)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), Data);
	}

	void HandleOutputFileEmpty(const TCHAR* Filename)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), FString::Printf(TEXT("Output file %s size is 0. Are you out of disk space?"), Filename));
	}

	void HandleOutputFileCorrupted(const TCHAR* Filename, int64 ExpectedSize, int64 ActualSize)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), FString::Printf(TEXT("Output file corrupted (expected %I64d bytes, but only got %I64d): %s"), ExpectedSize, ActualSize, Filename));
	}

	void HandleCrashInsidePlatformCompiler(const TCHAR* Data)
	{
		// If the crash originates from a platform compiler, the error code must have been reported and we don't have to assume a corrupted output file.
		// In that case, don't crash the cooker with a fatal error, just report the error so the cooker can dump debug info.
		constexpr bool bIsErrorFatal = false;
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), FString::Printf(TEXT("Crash inside the platform compiler:\n%s"), Data), 0, 0, bIsErrorFatal);
	}

	void HandleBadInputFile(const TCHAR* Data)
	{
		ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), FString::Printf(TEXT("Bad-input-file exception:\n%s"), Data));
	}

	bool HandleOutOfMemory(const TCHAR* ExceptionInfo, const TCHAR* Hostname, const FPlatformMemoryStats& MemoryStats, const TArray<FShaderCommonCompileJobPtr>& QueuedJobs)
	{
		constexpr int64 Gibibyte = 1024 * 1024 * 1024;
		const FString ErrorReport = FString::Printf(
			TEXT("ShaderCompileWorker failed with out-of-memory (OOM) exception on machine \"%s\" (%s); MemoryStats:")
			TEXT("\n\tAvailablePhysical %llu (%.2f GiB)")
			TEXT("\n\t AvailableVirtual %llu (%.2f GiB)")
			TEXT("\n\t     UsedPhysical %llu (%.2f GiB)")
			TEXT("\n\t PeakUsedPhysical %llu (%.2f GiB)")
			TEXT("\n\t      UsedVirtual %llu (%.2f GiB)")
			TEXT("\n\t  PeakUsedVirtual %llu (%.2f GiB)"),
			Hostname,
			(ExceptionInfo[0] == TEXT('\0') ? TEXT("No exception information") : ExceptionInfo),
			MemoryStats.AvailablePhysical, double(MemoryStats.AvailablePhysical) / Gibibyte,
			MemoryStats.AvailableVirtual, double(MemoryStats.AvailableVirtual) / Gibibyte,
			MemoryStats.UsedPhysical, double(MemoryStats.UsedPhysical) / Gibibyte,
			MemoryStats.PeakUsedPhysical, double(MemoryStats.PeakUsedPhysical) / Gibibyte,
			MemoryStats.UsedVirtual, double(MemoryStats.UsedVirtual) / Gibibyte,
			MemoryStats.PeakUsedVirtual, double(MemoryStats.PeakUsedVirtual) / Gibibyte
		);

		if (GShaderCompilingManager->IsRemoteCompilingEnabled())
		{
			// Remote shader compiler supports re-compiling jobs on local machine
			UE_LOG(LogShaderCompilers, Warning, TEXT("%s\nRecompile %d shader compile %s locally"), *ErrorReport, QueuedJobs.Num(), (QueuedJobs.Num() == 1 ? TEXT("job") : TEXT("jobs")));
			return true;
		}
		else
		{
			if (CVarShadersPropagateLocalWorkerOOMs.GetValueOnAnyThread())
			{
				FPlatformMemory::OnOutOfMemory(0, 64);
			}
			ModalErrorOrLog(TEXT("ShaderCompileWorker failed"), ErrorReport);
			return false;
		}
	}
}

static TMap<FString, uint32> GetFormatVersionMap()
{
	TMap<FString, uint32> FormatVersionMap;

	const TArray<const class IShaderFormat*>& ShaderFormats = GetTargetPlatformManagerRef().GetShaderFormats();
	check(ShaderFormats.Num());
	for (int32 Index = 0; Index < ShaderFormats.Num(); Index++)
	{
		TArray<FName> OutFormats;
		ShaderFormats[Index]->GetSupportedFormats(OutFormats);
		check(OutFormats.Num());
		for (int32 InnerIndex = 0; InnerIndex < OutFormats.Num(); InnerIndex++)
		{
			uint32 Version = ShaderFormats[Index]->GetVersion(OutFormats[InnerIndex]);
			FormatVersionMap.Add(OutFormats[InnerIndex].ToString(), Version);
		}
	}

	return FormatVersionMap;
}

#if 0 //UNUSED
static int32 GetNumTotalJobs(const TArray<FShaderCommonCompileJobPtr>& Jobs)
{
	int32 NumJobs = 0;
	for (int32 Index = 0; Index < Jobs.Num(); ++Index)
	{
		auto* PipelineJob = Jobs[Index]->GetShaderPipelineJob();
		NumJobs += PipelineJob ? PipelineJob->StageJobs.Num() : 1;
	}

	return NumJobs;
}
#endif

static void SplitJobsByType(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, TArray<FShaderCompileJob*>& OutQueuedSingleJobs, TArray<FShaderPipelineCompileJob*>& OutQueuedPipelineJobs)
{
	for (int32 Index = 0; Index < QueuedJobs.Num(); ++Index)
	{
		FShaderCommonCompileJobPtr CommonJob = QueuedJobs[Index];
		if (FShaderCompileJob* SingleJob = CommonJob->GetSingleShaderJob())
		{
			OutQueuedSingleJobs.Add(SingleJob);

		}
		else if (FShaderPipelineCompileJob* PipelineJob = CommonJob->GetShaderPipelineJob())
		{
			OutQueuedPipelineJobs.Add(PipelineJob);
		}
		else
		{
			checkf(0, TEXT("FShaderCommonCompileJob::Type=%d is not a valid type for a shader compile job"), (int32)CommonJob->Type);
		}
	}
}

static bool DoWriteTasksInner(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, FArchive& InTransferFile, IDistributedBuildController* BuildDistributionController, bool bUseRelativePaths, bool bCompressTaskFile)
{
	int32 InputVersion = ShaderCompileWorkerInputVersion;
	InTransferFile << InputVersion;

	TArray<uint8> UncompressedArray;
	FMemoryWriter TransferMemory(UncompressedArray);
	FArchive& TransferFile = bCompressTaskFile ? TransferMemory : InTransferFile;
	if (!bCompressTaskFile)
	{
		// still write NAME_None as string
		FString FormatNone = FName(NAME_None).ToString();
		TransferFile << FormatNone;
	}

	static TMap<FString, uint32> FormatVersionMap = GetFormatVersionMap();

	TransferFile << FormatVersionMap;

	TArray<FShaderCompileJob*> QueuedSingleJobs;
	TArray<FShaderPipelineCompileJob*> QueuedPipelineJobs;
	SplitJobsByType(QueuedJobs, QueuedSingleJobs, QueuedPipelineJobs);

	TArray<TRefCountPtr<FSharedShaderCompilerEnvironment>> SharedEnvironments;
	TArray<const FShaderParametersMetadata*> RequestShaderParameterStructures;

	// gather shared environments and parameter structures, these tend to be shared between jobs
	{

		for (int32 JobIndex = 0; JobIndex < QueuedSingleJobs.Num(); JobIndex++)
		{
			QueuedSingleJobs[JobIndex]->Input.GatherSharedInputs(SharedEnvironments, RequestShaderParameterStructures);
		}

		for (int32 JobIndex = 0; JobIndex < QueuedPipelineJobs.Num(); JobIndex++)
		{
			auto* PipelineJob = QueuedPipelineJobs[JobIndex];
			int32 NumStageJobs = PipelineJob->StageJobs.Num();

			for (int32 Index = 0; Index < NumStageJobs; Index++)
			{
				PipelineJob->StageJobs[Index]->Input.GatherSharedInputs(SharedEnvironments, RequestShaderParameterStructures);
			}
		}

		int32 NumSharedEnvironments = SharedEnvironments.Num();
		TransferFile << NumSharedEnvironments;

		for (int32 EnvironmentIndex = 0; EnvironmentIndex < SharedEnvironments.Num(); EnvironmentIndex++)
		{
			TransferFile << *SharedEnvironments[EnvironmentIndex];
		}
	}

	// Write shader parameter structures
	TArray<const FShaderParametersMetadata*> AllShaderParameterStructures;
	{
		// List all dependencies.
		for (int32 StructId = 0; StructId < RequestShaderParameterStructures.Num(); StructId++)
		{
			RequestShaderParameterStructures[StructId]->IterateStructureMetadataDependencies(
				[&](const FShaderParametersMetadata* Struct)
			{
				AllShaderParameterStructures.AddUnique(Struct);
			});
		}

		// Write all shader parameter structure.
		int32 NumParameterStructures = AllShaderParameterStructures.Num();
		TransferFile << NumParameterStructures;
		for (const FShaderParametersMetadata* Struct : AllShaderParameterStructures)
		{
			FString LayoutName = Struct->GetLayout().GetDebugName();
			FString StructTypeName = Struct->GetStructTypeName();
			FString ShaderVariableName = Struct->GetShaderVariableName();
			uint8 UseCase = uint8(Struct->GetUseCase());
			FString StructFileName = FString(ANSI_TO_TCHAR(Struct->GetFileName()));
			int32 StructFileLine = Struct->GetFileLine();
			uint32 Size = Struct->GetSize();
			int32 MemberCount = Struct->GetMembers().Num();

			static_assert(sizeof(UseCase) == sizeof(FShaderParametersMetadata::EUseCase), "Cast failure.");

			TransferFile << LayoutName;
			TransferFile << StructTypeName;
			TransferFile << ShaderVariableName;
			TransferFile << UseCase;
			TransferFile << StructFileName;
			TransferFile << StructFileLine;
			TransferFile << Size;
			TransferFile << MemberCount;

			for (const FShaderParametersMetadata::FMember& Member : Struct->GetMembers())
			{
				FString Name = Member.GetName();
				FString ShaderType = Member.GetShaderType();
				int32 FileLine = Member.GetFileLine();
				uint32 Offset = Member.GetOffset();
				uint8 BaseType = uint8(Member.GetBaseType());
				uint8 PrecisionModifier = uint8(Member.GetPrecision());
				uint32 NumRows = Member.GetNumRows();
				uint32 NumColumns = Member.GetNumColumns();
				uint32 NumElements = Member.GetNumElements();
				int32 StructMetadataIndex = INDEX_NONE;
				if (Member.GetStructMetadata())
				{
					StructMetadataIndex = AllShaderParameterStructures.Find(Member.GetStructMetadata());
					check(StructMetadataIndex != INDEX_NONE);
				}

				static_assert(sizeof(BaseType) == sizeof(EUniformBufferBaseType), "Cast failure.");
				static_assert(sizeof(PrecisionModifier) == sizeof(EShaderPrecisionModifier::Type), "Cast failure.");

				TransferFile << Name;
				TransferFile << ShaderType;
				TransferFile << FileLine;
				TransferFile << Offset;
				TransferFile << BaseType;
				TransferFile << PrecisionModifier;
				TransferFile << NumRows;
				TransferFile << NumColumns;
				TransferFile << NumElements;
				TransferFile << StructMetadataIndex;
			}
		}
	}

	// Write individual shader jobs
	{
		int32 SingleJobHeader = ShaderCompileWorkerSingleJobHeader;
		TransferFile << SingleJobHeader;

		int32 NumBatches = QueuedSingleJobs.Num();
		TransferFile << NumBatches;

		// Serialize all the batched jobs
		for (int32 JobIndex = 0; JobIndex < QueuedSingleJobs.Num(); JobIndex++)
		{
			QueuedSingleJobs[JobIndex]->SerializeWorkerInput(TransferFile);
			QueuedSingleJobs[JobIndex]->Input.SerializeSharedInputs(TransferFile, SharedEnvironments, AllShaderParameterStructures);
		}
	}

	// Write shader pipeline jobs
	{
		int32 PipelineJobHeader = ShaderCompileWorkerPipelineJobHeader;
		TransferFile << PipelineJobHeader;

		int32 NumBatches = QueuedPipelineJobs.Num();
		TransferFile << NumBatches;
		for (int32 JobIndex = 0; JobIndex < QueuedPipelineJobs.Num(); JobIndex++)
		{
			auto* PipelineJob = QueuedPipelineJobs[JobIndex];
			FString PipelineName = PipelineJob->Key.ShaderPipeline->GetName();
			TransferFile << PipelineName;
			int32 NumStageJobs = PipelineJob->StageJobs.Num();
			TransferFile << NumStageJobs;
			for (int32 Index = 0; Index < NumStageJobs; Index++)
			{
				PipelineJob->StageJobs[Index]->SerializeWorkerInput(TransferFile);
				PipelineJob->StageJobs[Index]->Input.SerializeSharedInputs(TransferFile, SharedEnvironments, AllShaderParameterStructures);
			}
		}
	}

	if (bCompressTaskFile)
	{
		TransferFile.Close();

		FName CompressionFormatToUse = NAME_LZ4;

		FString FormatName = CompressionFormatToUse.ToString();
		InTransferFile << FormatName;

		// serialize uncompressed data size
		int32 UncompressedDataSize = UncompressedArray.Num();
		checkf(UncompressedDataSize != 0, TEXT("Did not write any data to the task file for the compression."));
		InTransferFile << UncompressedDataSize;

		// not using SerializeCompressed because it splits into smaller chunks
		int32 CompressedSizeBound = FCompression::CompressMemoryBound(CompressionFormatToUse, static_cast<int32>(UncompressedDataSize));
		TArray<uint8> CompressedBuffer;
		CompressedBuffer.SetNumUninitialized(CompressedSizeBound);

		int32 ActualCompressedSize = CompressedSizeBound;
		bool bSucceeded = FCompression::CompressMemory(CompressionFormatToUse, CompressedBuffer.GetData(), ActualCompressedSize, UncompressedArray.GetData(), UncompressedDataSize, COMPRESS_BiasSpeed);
		checkf(ActualCompressedSize <= CompressedSizeBound, TEXT("Compressed size was larger than the bound - we stomped the memory."));
		CompressedBuffer.SetNum(ActualCompressedSize, EAllowShrinking::No);

		InTransferFile << CompressedBuffer;
		UE_LOG(LogShaderCompilers, Verbose, TEXT("Compressed the task file from %d bytes to %d bytes (%.2f%% savings)"), UncompressedDataSize, ActualCompressedSize,
			100.0 * (UncompressedDataSize - ActualCompressedSize) / static_cast<double>(UncompressedDataSize));
	}

	return InTransferFile.Close();
}

const TCHAR* DebugWorkerInputFileName = TEXT("DebugSCW.in");
const TCHAR* DebugWorkerOutputFileName = TEXT("DebugSCW.out");

static FString CreateShaderCompilerWorkerDebugCommandLine(FString DebugWorkerInputFilePath)
{
	// 0 is parent PID, pass zero TTL and KeepInput to make SCW process the single job then exit without deleting the input file
	return FString::Printf(TEXT("\"%s\" 0 \"DebugSCW\" %s %s -TimeToLive=0.0f -KeepInput"),
		*DebugWorkerInputFilePath, // working directory for SCW
		DebugWorkerInputFileName,
		DebugWorkerOutputFileName);
}

void DumpWorkerInputs(TConstArrayView<FShaderCommonCompileJobPtr> QueuedJobs)
{
	if (CVarDebugDumpWorkerInputs.GetValueOnAnyThread())
	{
		for (const FShaderCommonCompileJobPtr& CommonJob : QueuedJobs)
		{
			FShaderPipelineCompileJob* PipelineJob = CommonJob->GetShaderPipelineJob();
			FString DebugWorkerInputFilePath;
			if (PipelineJob)
			{
				// for pipeline jobs, write out the worker input for the whole pipeline, but only for the first stage
				// would be better to put in a parent folder probably...
				DebugWorkerInputFilePath = PipelineJob->StageJobs[0]->Input.DumpDebugInfoPath;
			}
			else
			{
				DebugWorkerInputFilePath = CommonJob->GetSingleShaderJob()->Input.DumpDebugInfoPath;
			}
			if (!DebugWorkerInputFilePath.IsEmpty())
			{
				TArray<FShaderCommonCompileJobPtr> SingleJobArray;
				SingleJobArray.Add(CommonJob);

				FArchive* DebugWorkerInputFileWriter = IFileManager::Get().CreateFileWriter(*(DebugWorkerInputFilePath / DebugWorkerInputFileName), FILEWRITE_NoFail);
				DoWriteTasksInner(
					SingleJobArray,
					*DebugWorkerInputFileWriter,
					nullptr,	// Don't pass a IDistributedBuildController, this is only used for conversion to relative paths which we do not want for debug files
					false,		// As above, use absolute paths not relative
					true);		// Always compress the debug files; they are rather large so this saves some disk space
				DebugWorkerInputFileWriter->Close();
				delete DebugWorkerInputFileWriter;

				FFileHelper::SaveStringToFile(
					CreateShaderCompilerWorkerDebugCommandLine(DebugWorkerInputFilePath),
					*(DebugWorkerInputFilePath / TEXT("DebugCompileArgs.txt")));
			}
		}
	}
}

// Serialize Queued Job information
bool FShaderCompileUtilities::DoWriteTasks(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, FArchive& InTransferFile, IDistributedBuildController* BuildDistributionController, bool bUseRelativePaths, bool bCompressTaskFile)
{
	DumpWorkerInputs(QueuedJobs);

	return DoWriteTasksInner(QueuedJobs, InTransferFile, BuildDistributionController, bUseRelativePaths, bCompressTaskFile);
}

static bool ReadSingleJob(FShaderCompileJob* CurrentJob, FArchive& WorkerOutputFileReader)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ReadSingleJob);

	check(!CurrentJob->bFinalized);
	CurrentJob->bFinalized = true;

	// Deserialize the shader compilation output.
	CurrentJob->SerializeWorkerOutput(WorkerOutputFileReader);

	// The job should already have a non-zero output hash
	checkf(CurrentJob->Output.OutputHash != FSHAHash() || !CurrentJob->bSucceeded, TEXT("OutputHash for a successful job was not set in the shader compile worker!"));


	// Support dumping debug info for only failed compilations or those with warnings
	if (GShaderCompilingManager->ShouldRecompileToDumpShaderDebugInfo(*CurrentJob))
	{
		// Build debug info path and create the directory if it doesn't already exist
		CurrentJob->Input.DumpDebugInfoPath = GShaderCompilingManager->CreateShaderDebugInfoPath(CurrentJob->Input);

		// We failed to compile a shader, we will retry and dump the shader source. so increment the number of shader sources we've dumped so far.
		GShaderCompilingManager->IncrementNumDumpedShaderSources();

		return true;
	}

	return false;
}

static const TCHAR* GetCompileJobSuccessText(FShaderCompileJob* SingleJob)
{
	if (SingleJob)
	{
		return SingleJob->Output.bSucceeded ? TEXT("Succeeded") : TEXT("Failed");
	}
	return TEXT("");
}

void LogQueuedCompileJobs(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, int32 NumProcessedJobs)
{
	if (NumProcessedJobs == -1)
	{
		UE_LOG(LogShaderCompilers, Error, TEXT("SCW %d Queued Jobs, Unknown number of processed jobs!"), QueuedJobs.Num());
	}
	else
	{
		UE_LOG(LogShaderCompilers, Error, TEXT("SCW %d Queued Jobs, Finished %d single jobs"), QueuedJobs.Num(), NumProcessedJobs);
	}

	for (int32 Index = 0; Index < QueuedJobs.Num(); ++Index)
	{
		if (FShaderCompileJob* SingleJob = QueuedJobs[Index]->GetSingleShaderJob())
		{
			UE_LOG(LogShaderCompilers, Error, TEXT("Job %d [Single] %s: %s"), Index, GetCompileJobSuccessText(SingleJob), *GetSingleJobCompilationDump(SingleJob));
		}
		else
		{
			FShaderPipelineCompileJob* PipelineJob = QueuedJobs[Index]->GetShaderPipelineJob();
			UE_LOG(LogShaderCompilers, Error, TEXT("Job %d: Pipeline %s "), Index, PipelineJob->Key.ShaderPipeline->GetName());
			for (int32 JobIndex = 0; JobIndex < PipelineJob->StageJobs.Num(); ++JobIndex)
			{
				FShaderCompileJob* StageJob = PipelineJob->StageJobs[JobIndex]->GetSingleShaderJob();
				UE_LOG(LogShaderCompilers, Error, TEXT("PipelineJob %d %s: %s"), JobIndex, GetCompileJobSuccessText(StageJob), *GetSingleJobCompilationDump(StageJob));
			}
		}
	}

	// Force a log flush so we can track the crash before the cooker potentially crashes before the output shows up
	GLog->Flush();
}

// Disable optimization for this crash handler to get full access to the entire stack frame when debugging a crash dump
UE_DISABLE_OPTIMIZATION_SHIP
static bool HandleWorkerCrash(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, FArchive& OutputFile, int32 OutputVersion, int64 FileSize, FSCWErrorCode::ECode ErrorCode, int32 NumProcessedJobs, int32 CallstackLength, int32 ExceptionInfoLength, int32 HostnameLength)
{
	TArray<TCHAR> Callstack;
	Callstack.AddUninitialized(CallstackLength + 1);
	OutputFile.Serialize(Callstack.GetData(), CallstackLength * sizeof(TCHAR));
	Callstack[CallstackLength] = TEXT('\0');

	TArray<TCHAR> ExceptionInfo;
	ExceptionInfo.AddUninitialized(ExceptionInfoLength + 1);
	OutputFile.Serialize(ExceptionInfo.GetData(), ExceptionInfoLength * sizeof(TCHAR));
	ExceptionInfo[ExceptionInfoLength] = TEXT('\0');

	TArray<TCHAR> Hostname;
	Hostname.AddUninitialized(HostnameLength + 1);
	OutputFile.Serialize(Hostname.GetData(), HostnameLength * sizeof(TCHAR));
	Hostname[HostnameLength] = TEXT('\0');

	// Read available and used physical memory from worker machine on OOM error
	FPlatformMemoryStats MemoryStats;
	if (ErrorCode == FSCWErrorCode::OutOfMemory)
	{
		OutputFile
			<< MemoryStats.AvailablePhysical
			<< MemoryStats.AvailableVirtual
			<< MemoryStats.UsedPhysical
			<< MemoryStats.PeakUsedPhysical
			<< MemoryStats.UsedVirtual
			<< MemoryStats.PeakUsedVirtual
			;
	}

	// Store primary job information onto stack to make it part of a crash dump
	static const int32 MaxNumCharsForSourcePaths = 8192;
	int32 JobInputSourcePathsLength = 0;
	ANSICHAR JobInputSourcePaths[MaxNumCharsForSourcePaths];
	JobInputSourcePaths[0] = 0;

	auto WriteInputSourcePathOntoStack = [&JobInputSourcePathsLength, &JobInputSourcePaths](const ANSICHAR* InputSourcePath)
	{
		if (InputSourcePath != nullptr && JobInputSourcePathsLength + 3 < MaxNumCharsForSourcePaths)
		{
			// Copy input source path into stack buffer
			int32 InputSourcePathLength = FMath::Min(FCStringAnsi::Strlen(InputSourcePath), (MaxNumCharsForSourcePaths - JobInputSourcePathsLength - 2));
			FMemory::Memcpy(JobInputSourcePaths + JobInputSourcePathsLength, InputSourcePath, InputSourcePathLength);

			// Write newline character and put NUL character at the end
			JobInputSourcePathsLength += InputSourcePathLength;
			JobInputSourcePaths[JobInputSourcePathsLength] = TEXT('\n');
			++JobInputSourcePathsLength;
			JobInputSourcePaths[JobInputSourcePathsLength] = 0;
		}
	};

	auto StoreInputDebugInfo = [&WriteInputSourcePathOntoStack, &JobInputSourcePathsLength, &JobInputSourcePaths](const FShaderCompilerInput& Input)
	{
		FString DebugInfo = FString::Printf(TEXT("%s:%s"), *Input.VirtualSourceFilePath, *Input.EntryPointName);
		WriteInputSourcePathOntoStack(TCHAR_TO_UTF8(*DebugInfo));
	};

	for (auto CommonJob : QueuedJobs)
	{
		if (FShaderCompileJob* SingleJob = CommonJob->GetSingleShaderJob())
		{
			StoreInputDebugInfo(SingleJob->Input);
		}
		else if (FShaderPipelineCompileJob* PipelineJob = CommonJob->GetShaderPipelineJob())
		{
			for (int32 Job = 0; Job < PipelineJob->StageJobs.Num(); ++Job)
			{
				if (FShaderCompileJob* SingleStageJob = PipelineJob->StageJobs[Job])
				{
					StoreInputDebugInfo(SingleStageJob->Input);
				}
			}
		}
	}

	// One entry per error code as we want to have different callstacks for crash reporter...
	switch (ErrorCode)
	{
	default:
	case FSCWErrorCode::GeneralCrash:
		LogQueuedCompileJobs(QueuedJobs, NumProcessedJobs);
		ShaderCompileWorkerError::HandleGeneralCrash(ExceptionInfo.GetData(), Callstack.GetData());
		break;
	case FSCWErrorCode::BadShaderFormatVersion:
		ShaderCompileWorkerError::HandleBadShaderFormatVersion(ExceptionInfo.GetData());
		break;
	case FSCWErrorCode::BadInputVersion:
		ShaderCompileWorkerError::HandleBadInputVersion(ExceptionInfo.GetData());
		break;
	case FSCWErrorCode::BadSingleJobHeader:
		ShaderCompileWorkerError::HandleBadSingleJobHeader(ExceptionInfo.GetData());
		break;
	case FSCWErrorCode::BadPipelineJobHeader:
		ShaderCompileWorkerError::HandleBadPipelineJobHeader(ExceptionInfo.GetData());
		break;
	case FSCWErrorCode::CantDeleteInputFile:
		ShaderCompileWorkerError::HandleCantDeleteInputFile(ExceptionInfo.GetData());
		break;
	case FSCWErrorCode::CantSaveOutputFile:
		ShaderCompileWorkerError::HandleCantSaveOutputFile(ExceptionInfo.GetData());
		break;
	case FSCWErrorCode::NoTargetShaderFormatsFound:
		ShaderCompileWorkerError::HandleNoTargetShaderFormatsFound(ExceptionInfo.GetData());
		break;
	case FSCWErrorCode::CantCompileForSpecificFormat:
		ShaderCompileWorkerError::HandleCantCompileForSpecificFormat(ExceptionInfo.GetData());
		break;
	case FSCWErrorCode::CrashInsidePlatformCompiler:
		LogQueuedCompileJobs(QueuedJobs, NumProcessedJobs);
		ShaderCompileWorkerError::HandleCrashInsidePlatformCompiler(ExceptionInfo.GetData());
		break;
	case FSCWErrorCode::BadInputFile:
		ShaderCompileWorkerError::HandleBadInputFile(ExceptionInfo.GetData());
		break;
	case FSCWErrorCode::OutOfMemory:
		return ShaderCompileWorkerError::HandleOutOfMemory(ExceptionInfo.GetData(), Hostname.GetData(), MemoryStats, QueuedJobs);
	case FSCWErrorCode::Success:
		// Can't get here...
		return true;
	}
	return false;
}
UE_ENABLE_OPTIMIZATION_SHIP

// Helper struct to provide consistent error report with detailed information about corrupted ShaderCompileWorker output file.
struct FSCWOutputFileContext
{
	FArchive& OutputFile;
	int64 FileSize = 0;

	FSCWOutputFileContext(FArchive& OutputFile) :
		OutputFile(OutputFile)
	{
	}

	template <typename FmtType, typename... Types>
	void ModalErrorOrLog(const FmtType& Format, Types&&... Args)
	{
		FString Text = FString::Printf(Format, Args...);
		Text = FString::Printf(TEXT("File path: \"%s\"\n%s\nForgot to build ShaderCompileWorker or delete invalidated DerivedDataCache?"), *OutputFile.GetArchiveName(), *Text);
		const TCHAR* Title = TEXT("Corrupted ShaderCompileWorker output file");
		if (FileSize > 0)
		{
			::ModalErrorOrLog(Title, Text, OutputFile.Tell(), FileSize);
		}
		else
		{
			::ModalErrorOrLog(Title, Text, 0, 0);
		}
	}
};

// Process results from Worker Process.
// Returns false if reading the tasks failed but we were able to recover from handing a crash report. In this case, all jobs must be submitted/processed again.
FSCWErrorCode::ECode FShaderCompileUtilities::DoReadTaskResults(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, FArchive& OutputFile)
{
	FSCWOutputFileContext OutputFileContext(OutputFile);

	if (OutputFile.TotalSize() == 0)
	{
		ShaderCompileWorkerError::HandleOutputFileEmpty(*OutputFile.GetArchiveName());
	}

	int32 OutputVersion = ShaderCompileWorkerOutputVersion;
	OutputFile << OutputVersion;

	if (ShaderCompileWorkerOutputVersion != OutputVersion)
	{
		OutputFileContext.ModalErrorOrLog(TEXT("Expecting output version %d, got %d instead!"), ShaderCompileWorkerOutputVersion, OutputVersion);
	}

	OutputFile << OutputFileContext.FileSize;

	// Check for corrupted output file
	if (OutputFileContext.FileSize > OutputFile.TotalSize())
	{
		ShaderCompileWorkerError::HandleOutputFileCorrupted(*OutputFile.GetArchiveName(), OutputFileContext.FileSize, OutputFile.TotalSize());
	}

	int32 ErrorCode = 0;
	OutputFile << ErrorCode;

	int32 NumProcessedJobs = 0;
	OutputFile << NumProcessedJobs;

	int32 CallstackLength = 0;
	OutputFile << CallstackLength;

	int32 ExceptionInfoLength = 0;
	OutputFile << ExceptionInfoLength;

	int32 HostnameLength = 0;
	OutputFile << HostnameLength;

	if (ErrorCode != FSCWErrorCode::Success)
	{
		// If worker crashed in a way we were able to recover from, return and expect the compile jobs to be reissued already
		if (HandleWorkerCrash(QueuedJobs, OutputFile, OutputVersion, OutputFileContext.FileSize, (FSCWErrorCode::ECode)ErrorCode, NumProcessedJobs, CallstackLength, ExceptionInfoLength, HostnameLength))
		{
			FSCWErrorCode::Reset();
			return (FSCWErrorCode::ECode)ErrorCode;
		}
	}

	TArray<FShaderCompileJob*> QueuedSingleJobs;
	TArray<FShaderPipelineCompileJob*> QueuedPipelineJobs;
	SplitJobsByType(QueuedJobs, QueuedSingleJobs, QueuedPipelineJobs);
	TArray<FShaderCommonCompileJob*> ReissueSourceJobs;

	// Read single jobs
	{
		int32 SingleJobHeader = -1;
		OutputFile << SingleJobHeader;
		if (SingleJobHeader != ShaderCompileWorkerSingleJobHeader)
		{
			OutputFileContext.ModalErrorOrLog(TEXT("Expecting single job header ID 0x%08X, got 0x%08X instead!"), ShaderCompileWorkerSingleJobHeader, SingleJobHeader);
		}

		int32 NumJobs;
		OutputFile << NumJobs;
		if (NumJobs != QueuedSingleJobs.Num())
		{
			OutputFileContext.ModalErrorOrLog(TEXT("Expecting %d single %s, got %d instead!"), QueuedSingleJobs.Num(), (QueuedSingleJobs.Num() == 1 ? TEXT("job") : TEXT("jobs")), NumJobs);
		}
		else
		{
			for (int32 JobIndex = 0; JobIndex < NumJobs; JobIndex++)
			{
				auto* CurrentJob = QueuedSingleJobs[JobIndex];
				if (ReadSingleJob(CurrentJob, OutputFile))
				{
					ReissueSourceJobs.Add(CurrentJob);
				}
			}
		}
	}

	// Pipeline jobs
	{
		int32 PipelineJobHeader = -1;
		OutputFile << PipelineJobHeader;
		if (PipelineJobHeader != ShaderCompileWorkerPipelineJobHeader)
		{
			OutputFileContext.ModalErrorOrLog(TEXT("Expecting pipeline jobs header ID 0x%08X, got 0x%08X instead!"), ShaderCompileWorkerPipelineJobHeader, PipelineJobHeader);
		}

		int32 NumJobs;
		OutputFile << NumJobs;
		if (NumJobs != QueuedPipelineJobs.Num())
		{
			OutputFileContext.ModalErrorOrLog(TEXT("Expecting %d pipeline %s, got %d instead!"), QueuedPipelineJobs.Num(), (QueuedPipelineJobs.Num() == 1 ? TEXT("job") : TEXT("jobs")), NumJobs);
		}
		else
		{
			for (int32 JobIndex = 0; JobIndex < NumJobs; JobIndex++)
			{
				FShaderPipelineCompileJob* CurrentJob = QueuedPipelineJobs[JobIndex];

				FString PipelineName;
				OutputFile << PipelineName;
				bool bSucceeded = false;
				OutputFile << bSucceeded;
				CurrentJob->bSucceeded = bSucceeded;
				if (PipelineName != CurrentJob->Key.ShaderPipeline->GetName())
				{
					OutputFileContext.ModalErrorOrLog(TEXT("Expecting pipeline job \"%s\", got \"%s\" instead!"), CurrentJob->Key.ShaderPipeline->GetName(), *PipelineName);
				}

				check(!CurrentJob->bFinalized);
				CurrentJob->bFinalized = true;

				int32 NumStageJobs = -1;
				OutputFile << NumStageJobs;

				if (NumStageJobs != CurrentJob->StageJobs.Num())
				{
					OutputFileContext.ModalErrorOrLog(TEXT("Expecting %d stage pipeline %s, got %d instead!"), CurrentJob->StageJobs.Num(), (CurrentJob->StageJobs.Num() == 1 ? TEXT("job") : TEXT("jobs")), NumStageJobs);
				}
				else
				{
					for (int32 Index = 0; Index < NumStageJobs; Index++)
					{
						FShaderCompileJob* SingleJob = CurrentJob->StageJobs[Index];
						// cannot reissue a single stage of a pipeline job
						ReadSingleJob(SingleJob, OutputFile);
					}
				}
			}
		}
	}
	
	// Requeue any jobs we wish to run again
	ReissueShaderCompileJobs(ReissueSourceJobs);

	return FSCWErrorCode::Success;
}

void FShaderCompileUtilities::ExecuteShaderCompileJob(FShaderCommonCompileJob& Job)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderCompileUtilities::ExecuteShaderCompileJob);

	check(!Job.bFinalized);

	FString WorkingDir = FPlatformProcess::ShaderDir();
	static ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	auto* SingleJob = Job.GetSingleShaderJob();
	const TArray<const IShaderFormat*> ShaderFormats = TPM.GetShaderFormats();
	if (SingleJob)
	{
		const FName Format = (SingleJob->Input.ShaderFormat != NAME_None) ? SingleJob->Input.ShaderFormat : LegacyShaderPlatformToShaderFormat(EShaderPlatform(SingleJob->Input.Target.Platform));
		CompileShader(ShaderFormats, *SingleJob, WorkingDir);
	}
	else
	{
		FShaderPipelineCompileJob* PipelineJob = Job.GetShaderPipelineJob();
		check(PipelineJob);

		EShaderPlatform Platform = (EShaderPlatform)PipelineJob->StageJobs[0]->Input.Target.Platform;
		const FName Format = LegacyShaderPlatformToShaderFormat(Platform);

		// Verify same platform on all stages
		for (int32 Index = 1; Index < PipelineJob->StageJobs.Num(); ++Index)
		{
			auto SingleStage = PipelineJob->StageJobs[Index];
			if (!SingleStage)
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Can't nest Shader Pipelines inside Shader Pipeline '%s'!"), PipelineJob->Key.ShaderPipeline->GetName());
			}
			else if (Platform != SingleStage->Input.Target.Platform)
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Mismatched Target Platform %s while compiling Shader Pipeline '%s'."), *Format.GetPlainNameString(), PipelineJob->Key.ShaderPipeline->GetName());
			}
		}

		CompileShaderPipeline(ShaderFormats, PipelineJob, WorkingDir);
	}

	Job.bFinalized = true;
}

FArchive* FShaderCompileUtilities::CreateFileHelper(const FString& Filename)
{
	// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
	// We can't avoid code duplication unless we refactored the local worker too.

	FArchive* File = nullptr;
	int32 RetryCount = 0;
	// Retry over the next two seconds if we can't write out the file.
	// Anti-virus and indexing applications can interfere and cause this to fail.
	while (File == nullptr && RetryCount < 200)
	{
		if (RetryCount > 0)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		File = IFileManager::Get().CreateFileWriter(*Filename, FILEWRITE_EvenIfReadOnly);
		RetryCount++;
	}
	if (File == nullptr)
	{
		File = IFileManager::Get().CreateFileWriter(*Filename, FILEWRITE_EvenIfReadOnly | FILEWRITE_NoFail);
	}
	checkf(File, TEXT("Failed to create file %s!"), *Filename);
	return File;
}

void FShaderCompileUtilities::MoveFileHelper(const FString& To, const FString& From)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (PlatformFile.FileExists(*From))
	{
		FString DirectoryName;
		int32 LastSlashIndex;
		if (To.FindLastChar('/', LastSlashIndex))
		{
			DirectoryName = To.Left(LastSlashIndex);
		} else
		{
			DirectoryName = To;
		}

		// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
		// We can't avoid code duplication unless we refactored the local worker too.

		bool Success = false;
		int32 RetryCount = 0;
		// Retry over the next two seconds if we can't move the file.
		// Anti-virus and indexing applications can interfere and cause this to fail.
		while (!Success && RetryCount < 200)
		{
			if (RetryCount > 0)
			{
				FPlatformProcess::Sleep(0.01f);
			}

			// MoveFile does not create the directory tree, so try to do that now...
			Success = PlatformFile.CreateDirectoryTree(*DirectoryName);
			if (Success)
			{
				Success = PlatformFile.MoveFile(*To, *From);
			}
			RetryCount++;
		}
		checkf(Success, TEXT("Failed to move file %s to %s!"), *From, *To);
	}
}

void FShaderCompileUtilities::DeleteFileHelper(const FString& Filename)
{
	// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
	// We can't avoid code duplication unless we refactored the local worker too.

	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*Filename))
	{
		bool bDeletedOutput = IFileManager::Get().Delete(*Filename, true, true);

		// Retry over the next two seconds if we couldn't delete it
		int32 RetryCount = 0;
		while (!bDeletedOutput && RetryCount < 200)
		{
			FPlatformProcess::Sleep(0.01f);
			bDeletedOutput = IFileManager::Get().Delete(*Filename, true, true);
			RetryCount++;
		}
		checkf(bDeletedOutput, TEXT("Failed to delete %s!"), *Filename);
	}
}


