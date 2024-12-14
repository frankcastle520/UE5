// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "HarmonixMidi/TempoMap.h"
#include "HarmonixMidi/BarMap.h"
#include "HarmonixMidi/BeatMap.h"
#include "HarmonixMidi/ChordMap.h"
#include "HarmonixMidi/SectionMap.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include <limits>

#include "SongMaps.generated.h"

class IMidiReader;
class FStdMidiFileReader;
class FSongMapReceiver;
class UMidiFile;
struct FMidiFileData;

namespace HarmonixMetasound 
{
	class FMidiClock;
}

UENUM()
enum class EMidiFileQuantizeDirection : uint8
{
	Nearest,
	Up,
	Down
};

UENUM(BlueprintType)
enum class EMidiClockSubdivisionQuantization : uint8
{
	Bar = static_cast<uint8>(EQuartzCommandQuantization::Bar),
	Beat = static_cast<uint8>(EQuartzCommandQuantization::Beat),
	ThirtySecondNote = static_cast<uint8>(EQuartzCommandQuantization::ThirtySecondNote),
	SixteenthNote = static_cast<uint8>(EQuartzCommandQuantization::SixteenthNote),
	EighthNote = static_cast<uint8>(EQuartzCommandQuantization::EighthNote),
	QuarterNote = static_cast<uint8>(EQuartzCommandQuantization::QuarterNote),
	HalfNote = static_cast<uint8>(EQuartzCommandQuantization::HalfNote),
	WholeNote = static_cast<uint8>(EQuartzCommandQuantization::WholeNote),
	DottedSixteenthNote = static_cast<uint8>(EQuartzCommandQuantization::DottedSixteenthNote),
	DottedEighthNote = static_cast<uint8>(EQuartzCommandQuantization::DottedEighthNote),
	DottedQuarterNote = static_cast<uint8>(EQuartzCommandQuantization::DottedQuarterNote),
	DottedHalfNote = static_cast<uint8>(EQuartzCommandQuantization::DottedHalfNote),
	DottedWholeNote = static_cast<uint8>(EQuartzCommandQuantization::DottedWholeNote),
	SixteenthNoteTriplet = static_cast<uint8>(EQuartzCommandQuantization::SixteenthNoteTriplet),
	EighthNoteTriplet = static_cast<uint8>(EQuartzCommandQuantization::EighthNoteTriplet),
	QuarterNoteTriplet = static_cast<uint8>(EQuartzCommandQuantization::QuarterNoteTriplet),
	HalfNoteTriplet = static_cast<uint8>(EQuartzCommandQuantization::HalfNoteTriplet),
	None = static_cast<uint8>(EQuartzCommandQuantization::None)
};

USTRUCT()
struct FSongLengthData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 LengthTicks = 0;
	UPROPERTY()
	float LengthFractionalBars = 0.0f;
	UPROPERTY()
	int32 LastTick = 0;

	bool operator==(const FSongLengthData& Other) const
	{
		return	LengthTicks == Other.LengthTicks &&
				LengthFractionalBars == Other.LengthFractionalBars && 
				LastTick == Other.LastTick;
	}
};

struct HARMONIXMIDI_API ISongMapEvaluator
{
public:
	int32 GetTicksPerQuarterNote() const;
	float TickToMs(float Tick) const;
	float MsToTick(float Ms) const;
	float GetCountInSeconds() const;

	// tempo
	const FTempoInfoPoint* GetTempoInfoForMs(float Ms) const;
	const FTempoInfoPoint* GetTempoInfoForTick(int32 Tick) const;
	int32 GetTempoPointIndexForTick(int32 Tick) const;
	const FTempoInfoPoint* GetTempoInfoPoint(int32 PointIndex) const;
	int32 GetNumTempoChanges() const;
	int32 GetTempoChangePointTick(int32 PointIndex) const;
	float GetTempoAtMs(float Ms) const;
	float GetTempoAtTick(int32 Tick) const;
	bool TempoMapIsEmpty() const;

