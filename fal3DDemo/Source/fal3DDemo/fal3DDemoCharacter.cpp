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
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "FalGeneratorWidget.h"
#include "FalApiClient.h"
#include "MeshyRigClient.h"
#include "Blueprint/UserWidget.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "glTFRuntimeAssetActor.h"
#include "glTFRuntimeAsset.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// Afal3DDemoCharacter

Afal3DDemoCharacter::Afal3DDemoCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

FString Afal3DDemoCharacter::GetCacheFilePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CharacterHistory.json"));
}

void Afal3DDemoCharacter::SaveToHistory(const FString& Name, const FRiggedCharacterUrls& Urls)
{
	// Add to front, remove duplicates with same name
	CharacterHistory.RemoveAll([&](const FSavedCharacter& C) { return C.Name == Name; });
	FSavedCharacter Entry;
	Entry.Name = Name;
	Entry.Urls = Urls;
	CharacterHistory.Insert(Entry, 0);

	// Keep max 5
	while (CharacterHistory.Num() > 5)
	{
		CharacterHistory.RemoveAt(CharacterHistory.Num() - 1);
	}

	SaveHistory();
	RefreshWidgetCharacterList();
}

void Afal3DDemoCharacter::SaveHistory()
{
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	for (const FSavedCharacter& Entry : CharacterHistory)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("Name"), Entry.Name);
		Obj->SetStringField(TEXT("RiggedGlbUrl"), Entry.Urls.RiggedGlbUrl);
		Obj->SetStringField(TEXT("WalkAnimGlbUrl"), Entry.Urls.WalkAnimGlbUrl);
		Obj->SetStringField(TEXT("RunAnimGlbUrl"), Entry.Urls.RunAnimGlbUrl);
		Obj->SetStringField(TEXT("IdleAnimGlbUrl"), Entry.Urls.IdleAnimGlbUrl);
		Obj->SetStringField(TEXT("JumpAnimGlbUrl"), Entry.Urls.JumpAnimGlbUrl);
		Obj->SetStringField(TEXT("SprintAnimGlbUrl"), Entry.Urls.SprintAnimGlbUrl);
		Obj->SetStringField(TEXT("BoxingAnimGlbUrl"), Entry.Urls.BoxingAnimGlbUrl);
		Obj->SetStringField(TEXT("KickAnimGlbUrl"), Entry.Urls.KickAnimGlbUrl);
		Obj->SetStringField(TEXT("PunchAnimGlbUrl"), Entry.Urls.PunchAnimGlbUrl);
		JsonArray.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(JsonArray, Writer);
	FFileHelper::SaveStringToFile(Output, *GetCacheFilePath());
	UE_LOG(LogTemplateCharacter, Log, TEXT("Saved %d characters to history"), CharacterHistory.Num());
}

void Afal3DDemoCharacter::LoadHistory()
{
	CharacterHistory.Empty();

	FString FileContents;
	if (!FFileHelper::LoadFileToString(FileContents, *GetCacheFilePath()))
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> JsonArray;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContents);
	if (!FJsonSerializer::Deserialize(Reader, JsonArray))
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& Val : JsonArray)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Val->TryGetObject(Obj)) continue;

		FSavedCharacter Entry;
		Entry.Name = (*Obj)->GetStringField(TEXT("Name"));
		Entry.Urls.RiggedGlbUrl = (*Obj)->GetStringField(TEXT("RiggedGlbUrl"));
		Entry.Urls.WalkAnimGlbUrl = (*Obj)->GetStringField(TEXT("WalkAnimGlbUrl"));
		Entry.Urls.RunAnimGlbUrl = (*Obj)->GetStringField(TEXT("RunAnimGlbUrl"));
		Entry.Urls.IdleAnimGlbUrl = (*Obj)->GetStringField(TEXT("IdleAnimGlbUrl"));
		Entry.Urls.JumpAnimGlbUrl = (*Obj)->GetStringField(TEXT("JumpAnimGlbUrl"));
		Entry.Urls.SprintAnimGlbUrl = (*Obj)->GetStringField(TEXT("SprintAnimGlbUrl"));
		Entry.Urls.BoxingAnimGlbUrl = (*Obj)->GetStringField(TEXT("BoxingAnimGlbUrl"));
		Entry.Urls.KickAnimGlbUrl = (*Obj)->GetStringField(TEXT("KickAnimGlbUrl"));
		Entry.Urls.PunchAnimGlbUrl = (*Obj)->GetStringField(TEXT("PunchAnimGlbUrl"));
		CharacterHistory.Add(Entry);
	}

	UE_LOG(LogTemplateCharacter, Log, TEXT("Loaded %d characters from history"), CharacterHistory.Num());
}

