#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FalApiClient.h"
#include "FalGeneratorWidget.generated.h"

class UEditableTextBox;
class UTextBlock;
class UButton;
class UImage;
class UScrollBox;
class USizeBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGenerateRequested, const FString&, Prompt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFalPanelCloseRequested);

UCLASS()
class FAL3DDEMO_API UFalGeneratorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnGenerateRequested OnGenerateRequested;

	UPROPERTY(BlueprintAssignable)
	FOnFalPanelCloseRequested OnCloseRequested;

	void UpdateStatus(const FString& Message);
	void SetGenerateEnabled(bool bEnabled);
	void AddLogLine(const FString& Line);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY()
	UEditableTextBox* PromptInput;

	UPROPERTY()
	UTextBlock* StatusText;

	UPROPERTY()
	UButton* GenerateButton;

	UPROPERTY()
	UButton* CloseButton;

	UPROPERTY()
	UImage* SpinnerImage;

	UPROPERTY()
	UScrollBox* LogScrollBox;

	UPROPERTY()
	UTextBlock* LogText;

	UPROPERTY()
	UTexture2D* LogoTexture;

	bool bWidgetBuilt = false;
	bool bSpinnerVisible = false;
	float SpinnerAngle = 0.f;
	float SpinnerTime = 0.f;

	// Brand colors for cycling
	FLinearColor GetSpinnerColor(float Time) const;

	UFUNCTION()
	void OnGenerateClicked();

	UFUNCTION()
	void OnCloseClicked();

	// Log line storage
	TArray<FString> LogLines;
	void RefreshLogText();
};