	// beats
	const FBeatMapPoint* GetBeatAtMs(float Ms) const;
	float GetMsAtBeat(float Beat) const;
	const FBeatMapPoint* GetBeatAtTick(int32 Tick) const;
	float GetMsPerBeatAtMs(float Ms) const;
	float GetMsPerBeatAtTick(int32 Tick) const;
	float GetFractionalBeatAtMs(float Ms) const;
	float GetFractionalBeatAtTick(float Tick) const;
	int32 GetBeatIndexAtMs(float Ms) const;
	int32 GetBeatIndexAtTick(int32 Tick) const;
	EMusicalBeatType GetBeatTypeAtMs(float Ms) const;
	EMusicalBeatType GetBeatTypeAtTick(int32 Tick) const;
	const FBeatMapPoint* GetBeatPointInfoAtTick(int32 Tick, int32* PointIndex = nullptr) const;

	float GetBeatInPulseBarAtMs(float Ms) const;
	float GetBeatInPulseBarAtTick(float Tick) const;
	int32 GetNumBeatsInPulseBarAtMs(float Ms) const;
	int32 GetNumBeatsInPulseBarAtTick(int32 Tick) const;
	bool BeatMapIsEmpty() const;

	// bars
	int32 GetStartBar() const;
	int32 GetNumTimeSignatureChanges() const;
	const FTimeSignature* GetTimeSignatureAtMs(float Ms) const;
	const FTimeSignature* GetTimeSignatureAtTick(int32 Tick) const;
	int32 GetTimeSignaturePointIndexForTick(int32 Tick) const;
	const FTimeSignature* GetTimeSignatureAtBar(int32 Bar) const;
	const FTimeSignaturePoint* GetTimeSignaturePointAtTick(int32 Tick) const;
	const FTimeSignaturePoint* GetTimeSignaturePoint(int32 PointIndex) const;
	int32 GetTimeSignatureChangePointTick(int32 PointIndex);
	float GetBarIncludingCountInAtMs(float Ms) const;
	float GetBarIncludingCountInAtTick(float Tick) const;
	float GetMsPerBarAtMs(float Ms) const;
	float GetMsPerBarAtTick(float Tick) const;
	bool BarMapIsEmpty() const;
	FMusicTimestamp TickToMusicTimestamp(float Tick, int32* OutBeatsPerBar = nullptr) const;
	int32 BarIncludingCountInToTick(int32 BarIndex, int32* OutBeatsPerBar = nullptr, int32* OutTicksPerBeat = nullptr) const;
	int32 BarBeatTickIncludingCountInToTick(int32 BarIndex, int32 BeatInBar, int32 TickInBeat) const;
	float FractionalBarIncludingCountInToTick(float FractionalBarIndex) const;
	int32 TickToBarIncludingCountIn(int32 Tick) const;
	float TickToFractionalBarIncludingCountIn(float Tick) const;
	void TickToBarBeatTickIncludingCountIn(int32 RawTick, int32& OutBarIndex, int32& OutBeatInBarIndex, int32& OutTickIndexInBeat, int32* OutBeatsPerBar = nullptr, int32* OutTicksPerBeat = nullptr) const;
	int32 CalculateMidiTick(const FMusicTimestamp& Timestamp, const EMidiClockSubdivisionQuantization Quantize) const;
	int32 SubdivisionToMidiTicks(const EMidiClockSubdivisionQuantization Division, const int32 AtTick) const;
	float MusicTimestampToTick(const FMusicTimestamp& Timestamp) const;
	int32 MusicTimestampBarToTick(int32 BarNumber, int32* OutBeatsPerBar = nullptr, int32* OutTicksPerBeat = nullptr) const;

	// sections
	const TArray<FSongSection>& GetSections() const;
	int32 GetNumSections() const;
	float GetSectionStartMsAtMs(float Ms) const;
	float GetSectionEndMsAtMs(float Ms) const;
	const FSongSection* GetSectionAtMs(float Ms) const;
	const FSongSection* GetSectionAtTick(int32 Tick) const;
	int32 GetSectionIndexAtTick(int32 Tick) const;
	const FSongSection* GetSectionWithName(const FString& Name) const;
	FString GetSectionNameAtMs(float Ms) const;
	FString GetSectionNameAtTick(int32 Tick) const;
	float GetSectionLengthMsAtMs(float Ms) const;
	float GetSectionLengthMsAtTick(int32 Tick) const;
	bool SectionMapIsEmpty() const;

