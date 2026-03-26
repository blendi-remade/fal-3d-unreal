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
	// Image preprocessing states
	PreprocessingSubmitting,
	PreprocessingPolling,
	PreprocessingFetching,
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

	// Text-to-3D generation
	void GenerateModel(const FString& Prompt);

	// Image-to-3D generation (preprocesses with nano-banana-pro/edit first)
	void GenerateModelFromImage(const FString& LocalImagePath);

private:
	FString ApiKey;
	FString RequestId;
	FString StatusUrl;
	FString ResponseUrl;
	FTimerHandle PollTimerHandle;

	// Preprocessing state (nano-banana-pro/edit)
	FString PreprocessRequestId;
	FString PreprocessStatusUrl;
	FString PreprocessResponseUrl;
	FTimerHandle PreprocessPollTimerHandle;

	static const FString TextTo3DUrl;
	static const FString ImageTo3DUrl;
	static const FString NanoBananaEditUrl;

	FString GetApiKey();
	void SetState(EFalGenerationState NewState, const FString& Message = TEXT(""));

	bool IsGenerating() const;

	// Text-to-3D flow
	void SubmitRequest(const FString& Prompt);
	void OnSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	void StartPolling();
	void PollStatus();
	void OnPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	void FetchResult();
	void OnFetchResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	void StopPolling();

	// Image preprocessing flow (nano-banana-pro/edit)
	void SubmitPreprocessing(const FString& Base64DataUrl);
	void OnPreprocessSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	void StartPreprocessPolling();
	void PollPreprocessStatus();
	void OnPreprocessPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	void FetchPreprocessResult();
	void OnPreprocessFetchResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	void StopPreprocessPolling();

	// Image-to-3D flow (uses same poll/fetch as text-to-3D after submit)
	void SubmitImageTo3D(const FString& ImageUrl);
	void OnImageTo3DSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
};
