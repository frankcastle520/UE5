// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Jobs;
using EpicGames.Horde.Jobs.Templates;
using MongoDB.Bson.Serialization.Attributes;

#pragma warning disable CA2227 // Change x to be read-only by removing the property setter

namespace HordeServer.Jobs.Templates
{
	/// <summary>
	/// Base class for parameters used to configure templates via the new build dialog
	/// </summary>
	[BsonDiscriminator(RootClass = true)]
	[BsonKnownTypes(typeof(TextParameter), typeof(ListParameter), typeof(BoolParameter))]
	public abstract class Parameter
	{
		/// <summary>
		/// Gets the arguments for a job given a set of parameters
		/// </summary>
		/// <param name="parameters">Map of parameter id to value</param>
		/// <param name="scheduledBuild">Whether this is a scheduled build</param>
		/// <param name="arguments">Receives command line arguments for the job</param>
		public abstract void GetArguments(IReadOnlyDictionary<ParameterId, string> parameters, bool scheduledBuild, List<string> arguments);

		/// <summary>
		/// Gets the default arguments for this parameter and its children
		/// </summary>
		/// <param name="parameters">List of default parameters</param>
		/// <param name="scheduledBuild">Whether the arguments are being queried for a scheduled build</param>
		public abstract void GetDefaultParameters(Dictionary<ParameterId, string> parameters, bool scheduledBuild);

		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns>Serializable parameter data</returns>
		public abstract ParameterData ToData();
	}

	/// <summary>
	/// Free-form text entry parameter
	/// </summary>
	public sealed class TextParameter : Parameter
	{
		/// <summary>
		/// Identifier for this parameter
		/// </summary>
		public ParameterId Id { get; set; }

		/// <summary>
		/// Label to display next to this parameter. Should default to the parameter name.
		/// </summary>
		public string Label { get; set; }

		/// <summary>
		/// Argument to add (will have the value of this field appended)
		/// </summary>
		public string Argument { get; set; }

		/// <summary>
		/// Default value for this argument
		/// </summary>
		public string Default { get; set; }

		/// <summary>
		/// Override for this argument in scheduled builds. 
		/// </summary>
		public string? ScheduleOverride { get; set; }

		/// <summary>
		/// Hint text to display when the field is empty
		/// </summary>
		public string? Hint { get; set; }

		/// <summary>
		/// Regex used to validate values entered into this text field.
		/// </summary>
		[BsonIgnoreIfNull]
		public string? Validation { get; set; }

		/// <summary>
		/// Message displayed to explain valid values if validation fails.
		/// </summary>
		[BsonIgnoreIfNull]
		public string? Description { get; set; }