	// chords
	const FChordMapPoint* GetChordAtMs(float Ms) const;
	const FChordMapPoint* GetChordAtTick(int32 Tick) const;
	FName GetChordNameAtMs(float Ms) const;
	FName GetChordNameAtTick(int32 Tick) const;
	float GetChordLengthMsAtMs(float Ms) const;
	float GetChordLengthMsAtTick(int32 Tick) const;
	bool ChordMapIsEmpty() const;

	// length
	float GetSongLengthMs() const;
	int32 GetSongLengthBeats() const;
	float GetSongLengthFractionalBars() const;
	bool LengthIsAPerfectSubdivision() const;
	FString GetSongLengthString() const;

	int32 QuantizeTickToAnyNearestSubdivision(int32 InTick, EMidiFileQuantizeDirection Direction, EMidiClockSubdivisionQuantization& Division) const;
	int32 QuantizeTickToNearestSubdivision(int32 InTick, EMidiFileQuantizeDirection Direction, EMidiClockSubdivisionQuantization Division) const;
	void GetTicksForNearestSubdivision(int32 InTick, EMidiClockSubdivisionQuantization Division, int32& LowerTick, int32& UpperTick) const;

	//virtual FSongLengthData& GetSongLengthData() = 0;
	virtual const FSongLengthData& GetSongLengthData() const = 0;

protected:
	friend struct FSongMaps;
	friend struct FSongMapsWithAlternateTempoSource;
	virtual const FTempoMap& GetTempoMap() const = 0;
	virtual const FBeatMap& GetBeatMap() const = 0;
	virtual const FBarMap& GetBarMap() const = 0;
	virtual const FSectionMap& GetSectionMap() const = 0;
	virtual const FChordProgressionMap& GetChordMap() const = 0;
};


/**
 * FSongMaps encapsulates a number of other musical/midi map types
 * that are very useful for musical gameplay and interactivity. 
 * 
 * With this class and the current playback position of a piece of music you can
 * do things like determine the current Bar | Beat | Tick, song section, tempo,
 * chord, etc.
 */
USTRUCT(BlueprintType)
struct HARMONIXMIDI_API FSongMaps 
#if CPP
	: public ISongMapEvaluator
#endif
{
	GENERATED_BODY()

public:
	FSongMaps();
	FSongMaps(float Bpm, int32 TimeSigNumerator, int32 TimeSigDenominator);
	FSongMaps(const ISongMapEvaluator& Other);
	virtual ~FSongMaps() = default;

	bool operator==(const FSongMaps& Other) const;

	void Init(int32 InTicksPerQuarterNote);
	void Copy(const ISongMapEvaluator& Other, int32 StartTick = 0, int32 EndTick = std::numeric_limits<int32>::max());

	// For importing...
	bool LoadFromStdMidiFile(const FString& FilePath);
	bool LoadFromStdMidiFile(void* Buffer, int32 BufferSize, const FString& Filename);
	bool LoadFromStdMidiFile(TSharedPtr<FArchive> Archive, const FString& Filename);

	// tracks
	TArray<FString>& GetTrackNames() { return TrackNames; }
	const TArray<FString>& GetTrackNames() const { return TrackNames; }
	FString GetTrackName(int32 Index) const;
	bool TrackNamesIsEmpty() const { return TrackNames.IsEmpty(); }
	void EmptyTrackNames() { TrackNames.Empty(); }

	void EmptyTempoMap() { TempoMap.Empty(); }
	void EmptyBeatMap() { BeatMap.Empty(); }
	void EmptyBarMap() { BarMap.Empty(); }
	void SetLengthTotalBars(int32 Bars);
	void EmptySectionMap() { SectionMap.Empty(); }
	void EmptyChordMap() { ChordMap.Empty(); }

	void EmptyAllMaps();
	bool IsEmpty() const;

	// BEGIN ISongMapEvaluator overrides (Immutable access to maps)
	virtual const FTempoMap& GetTempoMap() const override { return TempoMap; }
	virtual const FBeatMap& GetBeatMap() const override { return BeatMap; }
	virtual const FBarMap& GetBarMap() const override { return BarMap; }
	virtual const FSectionMap& GetSectionMap() const override { return SectionMap; }
	virtual const FChordProgressionMap& GetChordMap() const override { return ChordMap; }
	virtual const FSongLengthData& GetSongLengthData() const override { return LengthData; }
	// END ISongMapEvaluator overrides

	FTempoMap& GetTempoMap() { return TempoMap; }
	FBeatMap& GetBeatMap() { return BeatMap; }
	FBarMap& GetBarMap() { return BarMap; }
	FSectionMap& GetSectionMap() { return SectionMap; }
	FChordProgressionMap& GetChordMap() { return ChordMap; }
	FSongLengthData& GetSongLengthData() { return LengthData; }

	int32 GetTicksPerQuarterNote() const { return TicksPerQuarterNote; }

	void SetStartBar(int32 StartBar);
	void SetSongLengthTicks(int32 NewLengthTicks);
	void FinalizeBarMap(int32 InLastTick);

	void AddTempoChange(int32 Tick, float TempoBPM);
	void AddTimeSigChange(int32 Tick, int32 TimeSigNum, int32 TimeSigDenom);

	bool AddTempoInfoPoint(int32 MicrosecondsPerQuarterNote, int32 Tick, bool SortNow = true);
	bool AddTimeSignatureAtBarIncludingCountIn(int32 BarIndex, int32 InNumerator, int32 InDenominator, bool SortNow = true, bool FailOnError = true);
	FTimeSignaturePoint* GetMutableTimeSignaturePoint(int32 PointIndex);

protected:
	friend class FSongMapReceiver;
	friend class HarmonixMetasound::FMidiClock;

	UPROPERTY()
	int32 TicksPerQuarterNote = Harmonix::Midi::Constants::GTicksPerQuarterNoteInt;
	UPROPERTY()
	FTempoMap TempoMap;
	UPROPERTY()
	FBarMap BarMap;
	UPROPERTY()
	FBeatMap BeatMap;
	UPROPERTY()
	FSectionMap SectionMap;
	UPROPERTY()
	FChordProgressionMap ChordMap;
	UPROPERTY()
	TArray<FString>	TrackNames;

private:
	UPROPERTY()
	FSongLengthData LengthData;


	void StringLengthToMT(const FString& LengthString, int32& OutBars, int32& OutTicks);
	bool ReadWithReader(FStdMidiFileReader& Reader);
	bool FinalizeRead(IMidiReader* Reader);
};

