// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/Function.h"
#include "uLang/Common/Text/UTF8String.h"

namespace uLang
{
class IFileSystem : public CSharedMix
{
public:
    enum EWriteFlags
    {
        None = 0,
        EvenIfReadOnly,
    };

    virtual ~IFileSystem()
    {}

    /**
     * CFileDirectoryVisitor
     * @return True to keep iterating, false to abort.
     */
    using CFileDirectoryVisitor = TFunction<bool(const char* /*FileName*/, const char* /*DirPath*/, bool /*bIsDirectory*/)>;

    virtual bool IterateDirectory(const char* Directory, bool bRecurse, const CFileDirectoryVisitor& Visitor, bool bIsVisitorThreadSafe = false) = 0;

    /**
     * CFileMemAllocator
     * @return Buffer pointer to fill, null if the requested memory allocation failed or is disallowed.
     */
    using CFileMemAllocator = TFunction<void*(int32_t /*NeededByteSize*/)>;

    virtual bool FileRead(const char* FilePath, const CFileMemAllocator& Allocator) = 0;
    virtual bool FileWrite(const char* FilePath, const char* Output, const size_t ByteSize, const EWriteFlags WriteFlags = EWriteFlags::None) = 0;
    virtual bool DeleteFile(const char* FilePath) = 0;
    virtual bool IsFileNewer(const char* FilePath, const char* ComparisonPath) = 0;
    virtual bool CopyFile(const char* SourcePath, const char* DestinationPath) = 0;
    virtual bool DoesFileExist(const char* FilePath) = 0;
    virtual bool GetFilenameOnDisk(const char* FilePath, CUTF8String& OutFilenameOnDisk) = 0;
    virtual bool CreateDirectory(const char* DirPath) = 0;
    virtual bool DeleteDirectory(const char* DirPath) = 0;
    virtual bool DoesDirectoryExist(const char* DirPath) = 0;

    /**
     * Retrieves the path of the directory designated for temporary files.
     *
     * @param OutDirectory    Storage for the directory path.
     *
     * @return    `true` if the directory was retrieved successfully, `false` otherwise.
     */
    virtual bool FindTempDir(CUTF8String& OutDir) const = 0;

    /**
     * Creates a temporary file. The name is guaranteed to be unique.
     *
     * @param OutFilename 		Storage for the temporary filename.
     *
     * @return 				`true` if the temporary file was created successfully, `false` otherwise.
     */
    virtual bool MakeTempFile(CUTF8String& OutFilename) = 0;

    virtual bool GetCurrentWorkingDirectory(CUTF8String& OutWorkingDirectory) = 0;

    /**
     * Note that SetCurrentWorkingDirectory may not be implemented in some embeddings (e.g. Unreal).
     */
    virtual bool SetCurrentWorkingDirectory(const char* DirPath)
    {
        return false;
    }

    virtual bool IsReadOnly(const char* Filename) = 0;
    virtual bool SetReadOnly(const char* Filename, const bool bReadOnly) = 0;
};
}
