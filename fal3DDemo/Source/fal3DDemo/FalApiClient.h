#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "FalApiClient.generated.h"

UENUM(BlueprintType)
enum class EFalGenerationState : uint8
{
	Idle,
	Submitting,
	Polling,
	FetchingResult,
	Completed,
	Error
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGenerationComplete, const FString&, GlbUrl, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGenerationStateChanged, EFalGenerationState, NewState);

UCLASS()
class FAL3DDEMO_API UFalApiClient : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnGenerationComplete OnGenerationComplete;

	UPROPERTY(BlueprintAssignable)
	FOnGenerationStateChanged OnStateChanged;

	UPROPERTY(BlueprintReadOnly)
	EFalGenerationState CurrentState = EFalGenerationState::Idle;

	UPROPERTY(BlueprintReadOnly)
	FString StatusMessage;

	// Texture URL from the last generation result
	FString LastTextureUrl;

	void GenerateModel(const FString& Prompt);

private:
	FString ApiKey;
	FString RequestId;
	FString StatusUrl;
	FString ResponseUrl;
	FTimerHandle PollTimerHandle;

	static const FString BaseUrl;

	FString GetApiKey();
	void SetState(EFalGenerationState NewState, const FString& Message = TEXT(""));

	void SubmitRequest(const FString& Prompt);
	void OnSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	void StartPolling();
	void PollStatus();
	void OnPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	void FetchResult();
	void OnFetchResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	void StopPolling();
};
