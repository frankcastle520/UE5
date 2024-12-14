// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Issues
{
	/// <summary>
	/// Wraps a log event and allows it to be tagged by issue handlers
	/// </summary>
	public class IssueEvent
	{
		/// <summary>
		/// Index of the line within this log
		/// </summary>
		public int LineIndex { get; }

		/// <summary>
		/// Severity of the event
		/// </summary>
		public LogLevel Severity { get; }

		/// <summary>
		/// The type of event
		/// </summary>
		public EventId? EventId { get; }

		/// <summary>
		/// Gets this event data as a BSON document
		/// </summary>
		public IReadOnlyList<JsonLogEvent> Lines { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public IssueEvent(int lineIndex, LogLevel severity, EventId? eventId, IReadOnlyList<JsonLogEvent> lines)
		{
			LineIndex = lineIndex;
			Severity = severity;
			EventId = eventId;
			Lines = lines;
		}

		/// <summary>
		/// Renders the entire message of this event
		/// </summary>
		public string Render()
			=> String.Join("\n", Lines.Select(x => x.GetRenderedMessage().ToString()));

		/// <inheritdoc/>
		public override string ToString() => $"[{LineIndex}] {Render()}";
	}

	/// <summary>
	/// A group of <see cref="IssueEvent"/> objects with their fingerprint
	/// </summary>
	public class IssueEventGroup
	{
		/// <summary>
		/// The type of issue, which defines the handler to use for it
		/// </summary>
		public string Type { get; set; }

		/// <summary>
		/// Template string for the issue summary
		/// </summary>
		public string SummaryTemplate { get; set; }

		/// <summary>
		/// List of keys which identify this issue.
		/// </summary>
		public HashSet<IssueKey> Keys { get; } = new HashSet<IssueKey>();

		/// <summary>
		/// Collection of additional metadata added by the handler
		/// </summary>
		public HashSet<IssueMetadata> Metadata { get; } = new HashSet<IssueMetadata>();

		/// <summary>
		/// Filter for changes that should be included in this issue
		/// </summary>
		public string ChangeFilter { get; set; }

		/// <summary>
		/// Individual log events
		/// </summary>
		public List<IssueEvent> Events { get; } = new List<IssueEvent>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="type">The type of issue</param>
		/// <param name="summaryTemplate">Template for the summary string to display for the issue</param>
		/// <param name="changeFilter">Filter for changes covered by this issue</param>
		public IssueEventGroup(string type, string summaryTemplate, string changeFilter)
		{
			Type = type;
			SummaryTemplate = summaryTemplate;
			ChangeFilter = changeFilter;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="type">The type of issue</param>
		/// <param name="summaryTemplate">Template for the summary string to display for the issue</param>
		/// <param name="changeFilter">Filter for changes covered by this issue</param>
		public IssueEventGroup(string type, string summaryTemplate, IReadOnlyList<string> changeFilter)
			: this(type, summaryTemplate, String.Join(";", changeFilter))
		{
		}
	}
}