void Afal3DDemoCharacter::ClearHistory()
{
	CharacterHistory.Empty();
	IFileManager::Get().Delete(*GetCacheFilePath());
	RefreshWidgetCharacterList();
	UE_LOG(LogTemplateCharacter, Log, TEXT("Character history cleared"));
}

void Afal3DDemoCharacter::RefreshWidgetCharacterList()
{
	if (!GeneratorWidget) return;

	TArray<FString> Names;
	for (const FSavedCharacter& Entry : CharacterHistory)
	{
		Names.Add(Entry.Name);
	}
	GeneratorWidget->SetCharacterList(Names);
}

void Afal3DDemoCharacter::OnCharacterLoadRequested(int32 Index)
{
	if (Index >= 0 && Index < CharacterHistory.Num())
	{
		UE_LOG(LogTemplateCharacter, Log, TEXT("Loading saved character: %s"), *CharacterHistory[Index].Name);
		if (GeneratorWidget)
		{
			GeneratorWidget->UpdateStatus(FString::Printf(TEXT("Loading %s..."), *CharacterHistory[Index].Name));
		}
		StartLoadingRiggedAssets(CharacterHistory[Index].Urls);
	}
}


void Afal3DDemoCharacter::BeginPlay()
{
	Super::BeginPlay();

	FalClient = NewObject<UFalApiClient>(this);
	FalClient->OnGenerationComplete.AddDynamic(this, &Afal3DDemoCharacter::OnGenerationComplete);
	FalClient->OnStateChanged.AddDynamic(this, &Afal3DDemoCharacter::OnGenerationStateChanged);

	MeshyClient = NewObject<UMeshyRigClient>(this);
	MeshyClient->OnRiggingComplete.AddDynamic(this, &Afal3DDemoCharacter::OnRiggingComplete);
	MeshyClient->OnStateChanged.AddDynamic(this, &Afal3DDemoCharacter::OnRigStateChanged);

	GeneratorWidget = CreateWidget<UFalGeneratorWidget>(GetWorld()->GetFirstPlayerController(), UFalGeneratorWidget::StaticClass());
	if (GeneratorWidget)
	{
		GeneratorWidget->OnGenerateRequested.AddDynamic(this, &Afal3DDemoCharacter::OnGenerateRequested);
		GeneratorWidget->OnImageGenerateRequested.AddDynamic(this, &Afal3DDemoCharacter::OnImageGenerateRequested);
		GeneratorWidget->OnCloseRequested.AddDynamic(this, &Afal3DDemoCharacter::OnCloseRequested);
		GeneratorWidget->OnCharacterLoadRequested.AddDynamic(this, &Afal3DDemoCharacter::OnCharacterLoadRequested);
	}

	// Load saved character history and populate dropdown
	LoadHistory();
	RefreshWidgetCharacterList();

	// Create runtime input actions for sprint and combat (no editor setup needed)
	SprintInputAction = NewObject<UInputAction>(this);
	SprintInputAction->ValueType = EInputActionValueType::Boolean;

	PunchInputAction = NewObject<UInputAction>(this);
	PunchInputAction->ValueType = EInputActionValueType::Boolean;

	KickInputAction = NewObject<UInputAction>(this);
	KickInputAction->ValueType = EInputActionValueType::Boolean;

	BoxingInputAction = NewObject<UInputAction>(this);
	BoxingInputAction->ValueType = EInputActionValueType::Boolean;

	CombatMappingContext = NewObject<UInputMappingContext>(this);
	CombatMappingContext->MapKey(SprintInputAction, EKeys::LeftShift);
	CombatMappingContext->MapKey(PunchInputAction, EKeys::LeftMouseButton);
	CombatMappingContext->MapKey(KickInputAction, EKeys::F);
	CombatMappingContext->MapKey(BoxingInputAction, EKeys::V);

	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(CombatMappingContext, 1);
		}
	}

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(SprintInputAction, ETriggerEvent::Started, this, &Afal3DDemoCharacter::OnSprintStarted);
		EIC->BindAction(SprintInputAction, ETriggerEvent::Completed, this, &Afal3DDemoCharacter::OnSprintEnded);
		EIC->BindAction(PunchInputAction, ETriggerEvent::Started, this, &Afal3DDemoCharacter::OnPunchPressed);
		EIC->BindAction(KickInputAction, ETriggerEvent::Started, this, &Afal3DDemoCharacter::OnKickPressed);
		EIC->BindAction(BoxingInputAction, ETriggerEvent::Started, this, &Afal3DDemoCharacter::OnBoxingPressed);
	}

	// Characters are now loaded on-demand from the widget dropdown (no auto-load)
}

