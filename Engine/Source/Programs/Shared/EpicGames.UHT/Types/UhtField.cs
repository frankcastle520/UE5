// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Represents a UField
	/// </summary>
	public abstract class UhtField : UhtObject
	{
		/// <inheritdoc/>
		public override string EngineClassName => "Field";

		/// <summary>
		/// Name of the module containing the type
		/// </summary>
		public string? VerseModule { get; set; } = null;

		/// <summary>
		/// Cased name of the verse field
		/// </summary>
		public string? VerseName { get; set; } = null;

		/// <summary>
		/// Returns true if field is a verse element
		/// </summary>
		public bool IsVerseField => VerseName != null;

		/// <summary>
		/// Construct a new field
		/// </summary>
		/// <param name="headerFile">Header file being parsed</param>
		/// <param name="outer">Outer object</param>
		/// <param name="lineNumber">Line number of declaration</param>
		protected UhtField(UhtHeaderFile headerFile, UhtType outer, int lineNumber) : base(headerFile, outer, lineNumber)
		{
		}
	}

	/// <summary>
	/// Helper extension methods for fields
	/// </summary>
	public static class UhtFieldStringBuilderExtensions
	{

		/// <summary>
		/// Append the Verse UE VNI package name
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="fieldObj">Field to serialize</param>
		/// <returns>Builder</returns>
		public static StringBuilder AppendVerseUEVNIPackageName(this StringBuilder builder, UhtField fieldObj)
		{
			if (!fieldObj.IsVerseField)
			{
				throw new UhtException(fieldObj, "Attempt to write the Verse VNI package name on a field that isn't part of Verse");
			}
			UhtModule module = fieldObj.Module;
			return builder.Append('/').Append(module.Module.VersePluginName).Append("/_Verse/VNI/").Append(module.Module.Name);
		}

		/// <summary>
		/// Append the Verse UE name 
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="fieldObj">Field to serialize</param>
		/// <returns>Builder</returns>
		public static StringBuilder AppendVerseUEName(this StringBuilder builder, UhtField fieldObj)
		{
			if (!fieldObj.IsVerseField)
			{
				throw new UhtException(fieldObj, "Attempt to write the Verse name on a field that isn't part of Verse");
			}
			return builder.Append(fieldObj.VerseName!);
		}

		/// <summary>
		/// Append the Verse UE name 
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="fieldObj">Field to serialize</param>
		/// <returns>Builder</returns>
		public static StringBuilder AppendVerseUEModuleAndName(this StringBuilder builder, UhtField fieldObj)
		{
			if (fieldObj.VerseModule == null)
			{
				throw new UhtException(fieldObj, "Attempt to write the Verse module on a field doesn't have the module name set");
			}
			return builder.Append(fieldObj.VerseModule!).Append('_').AppendVerseUEName(fieldObj);
		}

		/// <summary>
		/// Append the Verse package name
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="fieldObj">Field to serialize</param>
		/// <returns>Builder</returns>
		public static StringBuilder AppendVersePackageName(this StringBuilder builder, UhtField fieldObj)
		{
			if (!fieldObj.IsVerseField)
			{
				throw new UhtException(fieldObj, "Attempt to write the Verse package name on a field that isn't part of Verse");
			}
			UhtModule module = fieldObj.Module;
			return builder.Append('/').Append(module.Module.VersePluginName).Append("/_Verse/VNI/").Append(module.Module.Name);
		}

		/// <summary>
		/// Append the Verse module name
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="fieldObj">Field to serialize</param>
		/// <returns>Builder</returns>
		public static StringBuilder AppendVerseModuleName(this StringBuilder builder, UhtField fieldObj)
		{
			if (!fieldObj.IsVerseField)
			{
				throw new UhtException(fieldObj, "Attempt to write the Verse module name on a field that isn't part of Verse");
			}
			builder.Append(fieldObj.Package.Module.Module.VersePath);
			if (fieldObj.VerseModule != null)
			{
				builder.Append('/').Append(fieldObj.VerseModule!);
			}
			return builder;
		}
	}
}
