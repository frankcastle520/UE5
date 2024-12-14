// Copyright Epic Games Tools, LLC. All Rights Reserved.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>

#include <map>
#include <vector>
#include <string>

#define uint64 uint64_t

#define VERSION "1.0"

bool GetLine(char* FileBuffer, uint64 BufferValid, uint64 InCurrentLineStart, uint64* OutCurrentLineEnd, uint64* OutNextLineStart)
{
    // Start reading at CurrentLineStart and continue until we hit a newline. Then consume
    // one newline.
    uint64 Cursor = InCurrentLineStart;
    for (;;)
    {
        if (Cursor >= BufferValid)
            return false;

        if (FileBuffer[Cursor] == '\r' ||
            FileBuffer[Cursor] == '\n')
        {
            // Got the line.
            *OutCurrentLineEnd = Cursor;

            // Consume ONE newline. If there isn't a complete newline then we fail.
            if (FileBuffer[Cursor] == '\r')
            {
                if (Cursor + 1 >= BufferValid ||
                    FileBuffer[Cursor + 1] != '\n')
                    return false;
                Cursor++;
            }
            Cursor++;
            *OutNextLineStart = Cursor;
            return true;
        }
        
        Cursor++;
    }
}


static unsigned int stb_hash_case_sensitive(const char* str, uint64 str_len)
{
    unsigned int hash = 0;
    for (uint64 i=0; i < str_len; i++)
    {
        hash = (hash << 7) + (hash >> 25) + str[i];
    }
    return hash + (hash >> 16);
}

struct DirEntry
{
    // Dir is tolowered()
    std::string Dir;
    uint32_t DirHash;

    static DirEntry Construct(char* String, uint64 StringLen)
    {
        DirEntry Result;
        Result.Dir = std::string(String, StringLen);

        // tolower everything
        for (char& c : Result.Dir)
        {
            c = (char)tolower(c);
        }

        Result.DirHash = stb_hash_case_sensitive(Result.Dir.c_str(), Result.Dir.length());
        return Result;
    }
};

struct cmp_str
{
    bool operator()(const DirEntry& a, const DirEntry& b) const
    {
        if (a.DirHash != b.DirHash)
            return a.DirHash < b.DirHash;

        if (a.Dir.length() < b.Dir.length())
            return true;
        else if (a.Dir.length() > b.Dir.length())
            return false;

        return strncmp(a.Dir.c_str(), b.Dir.c_str(), a.Dir.length()) < 0;
    }
};

SRWLOCK CacheLock;
// Map from a tolowered name to the OS case correct directory name.
std::map<DirEntry, std::string, cmp_str> CaseFixedDirectories;



HANDLE DoneSemaphore = INVALID_HANDLE_VALUE;
bool Closing = false;

struct LineEntry
{
    char* FileBuffer;
    char* RootPath;
    uint64 RootPathLen;
    uint64 FileNameStart, FileNameEnd;
    uint64 LineStart;
};

struct ThreadJob
{
    HANDLE PostSemaphore;
    uint64 StartIndex, EndIndex;
};

constexpr int ThreadCount = 8;
ThreadJob ThreadJobs[ThreadCount];

std::vector<LineEntry> ThreadLines;
char* ThreadFileBuffer = 0;