void Afal3DDemoCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bUsingRiggedCharacter)
	{
		// Update sprint speed
		GetCharacterMovement()->MaxWalkSpeed = bSprintHeld ? 800.f : 500.f;

		// Check if combat animation finished
		if (bPlayingCombatAnim)
		{
			CombatAnimTimeRemaining -= DeltaTime;
			if (CombatAnimTimeRemaining <= 0.f)
			{
				bPlayingCombatAnim = false;
				CurrentPlayingAnim = nullptr; // Force movement anim re-evaluation
			}
		}

		if (!bPlayingCombatAnim)
		{
			UpdateMovementAnimation();
		}
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

		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &Afal3DDemoCharacter::Move);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &Afal3DDemoCharacter::Look);

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
		HidePanel();
	else
		ShowPanel();
}

void Afal3DDemoCharacter::ShowPanel()
{
	if (!GeneratorWidget) return;

	GeneratorWidget->AddToViewport(10);
	bPanelVisible = true;

	// Refresh dropdown now that widget is constructed
	RefreshWidgetCharacterList();

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
		LastGenerationPrompt = Prompt;
		GeneratorWidget->SetGenerateEnabled(false);
		FalClient->GenerateModel(Prompt);
	}
}

void Afal3DDemoCharacter::OnImageGenerateRequested(const FString& ImagePath)
{
	if (FalClient)
	{
		LastGenerationPrompt = FPaths::GetBaseFilename(ImagePath);
		GeneratorWidget->SetGenerateEnabled(false);
		FalClient->GenerateModelFromImage(ImagePath);
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

	// Spawn static preview
	FglTFRuntimeHttpResponse Delegate;
	Delegate.BindDynamic(this, &Afal3DDemoCharacter::OnGlbAssetLoaded);
	UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUrl(GlbUrl, {}, Delegate, FglTFRuntimeConfig());

	// Start rigging in parallel, pass texture URL for proper texturing
	if (MeshyClient && FalClient)
	{
		MeshyClient->RigAndAnimate(GlbUrl, FalClient->LastTextureUrl);
	}
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

	FVector ForwardDir = GetActorForwardVector();
	FVector SpawnLocation = GetActorLocation() + ForwardDir * 300.f;

	FHitResult HitResult;
	FVector TraceStart = SpawnLocation + FVector(0.f, 0.f, 500.f);
	FVector TraceEnd = SpawnLocation - FVector(0.f, 0.f, 1000.f);

	if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility))
	{
		SpawnLocation = HitResult.ImpactPoint;
	}

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

		SpawnedStaticActor = GltfActor;

		if (GeneratorWidget)
		{
			GeneratorWidget->UpdateStatus(TEXT("Model spawned! Rigging in progress..."));
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Meshy Rigging

void Afal3DDemoCharacter::OnRigStateChanged(EMeshyRigState NewState)
{
	if (GeneratorWidget && MeshyClient)
	{
		GeneratorWidget->UpdateStatus(MeshyClient->StatusMessage);
	}
}

void Afal3DDemoCharacter::OnRiggingComplete(const FRiggedCharacterUrls& Urls, const FString& Error)
{
	if (!Error.IsEmpty())
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("Rigging failed: %s"), *Error);
		if (GeneratorWidget)
		{
			GeneratorWidget->UpdateStatus(FString::Printf(TEXT("Rigging failed: %s"), *Error));
		}
		return;
	}

	if (GeneratorWidget)
	{
		GeneratorWidget->UpdateStatus(TEXT("Downloading rigged character..."));
	}

	// Save to character history for the dropdown
	SaveToHistory(LastGenerationPrompt.IsEmpty() ? TEXT("Generated Character") : LastGenerationPrompt, Urls);

	StartLoadingRiggedAssets(Urls);
}

