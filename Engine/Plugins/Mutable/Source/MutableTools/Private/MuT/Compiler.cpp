// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/Compiler.h"

#include "MuT/AST.h"
#include "MuT/ASTOpParameter.h"
#include "MuT/CodeGenerator.h"
#include "MuT/CodeOptimiser.h"
#include "MuT/CompilerPrivate.h"
#include "MuT/ErrorLog.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/Node.h"
#include "MuT/NodePrivate.h"
#include "MuT/Table.h"
#include "MuR/Image.h"
#include "MuR/Mesh.h"
#include "MuR/Model.h"
#include "MuR/ModelPrivate.h"
#include "MuR/MutableTrace.h"
#include "MuR/Operations.h"
#include "MuR/ParametersPrivate.h"
#include "MuR/Platform.h"
#include "MuR/Serialisation.h"
#include "MuR/MutableRuntimeModule.h"
#include "MuR/System.h"

#include "Trace/Detail/Channel.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/PlatformCrt.h"
#include "Hash/CityHash.h"
#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/AssertionMacros.h"
#include "Async/ParallelFor.h"

#include <inttypes.h>	// Required for 64-bit printf macros


namespace mu
{

    const char* CompilerOptions::GetTextureLayoutStrategyName( CompilerOptions::TextureLayoutStrategy s )
    {
        static const char* s_textureLayoutStrategyName[ 2 ] =
        {
            "Unscaled Pack",
            "No Packing",
        };

        return s_textureLayoutStrategyName[(int)s];
    }


	//---------------------------------------------------------------------------------------------
	FProxyFileContext::FProxyFileContext()
	{
		uint32 Seed = FPlatformTime::Cycles();
		FRandomStream RandomStream = FRandomStream((int32)Seed);
		CurrentFileIndex = RandomStream.GetUnsignedInt();
	}


    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    CompilerOptions::CompilerOptions()
    {
        m_pD = new Private();
    }


    //---------------------------------------------------------------------------------------------
    CompilerOptions::~CompilerOptions()
    {
        check( m_pD );
        delete m_pD;
        m_pD = 0;
    }


    //---------------------------------------------------------------------------------------------
    CompilerOptions::Private* CompilerOptions::GetPrivate() const
    {
        return m_pD;
    }


    //---------------------------------------------------------------------------------------------
    void CompilerOptions::SetLogEnabled( bool bEnabled)
    {
        m_pD->bLog = bEnabled;
    }


    //---------------------------------------------------------------------------------------------
    void CompilerOptions::SetOptimisationEnabled( bool bEnabled)
    {
        m_pD->OptimisationOptions.bEnabled = bEnabled;
        if (bEnabled)
        {
            m_pD->OptimisationOptions.bConstReduction = true;
        }
    }


    //---------------------------------------------------------------------------------------------
    void CompilerOptions::SetConstReductionEnabled( bool bConstReductionEnabled )
    {
        m_pD->OptimisationOptions.bConstReduction = bConstReductionEnabled;
    }


	//---------------------------------------------------------------------------------------------
	void CompilerOptions::SetUseDiskCache(bool bEnabled)
	{
		m_pD->OptimisationOptions.DiskCacheContext = bEnabled ? &m_pD->DiskCacheContext : nullptr;
	}


	//---------------------------------------------------------------------------------------------
	void CompilerOptions::SetUseConcurrency(bool bEnabled)
	{
		m_pD->bUseConcurrency = bEnabled;
	}


    //---------------------------------------------------------------------------------------------
    void CompilerOptions::SetOptimisationMaxIteration( int32 maxIterations )
    {
        m_pD->OptimisationOptions.MaxOptimisationLoopCount = maxIterations;
    }


    //---------------------------------------------------------------------------------------------
    void CompilerOptions::SetIgnoreStates( bool bIgnore )
    {
        m_pD->bIgnoreStates = bIgnore;
    }


