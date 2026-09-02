#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "FalQueueRequest.generated.h"

class FJsonObject;

// Fired exactly once per request. Either Result is valid and Error is empty,
// or Result is null and Error holds a human-readable reason.
DECLARE_DELEGATE_TwoParams(FOnFalQueueComplete, TSharedPtr<FJsonObject> /*Result*/, const FString& /*Error*/);

// Fired whenever the progress message changes while the request is queued or running.
DECLARE_DELEGATE_OneParam(FOnFalQueueProgress, const FString& /*Message*/);

/**
 * One submit -> poll -> fetch cycle against the fal.ai queue API
 * (https://queue.fal.run). Every fal endpoint the project uses goes through
 * this class, so authentication, polling and error extraction live in one place.
 *
 * Outer must be (or be owned by) an actor so a world timer is available for polling.
 */
UCLASS()
class FAL3DDEMO_API UFalQueueRequest : public UObject
{
	GENERATED_BODY()

public:
	// Loads FAL_KEY from <ProjectDir>/.env, falling back to the OS environment. Cached after first call.
	static FString GetApiKey();

	// Submits Body to EndpointUrl and drives the request to completion.
	void Start(const FString& EndpointUrl, const TSharedRef<FJsonObject>& Body,
		FOnFalQueueComplete OnComplete, FOnFalQueueProgress OnProgress = FOnFalQueueProgress());

	// Stops polling and drops any in-flight callbacks. OnComplete is not fired.
	void Cancel();

	bool IsActive() const { return bActive; }

private:
	FString StatusUrl;
	FString ResponseUrl;
	FString LastProgressMessage;
	FTimerHandle PollTimerHandle;
	bool bActive = false;

	// Incremented on every Start/Cancel/Finish so stale HTTP callbacks are ignored.
	int32 Generation = 0;

	FOnFalQueueComplete CompleteDelegate;
	FOnFalQueueProgress ProgressDelegate;

	static FString CachedApiKey;
	static const float PollIntervalSeconds;

	UWorld* FindWorld() const;

	void SendRequest(const FString& Url, const FString& Verb, const FString& Body,
		TFunction<void(FHttpResponsePtr, bool)> Handler);

	void OnSubmitResponse(FHttpResponsePtr Response, bool bConnected);
	void PollStatus();
	void OnStatusResponse(FHttpResponsePtr Response, bool bConnected);
	void FetchResult();
	void OnResultResponse(FHttpResponsePtr Response, bool bConnected);

	void StopPolling();
	void ReportProgress(const FString& Message);
	void Finish(TSharedPtr<FJsonObject> Result, const FString& Error);

	static TSharedPtr<FJsonObject> ParseJson(const FString& Body);

	// Pulls the most useful message out of a fal error payload:
	// {"detail":"..."}, {"detail":[{"msg":"..."}]} or {"message":"..."}.
	static FString ExtractErrorDetail(const TSharedPtr<FJsonObject>& Json, int32 ResponseCode);
};