void Afal3DDemoCharacter::StartLoadingRiggedAssets(const FRiggedCharacterUrls& Urls)
{
	CompletedDownloads = 0;
	PendingDownloads = 0;

	// Reset all asset pointers
	RiggedGlbAsset = nullptr;
	WalkAnimAsset = nullptr;
	RunAnimAsset = nullptr;
	IdleAnimAsset = nullptr;
	JumpAnimAsset = nullptr;
	FallAnimAsset = nullptr;
	SprintAnimAsset = nullptr;
	BoxingAnimAsset = nullptr;
	KickAnimAsset = nullptr;
	PunchAnimAsset = nullptr;
	RiggedSkeletalMesh = nullptr;
	IdleAnim = nullptr;
	WalkAnim = nullptr;
	RunAnim = nullptr;
	JumpAnim = nullptr;
	FallAnim = nullptr;
	SprintAnim = nullptr;
	BoxingAnim = nullptr;
	KickAnim = nullptr;
	PunchAnim = nullptr;

	// Default SceneScale=100 converts glTF meters to UE centimeters (1.7m → 170 UE units).
	FglTFRuntimeConfig LoadConfig;

	auto LoadUrl = [this, LoadConfig](const FString& Url, const FName& FuncName)
	{
		if (!Url.IsEmpty())
		{
			PendingDownloads++;
			FglTFRuntimeHttpResponse Delegate;
			Delegate.BindUFunction(this, FuncName);
			UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUrl(Url, {}, Delegate, LoadConfig);
		}
	};

	LoadUrl(Urls.RiggedGlbUrl, FName("OnRiggedGlbLoaded"));
	LoadUrl(Urls.WalkAnimGlbUrl, FName("OnWalkAnimLoaded"));
	LoadUrl(Urls.RunAnimGlbUrl, FName("OnRunAnimLoaded"));
	LoadUrl(Urls.IdleAnimGlbUrl, FName("OnIdleAnimLoaded"));
	LoadUrl(Urls.JumpAnimGlbUrl, FName("OnJumpAnimLoaded"));
	LoadUrl(Urls.FallAnimGlbUrl, FName("OnFallAnimLoaded"));
	LoadUrl(Urls.SprintAnimGlbUrl, FName("OnSprintAnimLoaded"));
	LoadUrl(Urls.BoxingAnimGlbUrl, FName("OnBoxingAnimLoaded"));
	LoadUrl(Urls.KickAnimGlbUrl, FName("OnKickAnimLoaded"));
	LoadUrl(Urls.PunchAnimGlbUrl, FName("OnPunchAnimLoaded"));

	UE_LOG(LogTemplateCharacter, Log, TEXT("Started downloading %d rigged assets"), PendingDownloads);

	if (PendingDownloads == 0)
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("No rigged assets to download!"));
	}
}

// Each callback just stores the raw asset — extraction happens after all arrive
void Afal3DDemoCharacter::OnRiggedGlbLoaded(UglTFRuntimeAsset* Asset)
{
	RiggedGlbAsset = Asset;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Rigged GLB downloaded: %s"), Asset ? TEXT("OK") : TEXT("FAIL"));
	OnAssetDownloaded();
}

void Afal3DDemoCharacter::OnWalkAnimLoaded(UglTFRuntimeAsset* Asset)
{
	WalkAnimAsset = Asset;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Walk anim downloaded: %s"), Asset ? TEXT("OK") : TEXT("FAIL"));
	OnAssetDownloaded();
}

void Afal3DDemoCharacter::OnRunAnimLoaded(UglTFRuntimeAsset* Asset)
{
	RunAnimAsset = Asset;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Run anim downloaded: %s"), Asset ? TEXT("OK") : TEXT("FAIL"));
	OnAssetDownloaded();
}

void Afal3DDemoCharacter::OnIdleAnimLoaded(UglTFRuntimeAsset* Asset)
{
	IdleAnimAsset = Asset;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Idle anim downloaded: %s"), Asset ? TEXT("OK") : TEXT("FAIL"));
	OnAssetDownloaded();
}

void Afal3DDemoCharacter::OnJumpAnimLoaded(UglTFRuntimeAsset* Asset)
{
	JumpAnimAsset = Asset;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Jump anim downloaded: %s"), Asset ? TEXT("OK") : TEXT("FAIL"));
	OnAssetDownloaded();
}

void Afal3DDemoCharacter::OnFallAnimLoaded(UglTFRuntimeAsset* Asset)
{
	FallAnimAsset = Asset;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Fall anim downloaded: %s"), Asset ? TEXT("OK") : TEXT("FAIL"));
	OnAssetDownloaded();
}

void Afal3DDemoCharacter::OnSprintAnimLoaded(UglTFRuntimeAsset* Asset)
{
	SprintAnimAsset = Asset;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Sprint anim downloaded: %s"), Asset ? TEXT("OK") : TEXT("FAIL"));
	OnAssetDownloaded();
}

void Afal3DDemoCharacter::OnBoxingAnimLoaded(UglTFRuntimeAsset* Asset)
{
	BoxingAnimAsset = Asset;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Boxing anim downloaded: %s"), Asset ? TEXT("OK") : TEXT("FAIL"));
	OnAssetDownloaded();
}

void Afal3DDemoCharacter::OnKickAnimLoaded(UglTFRuntimeAsset* Asset)
{
	KickAnimAsset = Asset;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Kick anim downloaded: %s"), Asset ? TEXT("OK") : TEXT("FAIL"));
	OnAssetDownloaded();
}