	//---------------------------------------------------------------------------------------------
	void CompilerOptions::SetImageCompressionQuality(int32 Quality)
	{
		m_pD->ImageCompressionQuality = Quality;
	}


	//---------------------------------------------------------------------------------------------
	void CompilerOptions::SetImageTiling(int32 Tiling)
	{
		m_pD->ImageTiling = Tiling;
	}


	//---------------------------------------------------------------------------------------------
	void CompilerOptions::SetDataPackingStrategy(int32 MinTextureResidentMipCount, uint64 EmbeddedDataBytesLimit, uint64 PackagedDataBytesLimit)
	{
		m_pD->EmbeddedDataBytesLimit = EmbeddedDataBytesLimit;
		m_pD->PackagedDataBytesLimit = PackagedDataBytesLimit;
		m_pD->MinTextureResidentMipCount = MinTextureResidentMipCount;
	}


	//---------------------------------------------------------------------------------------------
	void CompilerOptions::SetEnableProgressiveImages(bool bEnabled)
	{
		m_pD->OptimisationOptions.bEnableProgressiveImages = bEnabled;
	}


	//---------------------------------------------------------------------------------------------
	void CompilerOptions::SetImagePixelFormatOverride(const FImageOperator::FImagePixelFormatFunc& InFunc)
	{
		m_pD->ImageFormatFunc = InFunc;
	}


	//---------------------------------------------------------------------------------------------
	void CompilerOptions::SetReferencedResourceCallback(const FReferencedResourceFunc& Provider)
	{
		m_pD->OptimisationOptions.ReferencedResourceProvider = Provider;
	}


	void CompilerOptions::LogStats() const
	{
		UE_LOG(LogMutableCore, Log, TEXT("   Cache Files Written : %" PRIu64), m_pD->DiskCacheContext.FilesWritten.load());
		UE_LOG(LogMutableCore, Log, TEXT("   Cache Files Read    : %" PRIu64), m_pD->DiskCacheContext.FilesRead.load());
		UE_LOG(LogMutableCore, Log, TEXT("   Cache MB Written    : %" PRIu64), m_pD->DiskCacheContext.BytesWritten.load() >> 20);
		UE_LOG(LogMutableCore, Log, TEXT("   Cache MB Read       : %" PRIu64), m_pD->DiskCacheContext.BytesRead.load()>>20);
	}
	
	
	//---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    Compiler::Compiler( Ptr<CompilerOptions> options )
    {
        m_pD = new Private();
        m_pD->m_options = options;
        if (!m_pD->m_options)
        {
            m_pD->m_options = new CompilerOptions;
        }
    }


    //---------------------------------------------------------------------------------------------
    Compiler::~Compiler()
    {
        check( m_pD );
        delete m_pD;
        m_pD = 0;
    }