		/// <summary>
		/// Tool tip text to display
		/// </summary>
		public string? ToolTip { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		public TextParameter()
		{
			Label = null!;
			Argument = null!;
			Default = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">Identifier for this parameter</param>
		/// <param name="label">Parameter to pass this value to the BuildGraph script with</param>
		/// <param name="argument">Label to show next to the parameter</param>
		/// <param name="defaultValue">Default value for this argument</param>
		/// <param name="scheduleOverride">Override for this argument in scheduled builds</param>
		/// <param name="hint">Hint text to display</param>
		/// <param name="validation">Regex used to validate entries</param>
		/// <param name="description">Message displayed for invalid values</param>
		/// <param name="toolTip">Tool tip text to display</param>
		public TextParameter(ParameterId id, string label, string argument, string defaultValue, string? scheduleOverride, string? hint, string? validation, string? description, string? toolTip)
		{
			Id = id;
			Label = label;
			Argument = argument;
			Default = defaultValue;
			ScheduleOverride = scheduleOverride;
			Hint = hint;
			Validation = validation;
			Description = description;
			ToolTip = toolTip;
		}

		/// <inheritdoc/>
		public override void GetArguments(IReadOnlyDictionary<ParameterId, string> parameters, bool scheduledBuild, List<string> arguments)
		{
			if (parameters.TryGetValue(Id, out string? value))
			{
				arguments.Add(Argument + value);
			}
			else
			{
				arguments.Add(Argument + (scheduledBuild ? (ScheduleOverride ?? Default) : Default));
			}
		}

		/// <inheritdoc/>
		public override void GetDefaultParameters(Dictionary<ParameterId, string> parameters, bool scheduledBuild)
		{
			parameters[Id] = scheduledBuild ? (ScheduleOverride ?? Default) : Default;
		}

		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns>Serializable parameter data</returns>
		public override ParameterData ToData()
		{
			return new TextParameterData(Id, Label, Argument, Default, ScheduleOverride, Hint, Validation, Description, ToolTip);
		}
	}

	/// <summary>
	/// Possible option for a list parameter
	/// </summary>
	public class ListParameterItem
	{
		/// <summary>
		/// Identifier for this parameter
		/// </summary>
		public ParameterId Id { get; set; }

		/// <summary>
		/// Group to display this entry in
		/// </summary>
		[BsonIgnoreIfNull]
		public string? Group { get; set; }

		/// <summary>
		/// Text to display for this option.
		/// </summary>
		public string Text { get; set; }

		/// <summary>
		/// Argument to add if this parameter is enabled.
		/// </summary>
		[BsonIgnoreIfNull]
		public string? ArgumentIfEnabled { get; set; }

		/// <summary>
		/// Arguments to add if this parameter is enabled.
		/// </summary>
		[BsonIgnoreIfNull]
		public List<string>? ArgumentsIfEnabled { get; set; }

		/// <summary>
		/// Argument to add if this parameter is disabled.
		/// </summary>
		[BsonIgnoreIfNull]
		public string? ArgumentIfDisabled { get; set; }

		/// <summary>
		/// Arguments to add if this parameter is disabled.
		/// </summary>
		[BsonIgnoreIfNull]
		public List<string>? ArgumentsIfDisabled { get; set; }

		/// <summary>
		/// Whether this item is selected by default
		/// </summary>
		[BsonIgnoreIfDefault, BsonDefaultValue(false)]
		public bool Default { get; set; }

		/// <summary>
		/// Whether this item is selected by default
		/// </summary>
		public bool? ScheduleOverride { get; set; }

		/// <summary>
		/// Default constructor for JSON serializer
		/// </summary>
		public ListParameterItem()
		{
			Text = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">Identifier for this parameter</param>
		/// <param name="group">The group to put this parameter in</param>
		/// <param name="text">Text to display for this option</param>
		/// <param name="argumentIfEnabled">Argument to add if this option is enabled</param>
		/// <param name="argumentsIfEnabled">Arguments to add if this option is enabled</param>
		/// <param name="argumentIfDisabled">Argument to add if this option is disabled</param>
		/// <param name="argumentsIfDisabled">Arguments to add if this option is disabled </param>
		/// <param name="defaultValue">Whether this item is selected by default</param>
		/// <param name="scheduleOverride">Override for this value in scheduled builds</param>
		public ListParameterItem(ParameterId id, string? group, string text, string? argumentIfEnabled, List<string>? argumentsIfEnabled, string? argumentIfDisabled, List<string>? argumentsIfDisabled, bool defaultValue, bool? scheduleOverride)
		{
			Id = id;
			Group = group;
			Text = text;
			ArgumentIfEnabled = argumentIfEnabled;
			ArgumentsIfEnabled = argumentsIfEnabled;
			ArgumentIfDisabled = argumentIfDisabled;
			ArgumentsIfDisabled = argumentsIfDisabled;
			Default = defaultValue;
			ScheduleOverride = scheduleOverride;
		}

		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns>Serializable parameter data</returns>
		public ListParameterItemData ToData()
		{
			return new ListParameterItemData(Id, Group, Text, ArgumentIfEnabled, ArgumentsIfEnabled, ArgumentIfDisabled, ArgumentsIfDisabled, Default, ScheduleOverride);
		}
	}

	/// <summary>
	/// Allows the user to select a value from a constrained list of choices
	/// </summary>
	public class ListParameter : Parameter
	{
		/// <summary>
		/// Label to display next to this parameter.
		/// </summary>
		public string Label { get; set; }

		/// <summary>
		/// Style of picker parameter to use
		/// </summary>
		public ListParameterStyle Style { get; set; }

		/// <summary>
		/// List of values to display in the list
		/// </summary>
		public List<ListParameterItem> Items { get; set; }

		/// <summary>
		/// Tool tip text to display
		/// </summary>
		public string? ToolTip { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		public ListParameter()
		{
			Label = null!;
			Items = null!;
		}

		/// <summary>
		/// List of possible values
		/// </summary>
		/// <param name="label">Label to show next to this parameter</param>
		/// <param name="style">Style of list parameter to use</param>
		/// <param name="entries">Entries for this list</param>
		/// <param name="toolTip">Tool tip text to display</param>
		public ListParameter(string label, ListParameterStyle style, List<ListParameterItem> entries, string? toolTip)
		{
			Label = label;
			Style = style;
			Items = entries;
			ToolTip = toolTip;
		}

		/// <inheritdoc/>
		public override void GetArguments(IReadOnlyDictionary<ParameterId, string> parameters, bool scheduledBuild, List<string> arguments)
		{
			foreach (ListParameterItem item in Items)
			{
				bool value = BoolParameter.GetValue(item.Id, parameters) ?? (scheduledBuild ? (item.ScheduleOverride ?? item.Default) : item.Default);
				GetCommandLineArgumentsForItem(item, value, arguments);
			}
		}

		/// <inheritdoc/>
		public override void GetDefaultParameters(Dictionary<ParameterId, string> parameters, bool scheduledBuild)
		{
			foreach (ListParameterItem item in Items)
			{
				bool value = scheduledBuild ? (item.ScheduleOverride ?? item.Default) : item.Default;
				parameters[item.Id] = value.ToString();
			}
		}

		static void GetCommandLineArgumentsForItem(ListParameterItem item, bool value, List<string> arguments)
		{
			if (value)
			{
				if (item.ArgumentIfEnabled != null)
				{
					arguments.Add(item.ArgumentIfEnabled);
				}
				if (item.ArgumentsIfEnabled != null)
				{
					arguments.AddRange(item.ArgumentsIfEnabled);
				}
			}
			else
			{
				if (item.ArgumentIfDisabled != null)
				{
					arguments.Add(item.ArgumentIfDisabled);
				}
				if (item.ArgumentsIfDisabled != null)
				{
					arguments.AddRange(item.ArgumentsIfDisabled);
				}
			}
		}

		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns>Serializable parameter data</returns>
		public override ParameterData ToData()
		{
			return new ListParameterData(Label, Style, Items.ConvertAll(x => x.ToData()), ToolTip);
		}
	}

	/// <summary>
	/// Allows the user to toggle an option on or off
	/// </summary>
	public class BoolParameter : Parameter
	{
		/// <summary>
		/// Identifier for this parameter
		/// </summary>
		public ParameterId Id { get; set; }

		/// <summary>
		/// Label to display next to this parameter.
		/// </summary>
		public string Label { get; set; }

		/// <summary>
		/// Argument to add if this parameter is enabled
		/// </summary>
		[BsonIgnoreIfNull]
		public string? ArgumentIfEnabled { get; set; }

		/// <summary>
		/// Arguments to add if this parameter is enabled
		/// </summary>
		[BsonIgnoreIfNull]
		public List<string>? ArgumentsIfEnabled { get; set; }

		/// <summary>
		/// Argument to add if this parameter is disabled
		/// </summary>
		[BsonIgnoreIfNull]
		public string? ArgumentIfDisabled { get; set; }

		/// <summary>
		/// Arguments to add if this parameter is disabled
		/// </summary>
		[BsonIgnoreIfNull]
		public List<string>? ArgumentsIfDisabled { get; set; }

		/// <summary>
		/// Whether this option should be enabled by default
		/// </summary>
		[BsonIgnoreIfDefault, BsonDefaultValue(false)]
		public bool Default { get; set; }

		/// <summary>
		/// Whether this option should be enabled by default
		/// </summary>
		[BsonIgnoreIfNull]
		public bool? ScheduleOverride { get; set; }

		/// <summary>
		/// Tool tip text to display
		/// </summary>
		public string? ToolTip { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		public BoolParameter()
		{
			Label = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">Identifier for this parameter</param>
		/// <param name="label">Label to display for this parameter</param>
		/// <param name="argumentIfEnabled">Argument to add if this parameter is enabled</param>
		/// <param name="argumentsIfEnabled">Arguments to add if this parameter is enabled</param>
		/// <param name="argumentIfDisabled">Argument to add if this parameter is disabled</param>
		/// <param name="argumentsIfDisabled">Arguments to add if this parameter is disabled</param>
		/// <param name="defaultValue">Default value for this argument</param>
		/// <param name="scheduleOverride">Override for this argument in scheduled builds</param>
		/// <param name="toolTip">Tool tip text to display</param>
		public BoolParameter(ParameterId id, string label, string? argumentIfEnabled, List<string>? argumentsIfEnabled, string? argumentIfDisabled, List<string>? argumentsIfDisabled, bool defaultValue, bool? scheduleOverride, string? toolTip)
		{
			Id = id;
			Label = label;
			ArgumentIfEnabled = argumentIfEnabled;
			ArgumentsIfEnabled = argumentsIfEnabled;
			ArgumentIfDisabled = argumentIfDisabled;
			ArgumentsIfDisabled = argumentsIfDisabled;
			Default = defaultValue;
			ScheduleOverride = scheduleOverride;
			ToolTip = toolTip;
		}

		/// <summary>
		/// Parses a bool parameter value from a dictionary
		/// </summary>
		/// <param name="parameterId">The parameter id</param>
		/// <param name="parameters">Map from parameter id to value</param>
		public static bool? GetValue(ParameterId parameterId, IReadOnlyDictionary<ParameterId, string> parameters)
		{
			if (parameters.TryGetValue(parameterId, out string? stringValue))
			{
				if (stringValue.Equals("0", StringComparison.Ordinal) || stringValue.Equals("false", StringComparison.OrdinalIgnoreCase))
				{
					return false;
				}
				if (stringValue.Equals("1", StringComparison.Ordinal) || stringValue.Equals("true", StringComparison.OrdinalIgnoreCase))
				{
					return true;
				}
			}
			return null;
		}

		/// <inheritdoc/>
		public override void GetArguments(IReadOnlyDictionary<ParameterId, string> parameters, bool scheduledBuild, List<string> arguments)
		{
			bool value = GetValue(Id, parameters) ?? (scheduledBuild ? (ScheduleOverride ?? Default) : Default);
			GetCommandLineArgumentsForItem(value, arguments);
		}

		/// <inheritdoc/>
		public override void GetDefaultParameters(Dictionary<ParameterId, string> parameters, bool scheduledBuild)
		{
			bool value = scheduledBuild ? (ScheduleOverride ?? Default) : Default;
			parameters[Id] = value.ToString();
		}

		void GetCommandLineArgumentsForItem(bool value, List<string> arguments)
		{
			if (value)
			{
				if (!String.IsNullOrEmpty(ArgumentIfEnabled))
				{
					arguments.Add(ArgumentIfEnabled);
				}
				if (ArgumentsIfEnabled != null)
				{
					arguments.AddRange(ArgumentsIfEnabled);
				}
			}
			else
			{
				if (!String.IsNullOrEmpty(ArgumentIfDisabled))
				{
					arguments.Add(ArgumentIfDisabled);
				}
				if (ArgumentsIfDisabled != null)
				{
					arguments.AddRange(ArgumentsIfDisabled);
				}
			}
		}
		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns><see cref="BoolParameterData"/> instance</returns>
		public override ParameterData ToData()
		{
			return new BoolParameterData(Id, Label, ArgumentIfEnabled, ArgumentsIfEnabled, ArgumentIfDisabled, ArgumentsIfDisabled, Default, ScheduleOverride, ToolTip);
		}
	}

	/// <summary>
	/// Document describing a job template. These objects are considered immutable once created and uniquely referenced by hash, in order to de-duplicate across all job runs.
	/// </summary>
	public interface ITemplate
	{
		/// <summary>
		/// Hash of this template
		/// </summary>
		public ContentHash Hash { get; }

		/// <summary>
		/// Name of the template.
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Description for the template
		/// </summary>
		public string? Description { get; }

		/// <summary>
		/// Priority of this job
		/// </summary>
		public Priority? Priority { get; }

		/// <summary>
		/// Whether to allow preflights for this job type
		/// </summary>
		public bool AllowPreflights { get; }

		/// <summary>
		/// Whether to always issues for jobs using this template
		/// </summary>
		public bool UpdateIssues { get; }

		/// <summary>
		/// Whether to promote issues by default for jobs using this template
		/// </summary>
		public bool PromoteIssuesByDefault { get; }

		/// <summary>
		/// Agent type to use for parsing the job state
		/// </summary>
		public string? InitialAgentType { get; }

		/// <summary>
		/// Path to a file within the stream to submit to generate a new changelist for jobs
		/// </summary>
		public string? SubmitNewChange { get; }

		/// <summary>
		/// Description for new changelists
		/// </summary>
		public string? SubmitDescription { get; }

		/// <summary>
		/// Optional predefined user-defined properties for this job
		/// </summary>
		public IReadOnlyList<string> Arguments { get; }

		/// <summary>
		/// Parameters for this template
		/// </summary>
		public IReadOnlyList<Parameter> Parameters { get; }
	}

	/// <summary>
	/// Extension methods for templates
	/// </summary>
	public static class TemplateExtensions
	{
		/// <summary>
		/// Gets the full argument list for a template
		/// </summary>
		public static void GetArgumentsForParameters(this ITemplate template, IReadOnlyDictionary<ParameterId, string> parameters, List<string> arguments)
		{
			arguments.AddRange(template.Arguments);

			foreach (Parameter parameter in template.Parameters)
			{
				parameter.GetArguments(parameters, false, arguments);
			}
		}

		/// <summary>
		/// Gets the arguments for default options in this template. Does not include the standard template arguments.
		/// </summary>
		/// <returns>List of default arguments</returns>
		public static void GetDefaultParameters(this ITemplate template, Dictionary<ParameterId, string> parameters, bool scheduledBuild)
		{
			foreach (Parameter parameter in template.Parameters)
			{
				parameter.GetDefaultParameters(parameters, scheduledBuild);
			}
		}
	}
}
