#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "MeshyRigClient.generated.h"

UENUM(BlueprintType)
enum class EMeshyRigState : uint8
{
	Idle,
	RiggingSubmitting,
	RiggingPolling,
	AnimationsSubmitting,
	AnimationsPolling,
	Completed,
	Error
};

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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRigStateChanged, EMeshyRigState, NewState);

UCLASS()
class FAL3DDEMO_API UMeshyRigClient : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnRiggingComplete OnRiggingComplete;

	UPROPERTY(BlueprintAssignable)
	FOnRigStateChanged OnStateChanged;

	UPROPERTY(BlueprintReadOnly)
	EMeshyRigState CurrentState = EMeshyRigState::Idle;

	UPROPERTY(BlueprintReadOnly)
	FString StatusMessage;

	void RigAndAnimate(const FString& GlbUrl, const FString& TextureUrl = TEXT(""));

private:
	FString ApiKey;
	FString RigTaskId;
	FString CachedTextureUrl;
	FRiggedCharacterUrls ResultUrls;
	FTimerHandle PollTimerHandle;

	// Animation task tracking
	struct FAnimTask
	{
		FString TaskId;
		int32 ActionId;
		bool bCompleted = false;
		FString ResultGlbUrl;
	};
	TArray<FAnimTask> AnimTasks;

	static const FString BaseUrl;

	FString GetApiKey();
	void SetState(EMeshyRigState NewState, const FString& Message = TEXT(""));

	// Rigging
	void SubmitRigging(const FString& GlbUrl);
	void OnRiggingSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void StartRiggingPoll();
	void PollRigging();
	void OnRiggingPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	// Animations
	void SubmitAnimations();
	void SubmitSingleAnimation(int32 ActionId);
	void OnAnimationSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, int32 ActionId);
	void StartAnimationPoll();
	void PollAnimations();
	void OnAnimationPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, int32 ActionId);
	bool AllAnimationsComplete() const;

	void StopPolling();

	UWorld* FindWorld() const;
};