    //---------------------------------------------------------------------------------------------
	TSharedPtr<Model> Compiler::Compile( const Ptr<Node>& pNode )
    {
        MUTABLE_CPUPROFILER_SCOPE(Compile);

        TArray< FStateCompilationData > states;
        Ptr<ErrorLog> genErrorLog;
		TArray<FParameterDesc> Parameters;
        {
            CodeGenerator gen( m_pD->m_options->GetPrivate() );

            gen.GenerateRoot( pNode );

            check( !gen.States.IsEmpty() );

            for ( const TPair<FObjectState, Ptr<ASTOp>>& s: gen.States )
            {
                FStateCompilationData data;
                data.nodeState = s.Key;
                data.root = s.Value;
                data.state.Name = s.Key.Name;
                states.Add( data );
            }

            genErrorLog = gen.ErrorLog;

			// Set the parameter list from the non-optimized data, so that we have them all even if they are optimized out
			int32 ParameterCount = gen.FirstPass.ParameterNodes.Num();
			Parameters.SetNum(ParameterCount);
			int32 ParameterIndex = 0;
			for ( const TPair<Ptr<const Node>, Ptr<ASTOpParameter>>& Entry : gen.FirstPass.ParameterNodes )
			{
				Parameters[ParameterIndex] = Entry.Value->parameter;
				++ParameterIndex;
			}

			// Sort the parameters as deterministically as possible.
			struct FParameterSortPredicate
			{
				bool operator()(const FParameterDesc& A, const FParameterDesc& B) const
				{
					if (A.m_name < B.m_name) return true;
					if (A.m_name > B.m_name) return false;
					return A.m_uid < B.m_uid;
				}
			};
			
			FParameterSortPredicate SortPredicate;
			Parameters.Sort(SortPredicate);
		}


        // Slow AST code verification for debugging.
        //TArray<Ptr<ASTOp>> roots;
        //for( const FStateCompilationData& s: states)
        //{
        //    roots.Add(s.root);
        //}
        //ASTOp::FullAssert(roots);

        // Optimize the generated code
        {
            CodeOptimiser optimiser( m_pD->m_options, states );
            optimiser.OptimiseAST( );
        }

        // Link the Program and generate state data.
		TSharedPtr<Model> pResult = MakeShared<Model>();
        FProgram& Program = pResult->GetPrivate()->m_program;

		check(Program.m_parameters.IsEmpty());
		Program.m_parameters = Parameters;

		// Preallocate ample memory
		Program.m_byteCode.Reserve(16 * 1024 * 1024);
		Program.m_opAddress.Reserve(1024 * 1024);

		// Keep the link options outside the scope because it is also used to cache constant data that has already been 
		// added and could be reused across states.
		FImageOperator ImOp = FImageOperator::GetDefault(m_pD->m_options->GetPrivate()->ImageFormatFunc);
		FLinkerOptions LinkerOptions(ImOp);

		for(FStateCompilationData& s: states )
        {
			LinkerOptions.MinTextureResidentMipCount = m_pD->m_options->GetPrivate()->MinTextureResidentMipCount;

            if (s.root)
            {
				s.state.m_root = ASTOp::FullLink(s.root, Program, &LinkerOptions);
            }
            else
            {
                s.state.m_root = 0;
            }
        }

		Program.m_byteCode.Shrink();
		Program.m_opAddress.Shrink();

        // Set the runtime parameter indices.
        for(FStateCompilationData& s: states )
        {
            for ( int32 p=0; p<s.nodeState.RuntimeParams.Num(); ++p )
            {
                int32 paramIndex = -1;
                for ( int32 i=0; paramIndex<0 && i<Program.m_parameters.Num(); ++i )
                {
                    if ( Program.m_parameters[i].m_name
                         ==
                         s.nodeState.RuntimeParams[p] )
                    {
                        paramIndex = (int)i;
                    }
                }

                if (paramIndex>=0)
                {
                    s.state.m_runtimeParameters.Add( paramIndex );
                }
                else
                {
					FString Temp = FString::Printf(TEXT(
						"The state [%s] refers to a parameter [%s]  that has not been found in the model. This error can be "
						"safely dismissed in case of partial compilation."), 
						*s.nodeState.Name,
						*s.nodeState.RuntimeParams[p]);
                    m_pD->ErrorLog->GetPrivate()->Add(Temp, ELMT_WARNING, pNode->GetMessageContext() );
                }
            }

            // Generate the mask of update cache ops
            for ( const auto& a: s.m_updateCache )
            {
                s.state.m_updateCache.Emplace(a->linkedAddress);
            }

            // Sort the update cache addresses for performance and determinism
            s.state.m_updateCache.Sort();

            // Generate the mask of dynamic resources
            for ( const auto& a: s.m_dynamicResources )
            {
                uint64 relevantMask = 0;
                for ( const auto& b: a.Value )
                {
                    // Find the index in the model parameter list
                    int paramIndex = -1;
                    for ( int32 i=0; paramIndex<0 && i<Program.m_parameters.Num(); ++i )
                    {
                        if ( Program.m_parameters[i].m_name == b )
                        {
                            paramIndex = (int)i;
                        }
                    }
                    check(paramIndex>=0);

                    // Find the position in the state data vector.
                    int32 IndexInRuntimeList = s.state.m_runtimeParameters.Find( paramIndex );

                    if (IndexInRuntimeList !=INDEX_NONE )
                    {
                        relevantMask |= uint64(1) << IndexInRuntimeList;
                    }
                }

                // TODO: this shouldn't happen but it seems to happen. Investigate.
                // Maybe something with the difference of precision between the relevant parameters
                // in operation subtrees.
                //check(relevantMask!=0);
                if (relevantMask!=0)
                {
                    s.state.m_dynamicResources.Emplace( a.Key->linkedAddress, relevantMask );
                }
            }            

            // Sort for performance and determinism
            s.state.m_dynamicResources.Sort();

            Program.m_states.Add(s.state);
        }

		UE_LOG(LogMutableCore, Verbose, TEXT("(int) %s : %ld"), TEXT("Program size"), int64(Program.m_opAddress.Num()));

        // Merge the log in the right order
        genErrorLog->Merge( m_pD->ErrorLog.get() );
        m_pD->ErrorLog = genErrorLog.get();

		// Pack data
		m_pD->GenerateRoms(pResult.Get(), m_pD->m_options.get(), LinkerOptions.AdditionalData );

		UE_LOG(LogMutableCore, Verbose, TEXT("(int) %s : %ld"), TEXT("Program size"), int64(Program.m_opAddress.Num()));

        return pResult;
    }


