// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.Telemetry
{
	/// <summary>
	/// Wrapper around native telemetry objects
	/// </summary>
	public class TelemetryEvent
	{
		/// <summary>
		/// Record metadata
		/// </summary>
		public TelemetryRecordMeta RecordMeta { get; set; }

		/// <summary>
		/// Accessor for the native object
		/// </summary>
		public object Payload { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public TelemetryEvent(TelemetryRecordMeta recordMeta, object payload)
		{
			RecordMeta = recordMeta;
			Payload = payload;
		}
	}
}
