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

	int32 PendingDownloads = 0;
	int32 CompletedDownloads = 0;
	bool bUsingRiggedCharacter = false;
	ERuntimeMovementState CurrentMovementState = ERuntimeMovementState::Idle;

	UPROPERTY()
	AActor* SpawnedStaticActor;

	UPROPERTY()
	USkeletalMeshComponent* RiggedMeshComp;

	void StartLoadingRiggedAssets(const FRiggedCharacterUrls& Urls);
	void OnAssetDownloaded();
	void ExtractAndSwapCharacter();
	void UpdateMovementAnimation();

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
};
