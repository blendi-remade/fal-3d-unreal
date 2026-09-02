#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "IImageWrapper.h"
#include "FalGeneratorWidget.generated.h"

class UEditableTextBox;
class UTextBlock;
class UButton;
class UImage;
class UScrollBox;
class USizeBox;
class UComboBoxString;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGenerateRequested, const FString&, Prompt, bool, bTPose);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnImageGenerateRequested, const FString&, ImagePath, bool, bTPose);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFalPanelCloseRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharacterLoadRequested, int32, Index);

UCLASS()
class FAL3DDEMO_API UFalGeneratorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnGenerateRequested OnGenerateRequested;

	UPROPERTY(BlueprintAssignable)
	FOnImageGenerateRequested OnImageGenerateRequested;

	UPROPERTY(BlueprintAssignable)
	FOnFalPanelCloseRequested OnCloseRequested;

	UPROPERTY(BlueprintAssignable)
	FOnCharacterLoadRequested OnCharacterLoadRequested;

	void UpdateStatus(const FString& Message);
	void SetGenerateEnabled(bool bEnabled);
	void AddLogLine(const FString& Line);
	void SetCharacterList(const TArray<FString>& Names);

	// Downloads an image (e.g. the generated concept sheet) and shows it in the preview slot.
	void ShowImagePreviewFromUrl(const FString& Url);

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
	UTextBlock* GenerateButtonText;

	UPROPERTY()
	UButton* CloseButton;

	UPROPERTY()
	class UCheckBox* TPoseCheckBox;

	UPROPERTY()
	UImage* SpinnerImage;

	UPROPERTY()
	UScrollBox* LogScrollBox;

	UPROPERTY()
	UTextBlock* LogText;

	UPROPERTY()
	UTexture2D* LogoTexture;

	// Image browse controls
	UPROPERTY()
	UButton* BrowseImageButton;

	UPROPERTY()
	UButton* ClearImageButton;

	UPROPERTY()
	UTextBlock* ImageFileText;

	UPROPERTY()
	UImage* ImagePreview;

	UPROPERTY()
	UTexture2D* PreviewTexture;

	FString SelectedImagePath;

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

	UFUNCTION()
	void OnBrowseImageClicked();

	UFUNCTION()
	void OnClearImageClicked();

	UFUNCTION()
	void OnCharacterSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

	UPROPERTY()
	UComboBoxString* CharacterDropdown;

	// Log line storage
	TArray<FString> LogLines;
	void RefreshLogText();

	void UpdateGenerateButtonLabel();

	// Decodes compressed image bytes into PreviewTexture and shows ImagePreview. Returns false on failure.
	bool ApplyPreviewImage(const TArray<uint8>& CompressedData, EImageFormat Format);
	void HidePreviewImage();
};