struct HARMONIXMIDI_API FSongMapsWithAlternateTempoSource : public ISongMapEvaluator
{
public:
	FSongMapsWithAlternateTempoSource(const TSharedPtr<const ISongMapEvaluator>& SongMapsWithTempo, const TSharedPtr<const ISongMapEvaluator>& SongMapsWithOthers)
		: SongMapsWithTempoMap(SongMapsWithTempo)
		, SongMapsWithOtherMaps(SongMapsWithOthers)
	{}

	FSongMapsWithAlternateTempoSource(const TSharedPtr<const ISongMapEvaluator>& SongMaps)
		: SongMapsWithTempoMap(SongMaps)
		, SongMapsWithOtherMaps(SongMaps)
	{
	}

	FSongMapsWithAlternateTempoSource& operator=(const TSharedPtr<const FSongMapsWithAlternateTempoSource>& Other)
	{
		SongMapsWithTempoMap = Other->SongMapsWithTempoMap;
		SongMapsWithOtherMaps = Other->SongMapsWithOtherMaps;
		return *this;
	}

	virtual ~FSongMapsWithAlternateTempoSource() = default;

	operator bool() const 
	{
		return SongMapsWithTempoMap.IsValid() && SongMapsWithOtherMaps.IsValid();
	}

	const TSharedPtr<const ISongMapEvaluator>& GetSongMapsWithTempoMap() const { return SongMapsWithTempoMap; }
	const TSharedPtr<const ISongMapEvaluator>& GetSongMapsWithOtherMaps() const { return SongMapsWithOtherMaps; }

	bool AllMapsHaveOneSource() const { return SongMapsWithTempoMap == SongMapsWithOtherMaps; }

protected:
	virtual const FTempoMap& GetTempoMap() const override;
	virtual const FBeatMap& GetBeatMap() const override;
	virtual const FBarMap& GetBarMap() const override;
	virtual const FSectionMap& GetSectionMap() const override;
	virtual const FChordProgressionMap& GetChordMap() const override;
	virtual const FSongLengthData& GetSongLengthData() const override;

	TSharedPtr<const ISongMapEvaluator> SongMapsWithTempoMap;
	TSharedPtr<const ISongMapEvaluator> SongMapsWithOtherMaps;
};