void Afal3DDemoCharacter::OnPunchAnimLoaded(UglTFRuntimeAsset* Asset)
{
	PunchAnimAsset = Asset;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Punch anim downloaded: %s"), Asset ? TEXT("OK") : TEXT("FAIL"));
	OnAssetDownloaded();
}

void Afal3DDemoCharacter::OnAssetDownloaded()
{
	CompletedDownloads++;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Asset download %d/%d"), CompletedDownloads, PendingDownloads);

	if (GeneratorWidget)
	{
		GeneratorWidget->UpdateStatus(FString::Printf(TEXT("Loading assets... %d/%d"), CompletedDownloads, PendingDownloads));
	}

	if (CompletedDownloads >= PendingDownloads)
	{
		ExtractAndSwapCharacter();
	}
}

float Afal3DDemoCharacter::GetRootBoneScale(UglTFRuntimeAsset* Asset) const
{
	if (!Asset) return 1.f;

	// Load the skeleton from this GLB to read its root bone scale
	FglTFRuntimeSkeletalMeshConfig TempConfig;
	USkeletalMesh* TempMesh = Asset->LoadSkeletalMeshRecursive(
		TEXT(""), {}, TempConfig, EglTFRuntimeRecursiveMode::Ignore);

	if (!TempMesh) return 1.f;

	const FReferenceSkeleton& Skel = TempMesh->GetRefSkeleton();
	if (Skel.GetNum() == 0) return 1.f;

	// Root bone (Hips) scale — use the average of XYZ (should be uniform)
	FVector Scale = Skel.GetRefBonePose()[0].GetScale3D();
	float AvgScale = (FMath::Abs(Scale.X) + FMath::Abs(Scale.Y) + FMath::Abs(Scale.Z)) / 3.f;

	UE_LOG(LogTemplateCharacter, Log, TEXT("  Root bone scale from GLB: (%.4f, %.4f, %.4f) avg=%.4f"),
		Scale.X, Scale.Y, Scale.Z, AvgScale);

	return (AvgScale > 0.0001f) ? AvgScale : 1.f;
}

