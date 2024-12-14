// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Text.Json;

#pragma warning disable CA2227

namespace EpicGames.Horde.Logs
{
	/// <summary>
	/// Severity of a log event
	/// </summary>
	public enum LogEventSeverity
	{
		/// <summary>
		/// Severity is not specified
		/// </summary>
		Unspecified = 0,

		/// <summary>
		/// Information severity
		/// </summary>
		Information = 1,

		/// <summary>
		/// Warning severity
		/// </summary>
		Warning = 2,

		/// <summary>
		/// Error severity
		/// </summary>
		Error = 3,
	}

	/// <summary>
	/// Information about an uploaded event
	/// </summary>
	public class GetLogEventResponse
	{
		/// <summary>
		/// Unique id of the log containing this event
		/// </summary>
		public LogId LogId { get; set; }

		/// <summary>
		/// Severity of this event
		/// </summary>
		public LogEventSeverity Severity { get; set; }

		/// <summary>
		/// Index of the first line for this event
		/// </summary>
		public int LineIndex { get; set; }

		/// <summary>
		/// Number of lines in the event
		/// </summary>
		public int LineCount { get; set; }

		/// <summary>
		/// The issue id associated with this event
		/// </summary>
		public int? IssueId { get; set; }

		/// <summary>
		/// The structured message data for this event
		/// </summary>
		public List<JsonElement> Lines { get; set; } = new List<JsonElement>();
	}
}
