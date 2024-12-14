// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Text/UTF8String.h"

namespace uLang
{
namespace FilePathUtils
{

/*
 * Replaces backslashes with slashes and make drive letters uppercase
 */
ULANGCORE_API CUTF8String NormalizePath(const CUTF8StringView& Path);

/*
 * Determine if a path is relative (i.e. not fully qualified)
 */
ULANGCORE_API bool IsPathRelative(const CUTF8StringView& Path);

/*
 * Splits a path into the file name and the directory in front of it
 * The directory will have a slash or no slash at the end as specified by bIncludeDirEndSlash
 * If there's no slash in the path, returns false.
 */
ULANGCORE_API bool SplitPath(const CUTF8StringView& FilePath, CUTF8StringView& OutDir, CUTF8StringView& OutFileName, bool bIncludeDirEndSlash = false);

/*
 * Invoke a lambda with each part of a path from left to right
 * Slashes at either begin or end of path, as well as double slashes, will result in an invocation with an empty part
 */
template<typename FunctionType>
void ForeachPartOfPath(const CUTF8StringView& Path, FunctionType&& Lambda)
{
    const UTF8Char* PartBegin = Path._Begin;
    for (const UTF8Char* Ch = PartBegin; Ch <= Path._End; ++Ch)
    {
        if (Ch == Path._End || *Ch == '/' || *Ch == '\\')
        {
            Lambda(CUTF8StringView(PartBegin, Ch));
            PartBegin = Ch + 1;
        }
    }
}

/*
 * Appends a slash to the path if there isn't one already
 */
ULANGCORE_API CUTF8String AppendSlash(const CUTF8StringView& Path);

/*
 * Gets the containing directory of a file or directory
 */
ULANGCORE_API CUTF8String GetDirectory(const CUTF8StringView& Path, bool bIncludeDirEndSlash = false);

/*
 * Gets the unqualified name of a file or directory (without the containing path)
 */
ULANGCORE_API CUTF8String GetFileName(const CUTF8StringView& Path);

/*
 * Splits a file name into stem (name) and extension (including the dot)
 */
ULANGCORE_API void SplitFileName(const CUTF8StringView& FileName, CUTF8StringView& Stem, CUTF8StringView& Extension);

/*
 * Combines two paths
 */
ULANGCORE_API CUTF8String CombinePaths(const CUTF8StringView& LhsPath, const CUTF8StringView& RhsPath);

/*
 * Checks if a path is relative, and if so, combines it with given base path
 */
ULANGCORE_API CUTF8String ConvertRelativePathToFull(const CUTF8StringView& Path, const CUTF8StringView& BasePath);

/*
 * Creates a relative path by removing BasePath from the head of FullPath. Result will be empty if this is not possible.
 */
ULANGCORE_API CUTF8String ConvertFullPathToRelative(const CUTF8StringView& FullPath, const CUTF8StringView& BasePath);

/*
 * Gets stem/name of given file, or if stem is empty, name of enclosing directory
 */
ULANGCORE_API CUTF8StringView GetNameFromFileOrDir(const CUTF8StringView& FilePath);

/*
 * Check if a file/directory is a descendant of a given directory 
 * NOTE: This fails in a number of cases, but the most obvious one is when the parent directory passed in is a symbolic link and the second argument is the actual file/directory path.
 */
ULANGCORE_API bool IsDescendantOfDirectory(const CUTF8StringView& ParentDirectory, const CUTF8StringView& PotentialChildDirectory);

}
}