void Afal3DDemoCharacter::ExtractAndSwapCharacter()
{
	if (!RiggedGlbAsset)
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("No rigged GLB asset, cannot swap character"));
		if (GeneratorWidget)
		{
			GeneratorWidget->UpdateStatus(TEXT("Failed to load rigged character"));
		}
		return;
	}

	// Clean up previous rigged mesh component if re-generating
	if (RiggedMeshComp)
	{
		RiggedMeshComp->DestroyComponent();
		RiggedMeshComp = nullptr;
	}

	// Step 1: Extract skeletal mesh from rigged GLB
	FglTFRuntimeSkeletalMeshConfig SkeletalConfig;

	// Ignore mode = don't bake parent (Armature) node transforms into mesh vertices
	// This keeps mesh and animations in the same coordinate space
	RiggedSkeletalMesh = RiggedGlbAsset->LoadSkeletalMeshRecursive(
		TEXT(""), {}, SkeletalConfig, EglTFRuntimeRecursiveMode::Ignore);

	if (!RiggedSkeletalMesh)
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("Failed to extract skeletal mesh from rigged GLB"));
		if (GeneratorWidget)
		{
			GeneratorWidget->UpdateStatus(TEXT("Failed to extract skeletal mesh"));
		}
		return;
	}

	// Log skeleton info for debugging
	const FReferenceSkeleton& RefSkel = RiggedSkeletalMesh->GetRefSkeleton();
	UE_LOG(LogTemplateCharacter, Log, TEXT("Skeletal mesh extracted (bones=%d, materials=%d)"),
		RefSkel.GetNum(), RiggedSkeletalMesh->GetMaterials().Num());
	for (int32 i = 0; i < FMath::Min(RefSkel.GetNum(), 10); i++)
	{
		FTransform BoneT = RefSkel.GetRefBonePose()[i];
		UE_LOG(LogTemplateCharacter, Log, TEXT("  Bone[%d] '%s': pos=(%.1f,%.1f,%.1f) scale=(%.2f,%.2f,%.2f)"),
			i, *RefSkel.GetBoneName(i).ToString(),
			BoneT.GetLocation().X, BoneT.GetLocation().Y, BoneT.GetLocation().Z,
			BoneT.GetScale3D().X, BoneT.GetScale3D().Y, BoneT.GetScale3D().Z);
	}

	// Log mesh bounds to understand scale
	FBoxSphereBounds MeshBounds = RiggedSkeletalMesh->GetBounds();
	UE_LOG(LogTemplateCharacter, Log, TEXT("Mesh bounds: origin=(%.1f,%.1f,%.1f) extent=(%.1f,%.1f,%.1f) radius=%.1f"),
		MeshBounds.Origin.X, MeshBounds.Origin.Y, MeshBounds.Origin.Z,
		MeshBounds.BoxExtent.X, MeshBounds.BoxExtent.Y, MeshBounds.BoxExtent.Z,
		MeshBounds.SphereRadius);

	// Step 2: Extract animations
	FglTFRuntimeSkeletalAnimationConfig SkeletalAnimConfig;
	// Freeze root bone position to first frame (prevents character drifting from animation)
	SkeletalAnimConfig.bRemoveRootMotion = true;

	// The skeleton root is 'Hips' (bone 0) — do NOT remove its track, it's the main body bone.
	// Instead, use TransformPose to rotate the Hips bone to align animation forward with mesh forward.
	// We'll test without rotation first to see the actual direction, then adjust.
	FString HipsBoneName = RefSkel.GetBoneName(0).ToString();
	UE_LOG(LogTemplateCharacter, Warning, TEXT("Skeleton root bone: '%s' — applying rotation correction"), *HipsBoneName);

	// Apply 90° yaw correction to Hips bone to align animation direction with mesh direction
	FQuat RotCorrection(FVector::UpVector, FMath::DegreesToRadians(90.f));
	SkeletalAnimConfig.TransformPose.Add(HipsBoneName, FTransform(RotCorrection));
	UE_LOG(LogTemplateCharacter, Warning, TEXT("TransformPose: 90° yaw on '%s'"), *HipsBoneName);

	if (IdleAnimAsset)
	{
		IdleAnim = IdleAnimAsset->LoadSkeletalAnimation(RiggedSkeletalMesh, 0, SkeletalAnimConfig);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Idle animation: %s"), IdleAnim ? TEXT("OK") : TEXT("FAIL"));
	}
	if (WalkAnimAsset)
	{
		WalkAnim = WalkAnimAsset->LoadSkeletalAnimation(RiggedSkeletalMesh, 0, SkeletalAnimConfig);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Walk animation: %s"), WalkAnim ? TEXT("OK") : TEXT("FAIL"));
	}
	if (RunAnimAsset)
	{
		RunAnim = RunAnimAsset->LoadSkeletalAnimation(RiggedSkeletalMesh, 0, SkeletalAnimConfig);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Run animation: %s"), RunAnim ? TEXT("OK") : TEXT("FAIL"));
	}
	if (JumpAnimAsset)
	{
		JumpAnim = JumpAnimAsset->LoadSkeletalAnimation(RiggedSkeletalMesh, 0, SkeletalAnimConfig);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Jump animation: %s"), JumpAnim ? TEXT("OK") : TEXT("FAIL"));
	}
	if (FallAnimAsset)
	{
		FallAnim = FallAnimAsset->LoadSkeletalAnimation(RiggedSkeletalMesh, 0, SkeletalAnimConfig);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Fall animation: %s"), FallAnim ? TEXT("OK") : TEXT("FAIL"));
	}
	if (SprintAnimAsset)
	{
		SprintAnim = SprintAnimAsset->LoadSkeletalAnimation(RiggedSkeletalMesh, 0, SkeletalAnimConfig);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Sprint animation: %s"), SprintAnim ? TEXT("OK") : TEXT("FAIL"));
	}
	if (BoxingAnimAsset)
	{
		BoxingAnim = BoxingAnimAsset->LoadSkeletalAnimation(RiggedSkeletalMesh, 0, SkeletalAnimConfig);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Boxing animation: %s"), BoxingAnim ? TEXT("OK") : TEXT("FAIL"));
	}
	if (KickAnimAsset)
	{
		KickAnim = KickAnimAsset->LoadSkeletalAnimation(RiggedSkeletalMesh, 0, SkeletalAnimConfig);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Kick animation: %s"), KickAnim ? TEXT("OK") : TEXT("FAIL"));
	}
	if (PunchAnimAsset)
	{
		PunchAnim = PunchAnimAsset->LoadSkeletalAnimation(RiggedSkeletalMesh, 0, SkeletalAnimConfig);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Punch animation: %s"), PunchAnim ? TEXT("OK") : TEXT("FAIL"));
	}

	// Step 2b: Hardcoded per-animation scale corrections.
	// Meshy animation GLBs bake slightly different scales into keyframe data.
	// These values compensate so all anims render at the same visual size as idle.
	// Walk is ~15% smaller than idle, run is ~25% smaller than idle.
	WalkScaleCorrection = 1.10f;
	RunScaleCorrection  = 1.17f;
	JumpScaleCorrection = 1.17f;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Scale corrections: walk=%.2f run=%.2f jump=%.2f"),
		WalkScaleCorrection, RunScaleCorrection, JumpScaleCorrection);

	// Step 3: Compute dynamic scale to fit the character to ~180cm (capsule height)
	float MeshHeight = MeshBounds.BoxExtent.Z * 2.f;
	float TargetHeight = 180.f;
	float ComputedScale = (MeshHeight > 0.01f) ? (TargetHeight / MeshHeight) : 1.f;
	// The Meshy rigged GLB has a 0.01 scale on the root bone (Armature node).
	// UE5's skinning inflates the rendered mesh by 1/0.01 = 100x beyond what bounds report.
	// Divide by 100 to compensate.
	ComputedScale /= 100.f;
	ComputedScale = FMath::Clamp(ComputedScale, 0.001f, 1000.f);

	UE_LOG(LogTemplateCharacter, Log, TEXT("Mesh height=%.2f, target=%.1f, computed scale=%.4f"),
		MeshHeight, TargetHeight, ComputedScale);

	// Compute Z offset: place mesh bottom at capsule bottom (-96)
	float MeshBottom = (MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z) * ComputedScale;
	float OffsetZ = -96.f - MeshBottom;

	UE_LOG(LogTemplateCharacter, Log, TEXT("Mesh bottom (scaled)=%.1f, Z offset=%.1f"), MeshBottom, OffsetZ);

	// Step 4: Hide original mesh and create a fresh skeletal mesh component
	GetMesh()->SetVisibility(false);
	GetMesh()->SetHiddenInGame(true);
	GetMesh()->SetAnimInstanceClass(nullptr);

	RiggedMeshComp = NewObject<USkeletalMeshComponent>(this);
	RiggedMeshComp->SetupAttachment(GetRootComponent());
	RiggedMeshComp->SetSkeletalMeshAsset(RiggedSkeletalMesh);
	RiggedMeshComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);

	BaseComputedScale = ComputedScale;
	RiggedMeshComp->SetRelativeScale3D(FVector(ComputedScale));
	RiggedMeshComp->SetRelativeLocation(FVector(0.f, 0.f, OffsetZ));
	RiggedMeshComp->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));

	RiggedMeshComp->SetVisibility(true);
	RiggedMeshComp->SetHiddenInGame(false);
	RiggedMeshComp->SetCastShadow(true);
	RiggedMeshComp->bNeverDistanceCull = true;
	RiggedMeshComp->SetBoundsScale(100.f);
	RiggedMeshComp->SetForcedLOD(1);
	RiggedMeshComp->RegisterComponent();

	UE_LOG(LogTemplateCharacter, Log, TEXT("New mesh comp: visible=%d, registered=%d, bones=%d, scale=%.4f"),
		RiggedMeshComp->IsVisible(), RiggedMeshComp->IsRegistered(),
		RiggedMeshComp->GetNumBones(), ComputedScale);

	// Log material info on the final mesh
	for (int32 i = 0; i < RiggedMeshComp->GetNumMaterials(); i++)
	{
		UMaterialInterface* Mat = RiggedMeshComp->GetMaterial(i);
		UE_LOG(LogTemplateCharacter, Log, TEXT("  Material[%d]: %s (blend=%d)"),
			i, Mat ? *Mat->GetName() : TEXT("null"),
			Mat ? (int32)Mat->GetBlendMode() : -1);
	}

	// Start with idle animation
	if (IdleAnim)
	{
		RiggedMeshComp->PlayAnimation(IdleAnim, true);
	}
	else if (WalkAnim)
	{
		RiggedMeshComp->PlayAnimation(WalkAnim, true);
	}

	bUsingRiggedCharacter = true;
	CurrentMovementState = ERuntimeMovementState::Idle;

	// Delete the static preview actor
	if (SpawnedStaticActor)
	{
		SpawnedStaticActor->Destroy();
		SpawnedStaticActor = nullptr;
	}

	if (GeneratorWidget)
	{
		GeneratorWidget->UpdateStatus(TEXT("You are now the generated character!"));
	}

	UE_LOG(LogTemplateCharacter, Log, TEXT("Character swap complete! Anims: idle=%s walk=%s run=%s jump=%s fall=%s"),
		IdleAnim ? TEXT("Y") : TEXT("N"),
		WalkAnim ? TEXT("Y") : TEXT("N"),
		RunAnim ? TEXT("Y") : TEXT("N"),
		JumpAnim ? TEXT("Y") : TEXT("N"),
		FallAnim ? TEXT("Y") : TEXT("N"));
}

