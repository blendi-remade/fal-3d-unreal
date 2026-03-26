// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "FalApiClient.h"
#include "MeshyRigClient.h"
#include "fal3DDemoCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UFalGeneratorWidget;
class UglTFRuntimeAsset;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UENUM()
enum class ERuntimeMovementState : uint8
{
	Idle,
	Walk,
	Run,
	Sprint,
	Jump,
	Fall
};

UCLASS(config=Game)
class Afal3DDemoCharacter : public ACharacter
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* TogglePanelAction;

public:
	Afal3DDemoCharacter();

protected:
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

protected:
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

public:
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

private:
	// fal.ai generator panel
	UPROPERTY()
	UFalGeneratorWidget* GeneratorWidget;

	UPROPERTY()
	UFalApiClient* FalClient;

	UPROPERTY()
	UMeshyRigClient* MeshyClient;

	bool bPanelVisible = false;

	void ToggleGeneratorPanel();
	void ShowPanel();
	void HidePanel();

	UFUNCTION()
	void OnGenerateRequested(const FString& Prompt);

	UFUNCTION()
	void OnImageGenerateRequested(const FString& ImagePath);

	UFUNCTION()
	void OnCloseRequested();

	UFUNCTION()
	void OnGenerationStateChanged(EFalGenerationState NewState);

	UFUNCTION()
	void OnGenerationComplete(const FString& GlbUrl, const FString& Error);

	UFUNCTION()
	void OnGlbAssetLoaded(UglTFRuntimeAsset* Asset);

	// Meshy rigging
	UFUNCTION()
	void OnRigStateChanged(EMeshyRigState NewState);

	UFUNCTION()
	void OnRiggingComplete(const FRiggedCharacterUrls& Urls, const FString& Error);

	// Raw downloaded assets (stored until all arrive)
	UPROPERTY()
	UglTFRuntimeAsset* RiggedGlbAsset;

	UPROPERTY()
	UglTFRuntimeAsset* WalkAnimAsset;

	UPROPERTY()
	UglTFRuntimeAsset* RunAnimAsset;

	UPROPERTY()
	UglTFRuntimeAsset* IdleAnimAsset;

	UPROPERTY()
	UglTFRuntimeAsset* JumpAnimAsset;

	UPROPERTY()
	UglTFRuntimeAsset* FallAnimAsset;

	UPROPERTY()
	UglTFRuntimeAsset* SprintAnimAsset;

	UPROPERTY()
	UglTFRuntimeAsset* BoxingAnimAsset;

	UPROPERTY()
	UglTFRuntimeAsset* KickAnimAsset;

	UPROPERTY()
	UglTFRuntimeAsset* PunchAnimAsset;

	// Extracted after all assets loaded
	UPROPERTY()
	USkeletalMesh* RiggedSkeletalMesh;

	UPROPERTY()
	UAnimSequence* IdleAnim;

	UPROPERTY()
	UAnimSequence* WalkAnim;

	UPROPERTY()
	UAnimSequence* RunAnim;

	UPROPERTY()
	UAnimSequence* JumpAnim;

	UPROPERTY()
	UAnimSequence* FallAnim;

	UPROPERTY()
	UAnimSequence* SprintAnim;

	UPROPERTY()
	UAnimSequence* BoxingAnim;

	UPROPERTY()
	UAnimSequence* KickAnim;

	UPROPERTY()
	UAnimSequence* PunchAnim;

	int32 PendingDownloads = 0;
	int32 CompletedDownloads = 0;
	bool bUsingRiggedCharacter = false;
	ERuntimeMovementState CurrentMovementState = ERuntimeMovementState::Idle;
	UAnimSequence* CurrentPlayingAnim = nullptr;

	// Sprint
	bool bSprintHeld = false;

	// Combat animations (one-shot, play once and return to movement)
	bool bPlayingCombatAnim = false;
	float CombatAnimTimeRemaining = 0.f;
	void PlayCombatAnimation(UAnimSequence* Anim);

	// Runtime input actions (created in code, no editor setup needed)
	UPROPERTY()
	UInputAction* SprintInputAction;
	UPROPERTY()
	UInputAction* PunchInputAction;
	UPROPERTY()
	UInputAction* KickInputAction;
	UPROPERTY()
	UInputAction* BoxingInputAction;
	UPROPERTY()
	UInputMappingContext* CombatMappingContext;

	void OnSprintStarted();
	void OnSprintEnded();
	void OnPunchPressed();
	void OnKickPressed();
	void OnBoxingPressed();

	// Per-animation scale correction (ratio vs idle skeleton)
	float BaseComputedScale = 1.f;
	float WalkScaleCorrection = 1.f;
	float RunScaleCorrection = 1.f;
	float JumpScaleCorrection = 1.f;

	float GetRootBoneScale(UglTFRuntimeAsset* Asset) const;

	UPROPERTY()
	AActor* SpawnedStaticActor;

	UPROPERTY()
	USkeletalMeshComponent* RiggedMeshComp;

	void StartLoadingRiggedAssets(const FRiggedCharacterUrls& Urls);
	void OnAssetDownloaded();
	void ExtractAndSwapCharacter();
	void UpdateMovementAnimation();

	// Character history (up to 5 saved generations)
	struct FSavedCharacter
	{
		FString Name;
		FRiggedCharacterUrls Urls;
	};
	TArray<FSavedCharacter> CharacterHistory;

	void SaveToHistory(const FString& Name, const FRiggedCharacterUrls& Urls);
	void LoadHistory();
	void SaveHistory();
	void ClearHistory();
	FString GetCacheFilePath() const;
	void RefreshWidgetCharacterList();

	FString LastGenerationPrompt;

	UFUNCTION()
	void OnCharacterLoadRequested(int32 Index);

	UFUNCTION()
	void OnRiggedGlbLoaded(UglTFRuntimeAsset* Asset);

	UFUNCTION()
	void OnWalkAnimLoaded(UglTFRuntimeAsset* Asset);

	UFUNCTION()
	void OnRunAnimLoaded(UglTFRuntimeAsset* Asset);

	UFUNCTION()
	void OnIdleAnimLoaded(UglTFRuntimeAsset* Asset);

	UFUNCTION()
	void OnJumpAnimLoaded(UglTFRuntimeAsset* Asset);

	UFUNCTION()
	void OnFallAnimLoaded(UglTFRuntimeAsset* Asset);

	UFUNCTION()
	void OnSprintAnimLoaded(UglTFRuntimeAsset* Asset);

	UFUNCTION()
	void OnBoxingAnimLoaded(UglTFRuntimeAsset* Asset);

	UFUNCTION()
	void OnKickAnimLoaded(UglTFRuntimeAsset* Asset);

	UFUNCTION()
	void OnPunchAnimLoaded(UglTFRuntimeAsset* Asset);
};
