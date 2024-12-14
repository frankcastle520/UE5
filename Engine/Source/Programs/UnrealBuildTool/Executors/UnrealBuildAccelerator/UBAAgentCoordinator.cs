// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.UBA;

namespace UnrealBuildTool
{
	// statusRow, statusColumn, statusText, statusType, statusLink
	using StatusUpdateAction = Action<uint, uint, string, LogEntryType, string?>;

	interface IUBAAgentCoordinator
	{
		DirectoryReference? GetUBARootDir();

		Task InitAsync(UBAExecutor executor);

		void Start(ImmediateActionQueue queue, Func<LinkedAction, bool> canRunRemotely, StatusUpdateAction updateStatus);

		void Stop();

		Task CloseAsync();

		void Done();
	}
}