void Afal3DDemoCharacter::UpdateMovementAnimation()
{
	if (!RiggedSkeletalMesh) return;

	float Speed = GetCharacterMovement()->Velocity.Size2D();
	bool bFalling = GetCharacterMovement()->IsFalling();

	ERuntimeMovementState NewState;

	if (bFalling)
	{
		if (GetCharacterMovement()->Velocity.Z > 100.f)
			NewState = ERuntimeMovementState::Jump;
		else
			NewState = ERuntimeMovementState::Fall;
	}
	else if (Speed < 10.f)
	{
		NewState = ERuntimeMovementState::Idle;
	}
	else if (Speed < 300.f)
	{
		NewState = ERuntimeMovementState::Walk;
	}
	else if (bSprintHeld && Speed > 400.f)
	{
		NewState = ERuntimeMovementState::Sprint;
	}
	else
	{
		NewState = ERuntimeMovementState::Run;
	}

	if (NewState != CurrentMovementState)
	{
		UE_LOG(LogTemplateCharacter, Warning, TEXT("Movement: %d -> %d (speed=%.1f velZ=%.1f falling=%d)"),
			(int)CurrentMovementState, (int)NewState, Speed, GetCharacterMovement()->Velocity.Z, bFalling);

		CurrentMovementState = NewState;

		UAnimSequence* AnimToPlay = nullptr;
		bool bLoop = true;

		switch (NewState)
		{
		case ERuntimeMovementState::Idle:
			AnimToPlay = IdleAnim;
			break;
		case ERuntimeMovementState::Walk:
			AnimToPlay = WalkAnim ? WalkAnim : IdleAnim;
			break;
		case ERuntimeMovementState::Run:
			AnimToPlay = RunAnim ? RunAnim : WalkAnim;
			break;
		case ERuntimeMovementState::Sprint:
			AnimToPlay = SprintAnim ? SprintAnim : (RunAnim ? RunAnim : WalkAnim);
			break;
		case ERuntimeMovementState::Jump:
			AnimToPlay = JumpAnim ? JumpAnim : IdleAnim;
			bLoop = false;
			break;
		case ERuntimeMovementState::Fall:
			AnimToPlay = JumpAnim ? JumpAnim : IdleAnim;
			break;
		}

		if (AnimToPlay && RiggedMeshComp)
		{
			// Apply per-animation scale correction so all anims render at the same size
			float ScaleForAnim = BaseComputedScale;
			switch (NewState)
			{
			case ERuntimeMovementState::Walk:
				ScaleForAnim *= WalkScaleCorrection;
				break;
			case ERuntimeMovementState::Run:
			case ERuntimeMovementState::Sprint:
				ScaleForAnim *= RunScaleCorrection;
				break;
			case ERuntimeMovementState::Jump:
			case ERuntimeMovementState::Fall:
				ScaleForAnim *= JumpScaleCorrection;
				break;
			default:
				break; // Idle uses base scale
			}
			RiggedMeshComp->SetRelativeScale3D(FVector(ScaleForAnim));

			// Don't restart the same animation (e.g. Jump→Fall both use JumpAnim)
			if (CurrentPlayingAnim != AnimToPlay)
			{
				UE_LOG(LogTemplateCharacter, Warning, TEXT("  -> Playing %s (loop=%d)"), *AnimToPlay->GetName(), bLoop);
				RiggedMeshComp->PlayAnimation(AnimToPlay, bLoop);
				CurrentPlayingAnim = AnimToPlay;
			}
			else
			{
				UE_LOG(LogTemplateCharacter, Warning, TEXT("  -> SKIPPED (same anim already playing)"));
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Sprint & Combat

void Afal3DDemoCharacter::OnSprintStarted()
{
	bSprintHeld = true;
}

void Afal3DDemoCharacter::OnSprintEnded()
{
	bSprintHeld = false;
}

void Afal3DDemoCharacter::PlayCombatAnimation(UAnimSequence* Anim)
{
	if (!Anim || !RiggedMeshComp || !bUsingRiggedCharacter || bPanelVisible) return;

	bPlayingCombatAnim = true;
	CombatAnimTimeRemaining = Anim->GetPlayLength();
	CurrentPlayingAnim = Anim;
	RiggedMeshComp->SetRelativeScale3D(FVector(BaseComputedScale * 1.10f));
	RiggedMeshComp->PlayAnimation(Anim, false);

	UE_LOG(LogTemplateCharacter, Log, TEXT("Combat anim: %s (%.2fs)"), *Anim->GetName(), CombatAnimTimeRemaining);
}

void Afal3DDemoCharacter::OnPunchPressed()
{
	if (PunchAnim) PlayCombatAnimation(PunchAnim);
}

void Afal3DDemoCharacter::OnKickPressed()
{
	if (KickAnim) PlayCombatAnimation(KickAnim);
}

void Afal3DDemoCharacter::OnBoxingPressed()
{
	if (BoxingAnim) PlayCombatAnimation(BoxingAnim);
}
