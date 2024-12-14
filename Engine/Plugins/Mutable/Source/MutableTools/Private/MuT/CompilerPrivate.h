// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/Compiler.h"

#include "MuT/AST.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/NodeObjectPrivate.h"
#include "MuR/Operations.h"


namespace mu
{
	/** Statistics about the proxy file usage. */
	struct FProxyFileContext
	{
		FProxyFileContext();

		/** Options */

		/** Minimum data size in bytes to dump it to the disk. */
		uint64 MinProxyFileSize = 64 * 1024;

		/** When creating temporary files, number of retries in case the OS-level call fails. */
		uint64 MaxFileCreateAttempts = 256;

		/** Statistics */
		std::atomic<uint64> FilesWritten = 0;
		std::atomic<uint64> FilesRead = 0;
		std::atomic<uint64> BytesWritten = 0;
		std::atomic<uint64> BytesRead = 0;

		/** Internal data. */
		std::atomic<uint64> CurrentFileIndex = 0;
	};

		
    //!
    class CompilerOptions::Private
    {
    public:

        //! Detailed optimization options
        FModelOptimizationOptions OptimisationOptions;
		FProxyFileContext DiskCacheContext;

		uint64 EmbeddedDataBytesLimit = 1024;
		uint64 PackagedDataBytesLimit = 1024*1024*64;

		// \TODO: Unused?
		int32 MinTextureResidentMipCount = 3;

        int32 ImageCompressionQuality = 0;
		int32 ImageTiling = 0 ;

		/** If this flag is enabled, the compiler can use concurrency to reduce compile time at the cost of higher CPU and memory usage. */
		bool bUseConcurrency = false;

		bool bIgnoreStates = false;
		bool bLog = false;

		FImageOperator::FImagePixelFormatFunc ImageFormatFunc;
    };


    //!
    struct FStateCompilationData
    {
        FObjectState nodeState;
        Ptr<ASTOp> root;
        FProgram::FState state;
		//FStateOptimizationOptions optimisationFlags;

        //! List of instructions that need to be cached to efficiently update this state
        TArray<Ptr<ASTOp>> m_updateCache;

        //! List of root instructions for the dynamic resources that depend on the runtime
        //! parameters of this state.
		TArray< TPair<Ptr<ASTOp>, TArray<FString> > > m_dynamicResources;
    };


    //!
    class Compiler::Private
    {
    public:

        Private()
        {
            ErrorLog = new mu::ErrorLog();
        }

        Ptr<ErrorLog> ErrorLog;

        /** */
        Ptr<CompilerOptions> m_options;

		/** Store for additional data generated during compilation, but not necessary for the runtime. */
		struct FAdditionalData
		{
			/** Source data descriptor for every image constant that has been generated. 
			* It must have the same size than the Program::ConstantImages array.
			*/
			TArray<FSourceDataDescriptor> SourceImagePerConstant;

			/** Source data descriptor for every mesh constant that has been generated.
			* It must have the same size than the Program::ConstantMeshes array.
			*/
			TArray<FSourceDataDescriptor> SourceMeshPerConstant;
		};

		//!
		void GenerateRoms(Model*, const CompilerOptions*, const FLinkerOptions::FAdditionalData&);

    };

}
