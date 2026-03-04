// Copyright Epic Games, Inc. All Rights Reserved.

#include "fal3DDemoCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "FalGeneratorWidget.h"
#include "FalApiClient.h"
#include "Blueprint/UserWidget.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "glTFRuntimeAssetActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// Afal3DDemoCharacter

Afal3DDemoCharacter::Afal3DDemoCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void Afal3DDemoCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Create the API client
	FalClient = NewObject<UFalApiClient>(this);
	FalClient->OnGenerationComplete.AddDynamic(this, &Afal3DDemoCharacter::OnGenerationComplete);
	FalClient->OnStateChanged.AddDynamic(this, &Afal3DDemoCharacter::OnGenerationStateChanged);

	// Create the generator widget
	GeneratorWidget = CreateWidget<UFalGeneratorWidget>(GetWorld()->GetFirstPlayerController(), UFalGeneratorWidget::StaticClass());
	if (GeneratorWidget)
	{
		GeneratorWidget->OnGenerateRequested.AddDynamic(this, &Afal3DDemoCharacter::OnGenerateRequested);
		GeneratorWidget->OnCloseRequested.AddDynamic(this, &Afal3DDemoCharacter::OnCloseRequested);
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void Afal3DDemoCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void Afal3DDemoCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {

		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &Afal3DDemoCharacter::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &Afal3DDemoCharacter::Look);

		// Toggle generator panel
		if (TogglePanelAction)
		{
			EnhancedInputComponent->BindAction(TogglePanelAction, ETriggerEvent::Started, this, &Afal3DDemoCharacter::ToggleGeneratorPanel);
		}
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component!"), *GetNameSafe(this));
	}
}

void Afal3DDemoCharacter::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void Afal3DDemoCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

//////////////////////////////////////////////////////////////////////////
// Generator Panel

void Afal3DDemoCharacter::ToggleGeneratorPanel()
{
	if (bPanelVisible)
	{
		HidePanel();
	}
	else
	{
		ShowPanel();
	}
}

void Afal3DDemoCharacter::ShowPanel()
{
	if (!GeneratorWidget) return;

	GeneratorWidget->AddToViewport(10);
	bPanelVisible = true;

	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(GeneratorWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
	}
}

void Afal3DDemoCharacter::HidePanel()
{
	if (!GeneratorWidget) return;

	GeneratorWidget->RemoveFromParent();
	bPanelVisible = false;

	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void Afal3DDemoCharacter::OnGenerateRequested(const FString& Prompt)
{
	if (FalClient)
	{
		GeneratorWidget->SetGenerateEnabled(false);
		FalClient->GenerateModel(Prompt);
	}
}

void Afal3DDemoCharacter::OnCloseRequested()
{
	HidePanel();
}

void Afal3DDemoCharacter::OnGenerationStateChanged(EFalGenerationState NewState)
{
	if (!GeneratorWidget) return;

	if (FalClient)
	{
		GeneratorWidget->UpdateStatus(FalClient->StatusMessage);
	}

	// Re-enable button when generation finishes (success or error)
	if (NewState == EFalGenerationState::Completed || NewState == EFalGenerationState::Error)
	{
		GeneratorWidget->SetGenerateEnabled(true);
	}
}

void Afal3DDemoCharacter::OnGenerationComplete(const FString& GlbUrl, const FString& Error)
{
	if (!Error.IsEmpty())
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("Generation failed: %s"), *Error);
		return;
	}

	UE_LOG(LogTemplateCharacter, Log, TEXT("Loading GLB from: %s"), *GlbUrl);

	if (GeneratorWidget)
	{
		GeneratorWidget->UpdateStatus(TEXT("Downloading 3D model..."));
	}

	FglTFRuntimeHttpResponse Delegate;
	Delegate.BindDynamic(this, &Afal3DDemoCharacter::OnGlbAssetLoaded);
	UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUrl(GlbUrl, {}, Delegate, FglTFRuntimeConfig());
}

void Afal3DDemoCharacter::OnGlbAssetLoaded(UglTFRuntimeAsset* Asset)
{
	if (!Asset)
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("Failed to load GLB asset"));
		if (GeneratorWidget)
		{
			GeneratorWidget->UpdateStatus(TEXT("Failed to load 3D model"));
		}
		return;
	}

	UE_LOG(LogTemplateCharacter, Log, TEXT("GLB asset loaded, spawning actor"));

	// Calculate spawn location: 300 units in front of the player
	FVector ForwardDir = GetActorForwardVector();
	FVector SpawnLocation = GetActorLocation() + ForwardDir * 300.f;

	// Line trace downward to find ground
	FHitResult HitResult;
	FVector TraceStart = SpawnLocation + FVector(0.f, 0.f, 500.f);
	FVector TraceEnd = SpawnLocation - FVector(0.f, 0.f, 1000.f);

	if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility))
	{
		SpawnLocation = HitResult.ImpactPoint;
	}

	// Spawn the glTF actor using deferred spawning to set Asset before BeginPlay
	FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);

	AglTFRuntimeAssetActor* GltfActor = GetWorld()->SpawnActorDeferred<AglTFRuntimeAssetActor>(
		AglTFRuntimeAssetActor::StaticClass(),
		SpawnTransform
	);

	if (GltfActor)
	{
		GltfActor->Asset = Asset;
		GltfActor->StaticMeshConfig.bBuildSimpleCollision = true;
		GltfActor->FinishSpawning(SpawnTransform);

		UE_LOG(LogTemplateCharacter, Log, TEXT("Spawned glTF actor at %s"), *SpawnLocation.ToString());

		if (GeneratorWidget)
		{
			GeneratorWidget->UpdateStatus(TEXT("Model spawned!"));
		}
	}
}
