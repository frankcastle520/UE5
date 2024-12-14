// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/PlatformProcess.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/ModuleDiagnostics.h"
#include "ProfilingDebugging/MemoryTrace.h"
#include "ProfilingDebugging/MetadataTrace.h"

#if !UE_BUILD_SHIPPING
#include "Apple/PreAppleSystemHeaders.h"
#include <mach/mach.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include "Apple/PostAppleSystemHeaders.h"
#endif

////////////////////////////////////////////////////////////////////////////////
void Modules_Initialize()
{
#if !UE_BUILD_SHIPPING
	using namespace UE::Trace;

	constexpr uint32 SizeOfSymbolFormatString = 4;
	UE_TRACE_LOG(Diagnostics, ModuleInit, ModuleChannel, sizeof(ANSICHAR) * SizeOfSymbolFormatString)
		<< ModuleInit.SymbolFormat("psym", SizeOfSymbolFormatString)
		<< ModuleInit.ModuleBaseShift(uint8(0));
    
    struct task_dyld_info dyld_info;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_DYLD_INFO, (task_info_t)&dyld_info, &count) == KERN_SUCCESS)
    {
        HeapId ProgramHeapId = MemoryTrace_HeapSpec(EMemoryTraceRootHeap::SystemMemory, TEXT("Program"), EMemoryTraceHeapFlags::NeverFrees);
        FString ExecutablePath = FPaths::GetPath(FString([[NSBundle mainBundle] executablePath]));
        
        struct dyld_all_image_infos* infos = (struct dyld_all_image_infos*)dyld_info.all_image_info_addr;
        // FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Found %d modules"), infos->infoArrayCount);

        // Send Mach-O's uuid as the BuildId. Psym generation seems to have an extra 0 but we will ignore it on the other end.
        constexpr uint32 BuildIdSize = 16;
        uint8 BuildId[BuildIdSize] = {0};

        for (int i=0; i<infos->infoArrayCount; i++)
        {
            const struct dyld_image_info* Image = &infos->infoArray[i];
            // FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Found module %s"), ANSI_TO_TCHAR(Image->imageFilePath));

            // calc image size by adding header and size of segments
            struct mach_header_64* Header = (struct mach_header_64*)Image->imageLoadAddress;
            size_t ImageSize = sizeof(*Header) + Header->sizeofcmds;
            char* CmdPtr = (char*)(Header + 1);
            for (int cmd=0; cmd < Header->ncmds; cmd++)
            {
                struct load_command* LoadCommand = (struct load_command*)CmdPtr;
                if (LoadCommand->cmd == LC_SEGMENT_64)
                {
                    ImageSize += ((struct segment_command_64*)LoadCommand)->vmsize;
                }
                else if (LoadCommand->cmd == LC_UUID)
                {
                    FMemory::Memcpy(BuildId, ((struct uuid_command*)LoadCommand)->uuid, BuildIdSize);
                }
                CmdPtr = CmdPtr + LoadCommand->cmdsize;
            }
                        
            FString ImageName(Image->imageFilePath);
            bool bInsideExecutablePath = ImageName.StartsWith(ExecutablePath);
            
            // trim path to leave just image name
            ImageName = FPaths::GetCleanFilename(ImageName);
            uint64 ImageLoadAddress = (uint64)Image->imageLoadAddress;
            
            UE_TRACE_LOG(Diagnostics, ModuleLoad, ModuleChannel, sizeof(TCHAR) * ImageName.Len() + BuildIdSize)
                << ModuleLoad.Name(*ImageName, ImageName.Len())
                << ModuleLoad.Base(ImageLoadAddress)
                << ModuleLoad.Size(ImageSize)
                << ModuleLoad.ImageId(BuildId, BuildIdSize);
                       
#if UE_MEMORY_TRACE_ENABLED
            // Only count the main executable and any other libraries inside our bundle as "Program Size"
            if (bInsideExecutablePath)
            {
                UE_TRACE_METADATA_CLEAR_SCOPE();
                LLM(UE_MEMSCOPE(ELLMTag::ProgramSize));
                MemoryTrace_Alloc(ImageLoadAddress, ImageSize, 1);
                MemoryTrace_MarkAllocAsHeap(ImageLoadAddress, ProgramHeapId);
                MemoryTrace_Alloc(ImageLoadAddress, ImageSize, 1);
            }
#endif // UE_MEMORY_TRACE_ENABLED

        }
    }
#endif
}
