#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FalApiClient.h"
#include "FalGeneratorWidget.generated.h"

class UEditableTextBox;
class UTextBlock;
class UButton;

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

protected:
	virtual void NativeConstruct() override;

private:
	UPROPERTY()
	UEditableTextBox* PromptInput;

	UPROPERTY()
	UTextBlock* StatusText;

	UPROPERTY()
	UButton* GenerateButton;

	UPROPERTY()
	UButton* CloseButton;

	UFUNCTION()
	void OnGenerateClicked();

	UFUNCTION()
	void OnCloseClicked();
};
