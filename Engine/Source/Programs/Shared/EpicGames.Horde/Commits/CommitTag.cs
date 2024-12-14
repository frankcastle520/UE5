// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text.Json;
using System.Text.Json.Serialization;
using EpicGames.Core;

namespace EpicGames.Horde.Commits
{
	/// <summary>
	/// Constants for known commit tags
	/// </summary>
	[JsonSchemaString]
	[JsonConverter(typeof(CommitTagJsonConverter))]
	public readonly struct CommitTag : IEquatable<CommitTag>
	{
		/// <summary>
		/// Predefined filter name for commits containing code
		/// </summary>
		public static CommitTag Code { get; } = new CommitTag("code");

		/// <summary>
		/// Predefined filter name for commits containing content
		/// </summary>
		public static CommitTag Content { get; } = new CommitTag("content");

		/// <summary>
		/// The tag text
		/// </summary>
		public string Text { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="text"></param>
		public CommitTag(string text)
		{
			Text = text;
			StringId.ValidateArgument(new Utf8String(text), nameof(text));
		}

		/// <summary>
		/// Check if the current tag is empty
		/// </summary>
		public bool IsEmpty() => Text == null;

		/// <inheritdoc/>
		public override bool Equals(object? obj) => obj is CommitTag id && Equals(id);

		/// <inheritdoc/>
		public override int GetHashCode() => String.GetHashCode(Text, StringComparison.Ordinal);

		/// <inheritdoc/>
		public bool Equals(CommitTag other) => String.Equals(Text, other.Text, StringComparison.Ordinal);

		/// <inheritdoc/>
		public override string ToString() => Text;

		/// <summary>
		/// Compares two string ids for equality
		/// </summary>
		/// <param name="left">The first string id</param>
		/// <param name="right">Second string id</param>
		/// <returns>True if the two string ids are equal</returns>
		public static bool operator ==(CommitTag left, CommitTag right) => left.Equals(right);

		/// <summary>
		/// Compares two string ids for inequality
		/// </summary>
		/// <param name="left">The first string id</param>
		/// <param name="right">Second string id</param>
		/// <returns>True if the two string ids are not equal</returns>
		public static bool operator !=(CommitTag left, CommitTag right) => !left.Equals(right);
	}

	/// <summary>
	/// Converts <see cref="CommitTag"/> values to and from JSON
	/// </summary>
	public class CommitTagJsonConverter : JsonConverter<CommitTag>
	{
		/// <inheritdoc/>
		public override CommitTag Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
		{
			return new CommitTag(reader.GetString() ?? String.Empty);
		}

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter writer, CommitTag value, JsonSerializerOptions options)
		{
			writer.WriteStringValue(value.Text);
		}
	}
}