void WorkLine(char* FileBuffer, uint64 FileNameStart, uint64 FileNameEnd, char* RootPath, uint64 RootPathLen)
{
    uint64 FileNameLen = FileNameEnd - FileNameStart;

    char FileNameEndChar = FileBuffer[FileNameEnd];
    FileBuffer[FileNameEnd] = 0;

    // Go up the directory tree to our root, finding the case for each spot.
    uint64 DirStart = FileNameStart + RootPathLen;

    for (;;)
    {
        // Find the end of this name
        uint64 DirEnd = DirStart;
        while (DirEnd < FileNameEnd && FileBuffer[DirEnd] != '/')
            DirEnd++;

        char DirEndChar = FileBuffer[DirEnd];
        FileBuffer[DirEnd] = 0;

        // If it's a directory, see if we already know the capitalization
        if (DirEndChar == '/')
        {
            DirEntry SearchTerm = DirEntry::Construct(FileBuffer + FileNameStart, DirEnd - FileNameStart);

            AcquireSRWLockShared(&CacheLock);

            auto found = CaseFixedDirectories.find(SearchTerm);
            if (found != CaseFixedDirectories.end())
            {
                // Already know this one, we can just copy over.
                //printf("Cache hit %s -> %s\n", FileBuffer + FileNameStart, found->second.c_str());
                memcpy(FileBuffer + FileNameStart, found->second.c_str(), found->second.length());

                ReleaseSRWLockShared(&CacheLock);
                goto NextDir;
            }
            ReleaseSRWLockShared(&CacheLock);
        }

        // otherwise we need to go the normal route.

        WIN32_FIND_DATA FindData;
        HANDLE FindResult = FindFirstFileExA(FileBuffer + FileNameStart, FindExInfoBasic, &FindData, FindExSearchNameMatch, 0, 0);
        FindClose(FindResult);

        // Emplace the name over top.
        if (FindResult != INVALID_HANDLE_VALUE)
        {
            memcpy(FileBuffer + DirStart, FindData.cFileName, DirEnd - DirStart);

            // If this is a directory, add it to our directory cache.
            if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                DirEntry SearchTerm = DirEntry::Construct(FileBuffer + FileNameStart, DirEnd - FileNameStart);

                AcquireSRWLockExclusive(&CacheLock);
                CaseFixedDirectories.insert({ SearchTerm, std::string(FileBuffer + FileNameStart, DirEnd - FileNameStart) });
                ReleaseSRWLockExclusive(&CacheLock);

                //printf("Added: %s\n", FileBuffer + FileNameStart);
            }
        }

    NextDir:

        FileBuffer[DirEnd] = DirEndChar;
        DirStart = DirEnd + 1;

        if (DirStart >= FileNameEnd)
            break;
    }

    uint64 NewFileNameEnd = FileNameEnd - RootPathLen;
    memmove(FileBuffer + FileNameStart, FileBuffer + FileNameStart + RootPathLen, FileNameLen - RootPathLen);

    // The rest of the line we need to turn in to an empty line.
    FileBuffer[NewFileNameEnd] = '\n';

    if (FileNameEnd > (NewFileNameEnd + 1))
        memset(FileBuffer + NewFileNameEnd + 1, ' ', FileNameEnd - NewFileNameEnd - 1);

    FileBuffer[FileNameEnd] = FileNameEndChar;
}

