// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Text/FilePathUtils.h"
#include "uLang/Common/Text/Unicode.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"

namespace uLang
{
namespace FilePathUtils
{

CUTF8String NormalizePath(const CUTF8StringView& Path)
{
    if (Path.IsEmpty())
    {
        return CUTF8String();
    }

    return CUTF8String(Path.ByteLen(), [&Path](UTF8Char* Memory)
    {
        memcpy(Memory, Path._Begin, Path.ByteLen());

        // Normalize Windows drive letters to upper case.
        if (Path.ByteLen() >= 2 && CUnicode::IsLowerASCII(Memory[0]) && Memory[1] == ':')
        {
            Memory[0] = CUnicode::ToUpper_ASCII(Memory[0]);
        }

        for (UTF8Char* Ch = Memory + Path.ByteLen() - 1; Ch >= Memory; --Ch)
        {
            if (*Ch == '\\')
            {
                *Ch = '/';
            }
        }
    });
}

bool IsPathRelative(const CUTF8StringView& Path)
{
    return (!Path.Contains(':') && Path.FirstByte() != '/' && Path.FirstByte() != '\\');
}

bool SplitPath(const CUTF8StringView& FilePath, CUTF8StringView& OutDir, CUTF8StringView& OutFileName, bool bIncludeDirEndSlash)
{
    if (ULANG_ENSUREF(FilePath.IsFilled(), "Expected non-empty file path"))
    {
        const UTF8Char* LastDelim = FilePath._End;
        while (--LastDelim >= FilePath._Begin)
        {
            if (*LastDelim == '/' || *LastDelim == '\\')
            {
                // Assign in this order in case any of the inputs is the same as the output
                OutDir._Begin = FilePath._Begin;
                OutFileName._End = FilePath._End;
                OutDir._End = LastDelim + (bIncludeDirEndSlash ? 1 : 0);
                OutFileName._Begin = LastDelim + 1;
                return true;
            }
        }
    }

    OutDir.Reset();
    OutFileName = FilePath;
    return false;
}

CUTF8String AppendSlash(const CUTF8StringView& Path)
{
    if (Path.EndsWith("/") || Path.EndsWith("\\"))
    {
        return Path;
    }

    CUTF8StringBuilder Result(Path);
    Result.Append("/");
    return Result.MoveToString();
}

CUTF8String GetDirectory(const CUTF8StringView& Path, bool bIncludeDirEndSlash)
{
    CUTF8StringView Parent, Child;
    SplitPath(Path, Parent, Child, bIncludeDirEndSlash);
    return Parent;
}

CUTF8String GetFileName(const CUTF8StringView& Path)
{
    CUTF8StringView Parent, Child;
    SplitPath(Path, Parent, Child);
    return Child;
}

void SplitFileName(const CUTF8StringView& FileName, CUTF8StringView& Stem, CUTF8StringView& Extension)
{
    if (FileName.IsFilled())
    {
        const UTF8Char* LastDelim = FileName._End;
        while (--LastDelim >= FileName._Begin)
        {
            if (*LastDelim == '.')
            {
                Stem._Begin = FileName._Begin;
                Stem._End = LastDelim;
                Extension._Begin = LastDelim;
                Extension._End = FileName._End;
                return;
            }
        }
    }

    Extension.Reset();
    Stem = FileName;
}

CUTF8String CombinePaths(const CUTF8StringView& LhsPath, const CUTF8StringView& RhsPath)
{
    CUTF8StringView TrimmedLhsPath = LhsPath;
    CUTF8StringView TrimmedRhsPath = RhsPath;
    // Remove trailing slash
    if (TrimmedLhsPath.LastByte() == '/' || TrimmedLhsPath.LastByte() == '\\')
    {
        TrimmedLhsPath = TrimmedLhsPath.SubViewTrimEnd(1);
    }
    // Remove leading slash
    if (TrimmedRhsPath.FirstByte() == '/' || TrimmedRhsPath.FirstByte() == '\\')
    {
        TrimmedRhsPath = TrimmedRhsPath.SubViewTrimBegin(1);
    }
    // Collapse parent folder references
    while (TrimmedRhsPath.ByteLen() >= 3 && TrimmedRhsPath[0] == '.' && TrimmedRhsPath[1] == '.' && (TrimmedRhsPath[2] == '/' || TrimmedRhsPath[2] == '\\'))
    {
        CUTF8StringView Lhs1, Lhs2;
        if (!FilePathUtils::SplitPath(TrimmedLhsPath, Lhs1, Lhs2))
        {
            break;
        }
        TrimmedLhsPath = Lhs1;
        TrimmedRhsPath = TrimmedRhsPath.SubViewTrimBegin(3);
    }

    return CUTF8String(TrimmedLhsPath.ByteLen() + TrimmedRhsPath.ByteLen() + 1, [&TrimmedLhsPath, &TrimmedRhsPath](UTF8Char* Memory)
    {
        memcpy(Memory, TrimmedLhsPath._Begin, TrimmedLhsPath.ByteLen());
        memcpy(Memory + TrimmedLhsPath.ByteLen() + 1, TrimmedRhsPath._Begin, TrimmedRhsPath.ByteLen());
        Memory[TrimmedLhsPath.ByteLen()] = '/';
    });
}

CUTF8String ConvertRelativePathToFull(const CUTF8StringView& Path, const CUTF8StringView& BasePath)
{
    if (FilePathUtils::IsPathRelative(Path))
    {
        return FilePathUtils::CombinePaths(BasePath, Path);
    }

    return Path;
}

CUTF8String ConvertFullPathToRelative(const CUTF8StringView& FullPath, const CUTF8StringView& BasePath)
{
    if (IsPathRelative(FullPath) || IsPathRelative(BasePath))
    {
        return CUTF8String();
    }

    const UTF8Char *ChFull, *ChBase;
    const UTF8Char *CommonFull = nullptr, *CommonBase = nullptr;

    // Find common portion of path
    for (ChFull = FullPath._Begin, ChBase = BasePath._Begin; ChFull < FullPath._End && ChBase < BasePath._End; ++ChFull, ++ChBase)
    {
        if ((*ChFull == '/' || *ChFull == '\\') && (*ChBase == '/' || *ChBase == '\\'))
        {
            CommonFull = ChFull;
            CommonBase = ChBase;
        }
        else if (CUnicode::ToUpper_ASCII(*ChFull) != CUnicode::ToUpper_ASCII(*ChBase))
        {
            break;
        }
    }

    if (ChFull == FullPath._End || ChBase == BasePath._End)
    {
        CommonFull = ChFull;
        CommonBase = ChBase;
    }

    if (!CommonFull)
    {
        return CUTF8String();
    }

    // Skip slash in full path if any
    if (CommonFull < FullPath._End && (*CommonFull == '/' || *CommonFull == '\\'))
    {
        ++CommonFull;
    }

    // Is the base path completely contained in the full path?
    if (CommonBase == BasePath._End)
    {
        // Yes, then that's easy
        return CUTF8StringView(CommonFull, FullPath._End);
    }

    // Else, add parent dots for every additional fragment in the base path
    CUTF8StringBuilder Result;
    for (ChBase = CommonBase; ChBase < BasePath._End; ++ChBase)
    {
        if (*ChBase == '/' || *ChBase == '\\')
        {
            Result.Append("..");
            Result.Append(*ChBase);
        }
    }
    Result.Append(CUTF8StringView(CommonFull, FullPath._End));
    return Result.MoveToString();
}

CUTF8StringView GetNameFromFileOrDir(const CUTF8StringView& FilePath)
{
    CUTF8StringView DirPath, FileName, Name, Extension;
    FilePathUtils::SplitPath(FilePath, DirPath, FileName);
    FilePathUtils::SplitFileName(FileName, Name, Extension);
    if (Name.IsFilled())
    {
        return Name;
    }

    CUTF8StringView DirParent, DirName;
    FilePathUtils::SplitPath(DirPath, DirParent, DirName);
    return DirName;
}

bool IsDescendantOfDirectory(const CUTF8StringView& ParentDirectory, const CUTF8StringView& PotentialDescendantDirectory)
{
    // If the next character is a slash, it ensures the whole directory name is being accounted for by ParentDiectory.ByteLen()
    bool bNextCharacterInModulePathIsSlash = false;
    
    // PotentialChildDiectory must be of a larger length to reside in ParentDirectory
    if (PotentialDescendantDirectory.ByteLen() > ParentDirectory.ByteLen())
    {
        bNextCharacterInModulePathIsSlash = PotentialDescendantDirectory[ParentDirectory.ByteLen()] == '/' || PotentialDescendantDirectory[ParentDirectory.ByteLen()] == '\\';
    }

    return bNextCharacterInModulePathIsSlash && !memcmp(PotentialDescendantDirectory._Begin, ParentDirectory._Begin, ParentDirectory.ByteLen());
}

} // namespace FilePathUtils
} // namespace uLang
