// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.Server
{
	/// <summary>
	/// Interface for a service which keeps track of whether we're during downtime
	/// </summary>
	public interface IDowntimeService
	{
		/// <summary>
		/// Returns true if downtime is currently active
		/// </summary>
		public bool IsDowntimeActive
		{
			get;
		}
	}
}
