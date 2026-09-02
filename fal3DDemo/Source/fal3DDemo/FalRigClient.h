#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "FalRigClient.generated.h"

class UFalQueueRequest;
class FJsonObject;

UENUM(BlueprintType)
enum class EFalRigState : uint8
{
	Idle,
	Submitting,
	Polling,
	Completed,
	Error
};

// GLB URLs for one rigged character and its animation clips.
// Field names double as the keys in Saved/CharacterHistory.json, so keep them stable.
USTRUCT()
struct FRiggedCharacterUrls
{
	GENERATED_BODY()

	FString RiggedGlbUrl;
	FString WalkAnimGlbUrl;
	FString RunAnimGlbUrl;
	FString IdleAnimGlbUrl;
	FString JumpAnimGlbUrl;
	FString FallAnimGlbUrl;
	FString SprintAnimGlbUrl;
	FString BoxingAnimGlbUrl;
	FString KickAnimGlbUrl;
	FString PunchAnimGlbUrl;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRiggingComplete, const FRiggedCharacterUrls&, Urls, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRigStateChanged, EFalRigState, NewState);

/**
 * Rigs a humanoid GLB and generates every animation clip the character needs in a single
 * fal.ai request: fal-ai/meshy/rigging/multi-animation.
 */
UCLASS()
class FAL3DDEMO_API UFalRigClient : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnRiggingComplete OnRiggingComplete;

	UPROPERTY(BlueprintAssignable)
	FOnRigStateChanged OnStateChanged;

	UPROPERTY(BlueprintReadOnly)
	EFalRigState CurrentState = EFalRigState::Idle;

	UPROPERTY(BlueprintReadOnly)
	FString StatusMessage;

	void RigAndAnimate(const FString& GlbUrl);

	bool IsRigging() const;

private:
	UPROPERTY()
	UFalQueueRequest* Request = nullptr;

	static const FString MultiAnimationUrl;
	static const float CharacterHeightMeters;

	void HandleProgress(const FString& Message);
	void HandleResult(TSharedPtr<FJsonObject> Result, const FString& Error);

	void SetState(EFalRigState NewState, const FString& Message);
	void Fail(const FString& Error);
};
