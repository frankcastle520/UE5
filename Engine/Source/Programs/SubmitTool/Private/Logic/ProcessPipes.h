// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/** Wrapper around stdin and stdout pipes created by a call to FPlatformProcess::CreatePipe */
struct FProcessPipes
{
	FProcessPipes() = default;

	~FProcessPipes()
	{
		Reset();
	}

	bool Create();

	void Reset();

	bool IsValid() const;

	void* GetStdInForProcess() const
	{
		return StdInReadPipe;
	}

	void* GetStdInForWriting() const
	{
		return StdInWritePipe;
	}

	void* GetStdOutForProcess() const
	{
		return StdOutWritePipe;
	}

	void* GetStdOutForReading() const
	{
		return StdOutReadPipe;
	}

private:

	void* StdOutReadPipe = nullptr;
	void* StdOutWritePipe = nullptr;

	void* StdInReadPipe = nullptr;
	void* StdInWritePipe = nullptr;
};