DWORD WINAPI ThreadProc(
    _In_ LPVOID lpParameter
)
{
    uint64 ThreadId = (uint64)lpParameter;

    // Wait until we have a list of jobs to do.
    for (;;)
    {
        WaitForSingleObject(ThreadJobs[ThreadId].PostSemaphore, INFINITE);

        if (Closing)
            return 0;

        // Look up our work.
        for (uint64 LineIndex = ThreadJobs[ThreadId].StartIndex; LineIndex < ThreadJobs[ThreadId].EndIndex; LineIndex++)
        {
            LineEntry& entry = ThreadLines[LineIndex];
            WorkLine(entry.FileBuffer, entry.FileNameStart, entry.FileNameEnd, entry.RootPath, entry.RootPathLen);
        }

        // Done.
        ReleaseSemaphore(DoneSemaphore, 1, 0);
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // Load a psym file off the command line and scan for the path specified in the
    // second argument, and trim it. Additionally, do a "fix case" operation on 
    // such files to ensure they match what will exist on case sensititve platforms.
    if (argc < 3)
    {
show_help:
        printf("symbol_path_fixer " VERSION "\n\n");
        printf("Used to strip paths off the FILE entries in a breakpad portable symbols file\n");
        printf("as well as make FILE entries under that path match the case of the underlying\n");
        printf("file system entry.\n");
        printf("\n");
        printf("Usage: symbol_path_fixer path/to/psym path/to/strip\n");
        printf("\n");
        printf("Example: symbol_path_fixer c:/my.psym c:/projects/checkout_root\n");
        printf("will make FILE entries such as c:/projects/checkout_root/source/mainprogram.cpp\n");
        printf("turn in to source/MainProgram.cpp\n");
        return 1;
    }

    InitializeSRWLock(&CacheLock);

    DoneSemaphore = CreateSemaphoreA(0, 0, ThreadCount, 0);

    for (uint64 i = 0; i <ThreadCount; i++)
    {
        ThreadJobs[i].PostSemaphore = CreateSemaphoreA(0, 0, 1, 0);
        CreateThread(0, 64*1024, ThreadProc, (LPVOID)i, 0, 0);
    }

    char* RootPath = 0;
    uint64 RootPathLen = strlen(argv[2]);
    if (RootPathLen < 2)
    {
        printf("Root path missing.\n\n");
        goto show_help;
    }

    // normalize slashes and add trailing if needed.
    if (argv[2][RootPathLen-1] != '\\' &&
        argv[2][RootPathLen-1] != '/')
    {
        RootPath = (char*)malloc(RootPathLen + 2);
        memcpy(RootPath, argv[2], RootPathLen);
        RootPath[RootPathLen] = '/';
        RootPath[RootPathLen+1] = 0;
        RootPathLen++;
    }
    else
    {
        RootPath = (char*)malloc(RootPathLen + 1);
        memcpy(RootPath, argv[2], RootPathLen);
        RootPath[RootPathLen] = 0;
    }

    for (char* slash = RootPath; *slash; slash++)
    {
        if (*slash == '\\')
            *slash = '/';
    }

    HANDLE SymFile = CreateFile(
        argv[1], 
        GENERIC_READ | GENERIC_WRITE, 
        0, // no sharing
        nullptr,
        OPEN_EXISTING, 
        0,
        0);
    if (SymFile == INVALID_HANDLE_VALUE)
    {
        printf("Failed to open %s\n\n", argv[1]);
        goto show_help;
    }

    // The psym file is defined to be:
    // first line "MODULE..."
    // misc
    // N lines bunched together starting with FILE
    LARGE_INTEGER ReadFileOffset;
    ReadFileOffset.QuadPart = 0;

    LARGE_INTEGER WriteFileOffset;
    WriteFileOffset.QuadPart = 0;

    //
    // The only places the filenames can exist is up front with the FILE listing, so we only
    // look at the psym data through that point to save time. Once we're done with the FILE entries
    // we just copy the remaining data down - we know we are only removing data.
    //
    uint64 BufferMax = 1024 * 1024;
    char* FileBuffer = (char*)malloc(BufferMax);
    uint64 BufferValid = 0;
    bool FoundFiles = false; // if we've started processing FILE entries.
    bool DoneWithFiles = false; // if we're done processing FILE entries.
    for (;;)
    {
        SetFilePointerEx(SymFile, ReadFileOffset, 0, FILE_BEGIN);

        // Fill the buffer.
        uint64 BytesToRead64 = BufferMax - BufferValid;
        if (BytesToRead64 == 0 || BytesToRead64 > ~0U)
        {
            // We didn't consume any of our input in the last run... something's gone wrong, bail
            break;
        }
        DWORD BytesToRead32 = (DWORD)BytesToRead64;
        DWORD BytesRead32 = 0;
        if (!ReadFile(SymFile,
            FileBuffer + BufferValid,
            BytesToRead32,
            &BytesRead32,
            nullptr))
        {
            // Failed to read for some reason
            break;
        }

        if (BytesRead32 == 0)
        {
            // EOF, should have already done what we needed to do.
            break;
        }

        if (BufferValid + BytesRead32 > BufferMax ||
            BufferValid + BytesRead32 < BufferValid)
        {
            // odd.
            return 3;
        }
        ReadFileOffset.QuadPart += BytesRead32;

        BufferValid += BytesRead32;

        
        // Now try to process the buffer. Note this fails to handle the last line of a file if there's
        // no newline, however we know in the psyms that we are done with the FILE entries well before EOF
        // so we don't care.
        uint64 NextLineStart = 0;
        uint64 CurrentLineEnd = 0;
        uint64 CurrentLineStart = 0;
        while (GetLine(FileBuffer, BufferValid, CurrentLineStart, &CurrentLineEnd, &NextLineStart))
        {
            if (CurrentLineEnd - CurrentLineStart < 4)
                goto NextLine;

            if (memcmp(FileBuffer + CurrentLineStart, "FILE", 4) == 0)
            {
                FoundFiles = true;

                // Do path conversion if necessary. The file is the 3rd entry.
                uint64 FileNameStart = CurrentLineStart;
                int SpaceCount = 0;
                for (; FileNameStart < CurrentLineEnd; FileNameStart++)
                {
                    if (FileBuffer[FileNameStart] == ' ')
                    {
                        SpaceCount++;
                        if (SpaceCount == 2)
                        {
                            FileNameStart++;
                            break;
                        }
                    }
                }

                uint64 FileNameEnd = FileNameStart;
                for (; FileNameEnd < CurrentLineEnd; FileNameEnd++)
                {
                    // Replace backslash with forward slash
                    if (FileBuffer[FileNameEnd] == '\\')
                        FileBuffer[FileNameEnd] = '/';
                }

                // We only care if it's under our root path.
                uint64 FileNameLen = FileNameEnd - FileNameStart;
                if (FileNameLen < RootPathLen)
                    goto NextLine; 

                // RootPath is fwd slashes, and has a trailing slash.
                if (_strnicmp(FileBuffer + FileNameStart, RootPath, RootPathLen))
                    goto NextLine;

                if (FileNameLen > 1023)
                {
                    printf("FileName too long %.*s\n", (DWORD)FileNameLen, FileBuffer + FileNameStart);
                    goto NextLine;
                }

                // Add the line to our queue of work to convert the filename case. This fails if the file 
                // doesn't exist on disk, but that's OK, we only care if it does.
                LineEntry entry;
                entry.FileNameStart = FileNameStart;
                entry.FileNameEnd = FileNameEnd;
                entry.FileBuffer = FileBuffer;
                entry.RootPath = RootPath;
                entry.RootPathLen = RootPathLen;
                entry.LineStart = CurrentLineStart;

                ThreadLines.push_back(entry);
            }
            else if (FoundFiles)
            {
                // We are done with the files.
                DoneWithFiles = true;
                goto FlushLines;
            }

            NextLine:
            CurrentLineStart = NextLineStart;
        }

    FlushLines:

        // Dispatch the queue of work to our threads.
        uint64 LinesPerThread = ThreadLines.size() / ThreadCount;
        for (uint64 i =0; i < ThreadCount; i++)
        {
            ThreadJobs[i].StartIndex = i*LinesPerThread;
            ThreadJobs[i].EndIndex = (i+1)*LinesPerThread;
            if (i == ThreadCount - 1)
                ThreadJobs[i].EndIndex = ThreadLines.size();

            ReleaseSemaphore(ThreadJobs[i].PostSemaphore, 1, 0);
        }

        // wait for everyone to complete. They each increment by one, so decrement threadcount times.
        for (uint64 i = 0; i < ThreadCount; i++)
        {
            WaitForSingleObject(DoneSemaphore, INFINITE);
        }

        ThreadLines.clear();

        // Remove our empty lines.
        uint64 WriteOffset = 0;
        {
            uint64 NextClearLineStart = 0;
            uint64 ClearLineEnd = 0;
            uint64 ClearLineStart = 0;
            while (GetLine(FileBuffer, CurrentLineStart, ClearLineStart, &ClearLineEnd, &NextClearLineStart))
            {
                if (FileBuffer[ClearLineStart] != ' ')
                {
                    // Copy this line, include the newline.
                    memcpy(FileBuffer + WriteOffset, FileBuffer + ClearLineStart, NextClearLineStart - ClearLineStart);
                    WriteOffset += NextClearLineStart - ClearLineStart;
                }

                ClearLineStart = NextClearLineStart;
            }
        }

        // We need to write our changes to disk and move to the next buffer. We've processed up to CurrentLineStart,
        // and that starts at CurrentFileOffset;
        SetFilePointerEx(SymFile, WriteFileOffset, 0, FILE_BEGIN);

        // Lines fit in a buffer which is guaranteed to be less than 4gb.
        int TryCount = 0;
        for (; TryCount < 3; TryCount++)
        {
            DWORD WriteCount = 0;
            if (!WriteFile(SymFile, FileBuffer, (DWORD)WriteOffset, &WriteCount, 0))
            {
                printf("Failed to write (%d) trying again (%d / 3)\n", GetLastError(), TryCount);
                continue;
            }
            if (WriteCount != (DWORD)WriteOffset)
            {
                printf("bad news, partial write %d %d\n", WriteCount, (DWORD)WriteOffset);
                return 4;
            }
            break;
        }

        WriteFileOffset.QuadPart += WriteOffset;
        if (DoneWithFiles)
        {
            // We're done with the files, but we need to copy the rest of the data.
            // Write out the rest of the buffer that we haven't processed.
            WriteFile(SymFile, FileBuffer + CurrentLineStart, (DWORD)(BufferValid - CurrentLineStart), 0, 0);
            WriteFileOffset.QuadPart += BufferValid - CurrentLineStart;

            // Now stream data from farther in the file.
            for (;;)
            {
                SetFilePointerEx(SymFile, ReadFileOffset, 0, FILE_BEGIN);
                if (!ReadFile(SymFile, FileBuffer, (DWORD)BufferMax, &BytesRead32, 0))
                {
                    printf("failed to read stream %d\n", GetLastError());
                    return 5;
                }

                if (BytesRead32 == 0)
                    break; // eof

                ReadFileOffset.QuadPart += BytesRead32;

                DWORD BytesWritten32 = 0;
                SetFilePointerEx(SymFile, WriteFileOffset, 0, FILE_BEGIN);
                if (!WriteFile(SymFile, FileBuffer, BytesRead32, &BytesWritten32, 0) ||
                    BytesWritten32 != BytesRead32)
                {
                    printf("failed to write stream: %d\n", GetLastError());
                    return 6;
                }

                WriteFileOffset.QuadPart += BytesRead32;
            }

            SetFilePointerEx(SymFile, WriteFileOffset, 0, FILE_BEGIN);
            if (!SetEndOfFile(SymFile))
            {
                printf("failed to set eof: %d\n", GetLastError());
                return 7;
            }

            // Done copying down the file, so we're done with everything!
            break;
        }

        // Ran out of lines. 
        memmove(FileBuffer, FileBuffer + CurrentLineStart, BufferValid - CurrentLineStart);
        BufferValid = BufferValid - CurrentLineStart;
    }

    CloseHandle(SymFile);
    return 0;
}

// @cdep pre $Defaults
// @cdep pre $addtocswitches(/EHsc)
// @cdep post $Build