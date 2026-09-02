#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "FalApiClient.generated.h"

class UFalQueueRequest;
class FJsonObject;

UENUM(BlueprintType)
enum class EFalGenerationState : uint8
{
	Idle,
	Submitting,
	Polling,
	Completed,
	Error
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGenerationComplete, const FString&, GlbUrl, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGenerationStateChanged, EFalGenerationState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConceptImageReady, const FString&, ImageUrl);

/**
 * Generates a textured, rig-friendly 3D character through the fal.ai queue.
 *
 *   Text  : fal-ai/nano-banana-2  ->  meshy/v7/image-to-3d
 *           The text prompt is first turned into a clean front-facing concept image in an
 *           explicit A-pose or T-pose on a white background. Image conditioning gives Meshy a
 *           far stronger pose signal than text alone, which is what the auto-rigger needs.
 *   Image : meshy/v7/image-to-3d  (user-supplied photo, sent as a data URI)
 *
 * Both paths also pass Meshy's pose_mode so the mesh comes out in the requested pose.
 */
UCLASS()
class FAL3DDEMO_API UFalApiClient : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnGenerationComplete OnGenerationComplete;

	UPROPERTY(BlueprintAssignable)
	FOnGenerationStateChanged OnStateChanged;

	// Fired on the text path once the concept image exists, before the 3D step starts.
	UPROPERTY(BlueprintAssignable)
	FOnConceptImageReady OnConceptImageReady;

	UPROPERTY(BlueprintReadOnly)
	EFalGenerationState CurrentState = EFalGenerationState::Idle;

	UPROPERTY(BlueprintReadOnly)
	FString StatusMessage;

	void GenerateModel(const FString& Prompt, bool bTPose);
	void GenerateModelFromImage(const FString& LocalImagePath, bool bTPose);

	bool IsGenerating() const;

private:
	UPROPERTY()
	UFalQueueRequest* Request = nullptr;

	static const FString ConceptImageUrl;
	static const FString ImageTo3DUrl;

	// Mesh quality knobs.
	static const int32 TargetPolycount;
	static const bool bUltraMode;
	static const bool bEnablePbr;

	// Pose requested for the run currently in flight (needed across the two text-path stages).
	bool bPendingTPose = false;

	// Shown in front of progress messages, e.g. "Designing character" / "Generating 3D model".
	FString StageLabel;

	// Validates the API key and in-progress state, creating the request object on first use.
	bool BeginRequest();

	static FString BuildConceptPrompt(const FString& CharacterPrompt, bool bTPose);
	void AddMeshSettings(const TSharedRef<FJsonObject>& Body, bool bTPose) const;

	// Stage 1 (text path): concept image.
	void SubmitConceptImage(const FString& Prompt);
	void HandleConceptResult(TSharedPtr<FJsonObject> Result, const FString& Error);

	// Stage 2 (both paths): image -> 3D. ImageUrl may be an https URL or a data URI.
	void SubmitImageTo3D(const FString& ImageUrl, const FString& SubmitMessage);
	void HandleModelResult(TSharedPtr<FJsonObject> Result, const FString& Error);

	void HandleProgress(const FString& Message);

	void SetState(EFalGenerationState NewState, const FString& Message);
	void Fail(const FString& Error);
};