    //---------------------------------------------------------------------------------------------
    ErrorLogPtrConst Compiler::GetLog() const
    {
        return m_pD->ErrorLog;
    }


	//---------------------------------------------------------------------------------------------
	namespace
	{
		inline void EnsureUniqueRomId(TSet<uint32>& UsedIds, uint32& RomId)
		{
			while (true)
			{
				bool bAlreadyUsed = false;
				UsedIds.FindOrAdd( RomId, &bAlreadyUsed);

				if (!bAlreadyUsed)
				{
					break;
				}

				// Generate a new Id. It is not going to be stable accross build, which means it may hurt content patching
				// a little bit, but it shouldn't happen often.
				RomId++;
			}
		}
	}


	//---------------------------------------------------------------------------------------------
	void Compiler::Private::GenerateRoms(Model* p, const CompilerOptions* Options, const FLinkerOptions::FAdditionalData& AdditionalData )
	{
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
		MUTABLE_CPUPROFILER_SCOPE(GenerateRoms);

		uint64 EmbeddedDataBytesLimit = Options->GetPrivate()->EmbeddedDataBytesLimit;

		mu::FProgram& Program = p->GetPrivate()->m_program;

		// These are used for logging only.
		int32 NumRoms = 0;
		uint64 NumRomsBytes = 0;
		int32 NumEmbedded = 0;
		uint64 NumEmbeddedBytes = 0;
		int32 NumHighRes = 0;
		uint64 NumHighResBytes = 0;

		// Maximum number of roms
		int32 MaxRomCount = Program.ConstantImageLODs.Num() + Program.ConstantMeshes.Num();
		Program.m_roms.Reserve(MaxRomCount);

		TSet<uint32> UsedIds;
		UsedIds.Reserve(MaxRomCount);

		TArray<FRomData> RomDatas;
		RomDatas.SetNumZeroed(FMath::Max(Program.ConstantImageLODs.Num(), Program.ConstantMeshes.Num()));

		// Images
		{
			MUTABLE_CPUPROFILER_SCOPE(GenerateRoms_ImageIds);

			ParallelFor(Program.ConstantImageLODs.Num(),
				[EmbeddedDataBytesLimit, &Program, &RomDatas](uint32 ResourceIndex)
				{
					TPair<int32, Ptr<const Image>>& ResData = Program.ConstantImageLODs[ResourceIndex];

					// This shouldn't have been serialised with rom support before.
					check(ResData.Key < 0);

					// Serialize to find out final size of this rom
					OutputSizeStream SizeStream;
					OutputArchive MemoryArch(&SizeStream);
					Image::Serialise(ResData.Value.get(), MemoryArch);

					// If the resource uses less memory than the threshold, don't save it in a separate rom.
					if (SizeStream.GetBufferSize() <= EmbeddedDataBytesLimit)
					{
						return;
					}

					FRomData& RomData = RomDatas[ResourceIndex];
					RomData.ResourceType = DT_IMAGE;
					RomData.ResourceIndex = ResourceIndex;
					RomData.Size = SizeStream.GetBufferSize();
					RomData.Flags = ERomFlags::None;

					const Image* Resource = ResData.Value.get();
					uint64 FullHash = CityHash64(reinterpret_cast<const char*>(Resource->GetLODData(0)), Resource->GetLODDataSize(0));
					RomData.Id = GetTypeHash(FullHash);
				});
		}

		// Generate the high-res flags for images
		{
			// Initially all are high-res because if at least one reference to a mip is not we will set it to not-high-res
			TArray<bool> IsLODHighRes;
			IsLODHighRes.Init(true, Program.ConstantImageLODs.Num());

			for (int32 ImageIndex = 0; ImageIndex < Program.ConstantImages.Num(); ++ImageIndex)
			{
				const FImageLODRange& LODRange = Program.ConstantImages[ImageIndex];

				int32 NumHighResMips = FMath::Max(0,AdditionalData.SourceImagePerConstant[ImageIndex].SourceHighResMips);

				for (int32 LODRangeIndex = NumHighResMips; LODRangeIndex < LODRange.LODCount; ++LODRangeIndex)
				{
					int32 LODIndex = Program.ConstantImageLODIndices[LODRange.FirstIndex+LODRangeIndex];
					IsLODHighRes[LODIndex] = false;
				}

				// Moreover, at least one mip of each image has to be non-highres
				if (LODRange.LODCount>0)
				{
					int32 LastLODIndex = Program.ConstantImageLODIndices[LODRange.FirstIndex + LODRange.LODCount - 1];
					IsLODHighRes[LastLODIndex] = false;
				}
			}

			for (int32 ResourceIndex = 0; ResourceIndex < Program.ConstantImageLODs.Num(); ++ResourceIndex)
			{
				// If this mip represents a high-quality mip, flag the rom as such
				if (IsLODHighRes[ResourceIndex])
				{
					FRomData& RomData = RomDatas[ResourceIndex];
					EnumAddFlags(RomData.Flags, ERomFlags::HighRes);
					
					++NumHighRes;
					NumHighResBytes += RomData.Size;
				}
			}
		}

		{
			MUTABLE_CPUPROFILER_SCOPE(GenerateRoms_ImageSourceIds);

			for (int32 ImageIndex = 0; ImageIndex < Program.ConstantImages.Num(); ++ImageIndex)
			{
				const FImageLODRange& LODRange = Program.ConstantImages[ImageIndex];

				uint32 SourceId = AdditionalData.SourceImagePerConstant[ImageIndex].SourceId;

				for (int32 LODRangeIndex = 0; LODRangeIndex < LODRange.LODCount; ++LODRangeIndex)
				{
					int32 RomIndex = Program.ConstantImageLODIndices[LODRange.FirstIndex + LODRangeIndex];
					RomDatas[RomIndex].SourceId = SourceId;
				}
			}
		}

		{
			MUTABLE_CPUPROFILER_SCOPE(GenerateRoms_ImageIdsUnique);

			for (int32 ResourceIndex = 0; ResourceIndex < Program.ConstantImageLODs.Num(); ++ResourceIndex)
			{
				FRomData& RomData = RomDatas[ResourceIndex];

				// If the resource uses less memory than the threshold, don't save it in a separate rom.
				if (int32(RomData.Size) <= EmbeddedDataBytesLimit)
				{
					NumEmbedded++;
					NumEmbeddedBytes += RomData.Size;
					continue;
				}
				NumRoms++;
				NumRomsBytes += RomData.Size;

				// Ensure that the Id is unique
				EnsureUniqueRomId(UsedIds, RomData.Id);

				int32 RomIndex = Program.m_roms.Add(RomData);

				TPair<int32, Ptr<const Image>>& ResData = Program.ConstantImageLODs[ResourceIndex];
				ResData.Key = RomIndex;
			}
		}

		// Meshes
		FMemory::Memzero( RomDatas.GetData(), RomDatas.GetAllocatedSize() );
		{
			MUTABLE_CPUPROFILER_SCOPE(GenerateRoms_MeshIds);

			ParallelFor(Program.ConstantMeshes.Num(),
				[EmbeddedDataBytesLimit, &Program, &RomDatas](uint32 ResourceIndex)
				{
					TPair<int32, Ptr<const Mesh>>& ResData = Program.ConstantMeshes[ResourceIndex];

					// This shouldn't have been serialised with rom support before.
					check(ResData.Key < 0);

					// Serialize to memory, to find out final size of this rom
					const Mesh* Resource = ResData.Value.get();
					int32 ApproximateSize = Resource->GetDataSize();

					// If the resource uses less memory than the threshold, don't save it in a separate rom.
					if (ApproximateSize <= EmbeddedDataBytesLimit)
					{
						return;
					}

					OutputMemoryStream MemStream(ApproximateSize + 64 * 1024);
					OutputArchive MemoryArch(&MemStream);
					Mesh::Serialise(ResData.Value.get(), MemoryArch);

					FRomData& RomData = RomDatas[ResourceIndex];
					RomData.ResourceType = DT_MESH;
					RomData.ResourceIndex = ResourceIndex;
					RomData.Size = MemStream.GetBufferSize();
					RomData.Flags = ERomFlags::None;

					// Ensure that the Id is unique
					uint64 FullHash = CityHash64(static_cast<const char*>(MemStream.GetBuffer()), MemStream.GetBufferSize());
					RomData.Id = GetTypeHash(FullHash);
				});
		}

		{
			MUTABLE_CPUPROFILER_SCOPE(GenerateRoms_MeshIdsUnique);

			for (int32 ResourceIndex = 0; ResourceIndex < Program.ConstantMeshes.Num(); ++ResourceIndex)
			{
				FRomData& RomData = RomDatas[ResourceIndex];
				RomData.SourceId = AdditionalData.SourceMeshPerConstant[ResourceIndex].SourceId;

				// If the resource uses less memory than the threshold, don't save it in a separate rom.
				if (int32(RomData.Size) <= EmbeddedDataBytesLimit)
				{
					NumEmbedded++;
					NumEmbeddedBytes += RomData.Size;
					continue;
				}
				NumRoms++;
				NumRomsBytes += RomData.Size;

				// Ensure that the Id is unique
				EnsureUniqueRomId(UsedIds, RomData.Id);

				int32 RomIndex = Program.m_roms.Add(RomData);

				TPair<int32, Ptr<const Mesh>>& ResData = Program.ConstantMeshes[ResourceIndex];
				ResData.Key = RomIndex;
			}
		}

		UE_LOG(LogMutableCore, Log, TEXT("Generated roms: %d (%d KB) are embedded, %d (%d KB) are streamed of which %d (%d KB) are high-res."), 
			NumEmbedded, uint32(NumEmbeddedBytes/1024), NumRoms, uint32(NumRomsBytes/1024), NumHighRes, uint32(NumHighResBytes/1024));
	}


}

