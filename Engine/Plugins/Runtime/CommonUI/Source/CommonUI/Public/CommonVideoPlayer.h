// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/Widget.h"

#include "CommonVideoPlayer.generated.h"

class SImage;
class UMaterial;
class UMediaSource;
class UMediaPlayer;
class UMediaTexture;
class UMediaSoundComponent;
class USoundClass;

enum class EMediaEvent;

UCLASS(ClassGroup = UI, meta = (Category = "Common UI"))
class COMMONUI_API UCommonVideoPlayer : public UWidget
{
	GENERATED_BODY()

public:
	UCommonVideoPlayer(const FObjectInitializer& Initializer);
	virtual void PostInitProperties() override;

	UFUNCTION(BlueprintCallable, Category="Video Player")
	void SetVideo(UMediaSource* NewVideo);
	UFUNCTION(BlueprintCallable, Category="Video Player")
	void Seek(float PlaybackTime);
	UFUNCTION(BlueprintCallable, Category="Video Player")
	void Close();

	UFUNCTION(BlueprintCallable, Category="Video Player")
	void SetPlaybackRate(float PlaybackRate);
	UFUNCTION(BlueprintCallable, Category="Video Player")
	void SetLooping(bool bShouldLoopPlayback);
	UFUNCTION(BlueprintCallable, Category="Video Player")
	void SetIsMuted(bool bInIsMuted);
	UFUNCTION(BlueprintCallable, Category="Video Player")
	void SetShouldMatchSize(bool bInMatchSize);

	UFUNCTION(BlueprintCallable, Category="Video Player")
	void Play();
	UFUNCTION(BlueprintCallable, Category="Video Player")
	void Reverse();
	UFUNCTION(BlueprintCallable, Category="Video Player")
	void Pause();
	UFUNCTION(BlueprintCallable, Category="Video Player")
	void PlayFromStart();

	UFUNCTION(BlueprintCallable, Category="Video Player")
	float GetVideoDuration() const;
	UFUNCTION(BlueprintCallable, Category="Video Player")
	float GetPlaybackTime() const;
	UFUNCTION(BlueprintCallable, Category="Video Player")
	float GetPlaybackRate() const;

	UFUNCTION(BlueprintCallable, Category="Video Player")
	bool IsLooping() const;
	UFUNCTION(BlueprintCallable, Category="Video Player")
	bool IsPaused() const;
	UFUNCTION(BlueprintCallable, Category="Video Player")
	bool IsPlaying() const;
	UFUNCTION(BlueprintCallable, Category="Video Player")
	bool IsMuted() const { return bIsMuted; }

	FSimpleMulticastDelegate& OnPlaybackResumed() { return OnPlaybackResumedEvent; }
	FSimpleMulticastDelegate& OnPlaybackPaused() { return OnPlaybackPausedEvent; }
	FSimpleMulticastDelegate& OnPlaybackComplete() { return OnPlaybackCompleteEvent; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	void PlayInternal() const;
	const UMediaPlayer& GetMediaPlayer() const { return *MediaPlayer; }
	virtual void HandleMediaPlayerEvent(EMediaEvent EventType);
	virtual void PlaybackTick(double InCurrentTime, float InDeltaTime);

private:
	EActiveTimerReturnType HandlePlaybackTick(double InCurrentTime, float InDeltaTime);

private:
	UPROPERTY(EditAnywhere, Category = VideoPlayer)
	TObjectPtr<UMediaSource> Video;

	// Should we match the size of the media source when it opens?
	UPROPERTY(EditAnywhere, Category = VideoPlayer)
	bool bMatchSize = false;

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> MediaPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> MediaTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterial> VideoMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMediaSoundComponent> SoundComponent;

	UPROPERTY(Transient)
	FSlateBrush VideoBrush;

	mutable FSimpleMulticastDelegate OnPlaybackResumedEvent;
	mutable FSimpleMulticastDelegate OnPlaybackPausedEvent;
	mutable FSimpleMulticastDelegate OnPlaybackCompleteEvent;


	bool bIsMuted = false;
	TSharedPtr<SImage> MyImage;
};
