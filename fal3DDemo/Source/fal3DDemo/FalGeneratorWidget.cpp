#include "FalGeneratorWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Logging/LogMacros.h"
#include "Styling/AppStyle.h"
#include "DesktopPlatformModule.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"

DEFINE_LOG_CATEGORY_STATIC(LogFalWidget, Log, All);

// Accent colors (not available from FAppStyle)
namespace FalUI
{
	const FLinearColor TextAccent(0.33f, 0.55f, 0.75f, 1.f);         // Muted steel blue (Unreal-subtle)
	const FLinearColor BtnPrimary(0.0f, 0.28f, 0.55f, 1.f);          // Muted Unreal blue
	const FLinearColor BtnDanger(0.22f, 0.06f, 0.06f, 1.f);          // Dark muted red

	// fal brand colors for spinner
	const FLinearColor FalPurple(0.341f, 0.094f, 0.753f, 1.f);
	const FLinearColor FalViolet(0.671f, 0.467f, 1.0f, 1.f);
	const FLinearColor FalPink(0.890f, 0.400f, 0.686f, 1.f);
	const FLinearColor FalRed(0.925f, 0.024f, 0.282f, 1.f);
	const FLinearColor FalTeal(0.220f, 0.675f, 0.776f, 1.f);
	const FLinearColor FalBlue(0.247f, 0.710f, 0.996f, 1.f);
	const FLinearColor FalDeepBlue(0.067f, 0.369f, 0.953f, 1.f);
}

void UFalGeneratorWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (bWidgetBuilt)
	{
		UE_LOG(LogFalWidget, Log, TEXT("NativeConstruct: skipping rebuild (already built)"));
		return;
	}

	UE_LOG(LogFalWidget, Log, TEXT("NativeConstruct: starting"));

	// Try to load logo as imported UAsset
	LogoTexture = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr,
		TEXT("/Game/UI/fal_logo")));

	// Editor font
	FSlateFontInfo NormalFont = FAppStyle::GetFontStyle("NormalFont");
	FSlateFontInfo SmallFont = NormalFont;
	SmallFont.Size = 9;
	FSlateFontInfo TinyFont = NormalFont;
	TinyFont.Size = 8;

	// Root canvas
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	// Panel background — uses editor's ToolPanel.GroupBorder brush (rounded dark panel)
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
	Background->SetBrush(*FAppStyle::GetBrush("ToolPanel.GroupBorder"));
	Background->SetPadding(FMargin(12.f, 10.f, 12.f, 10.f));

	UCanvasPanelSlot* BgSlot = RootCanvas->AddChildToCanvas(Background);
	BgSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	BgSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	BgSlot->SetSize(FVector2D(400.f, 620.f));
	BgSlot->SetPosition(FVector2D(0.f, 0.f));

	// Single vertical box (flat structure — proven to work)
	UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VBox"));
	Background->SetContent(VBox);

	// ── Header row: Logo + Title (left-aligned like editor panels) ──
	UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeaderRow"));
	UVerticalBoxSlot* HeaderRowSlot = VBox->AddChildToVerticalBox(HeaderRow);
	HeaderRowSlot->SetHorizontalAlignment(HAlign_Left);
	HeaderRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

	// Logo / spinner image
	SpinnerImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SpinnerImage"));
	if (LogoTexture)
	{
		SpinnerImage->SetBrushFromTexture(LogoTexture, false);
	}
	SpinnerImage->SetDesiredSizeOverride(FVector2D(24.f, 24.f));
	SpinnerImage->SetColorAndOpacity(FalUI::FalPurple);
	SpinnerImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

	UHorizontalBoxSlot* LogoSlot = HeaderRow->AddChildToHorizontalBox(SpinnerImage);
	LogoSlot->SetVerticalAlignment(VAlign_Center);
	LogoSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

	// Title — editor font
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("fal.ai 3D Generator")));
	FSlateFontInfo TitleFont = NormalFont;
	TitleFont.Size = 13;
	Title->SetFont(TitleFont);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	UHorizontalBoxSlot* TitleSlot = HeaderRow->AddChildToHorizontalBox(Title);
	TitleSlot->SetVerticalAlignment(VAlign_Center);

	// ── Separator ──
	USpacer* Sep1 = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("Sep1"));
	Sep1->SetSize(FVector2D(0.f, 1.f));
	UVerticalBoxSlot* Sep1Slot = VBox->AddChildToVerticalBox(Sep1);
	Sep1Slot->SetHorizontalAlignment(HAlign_Fill);
	Sep1Slot->SetPadding(FMargin(0.f, 4.f, 0.f, 6.f));

	// ── Prompt label ──
	UTextBlock* PromptLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PromptLabel"));
	PromptLabel->SetText(FText::FromString(TEXT("Prompt")));
	PromptLabel->SetFont(SmallFont);
	PromptLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)));
	UVerticalBoxSlot* LabelSlot = VBox->AddChildToVerticalBox(PromptLabel);
	LabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

	// ── Prompt input — use editor text box style ──
	PromptInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("PromptInput"));
	PromptInput->SetHintText(FText::FromString(TEXT("Describe what you want to generate...")));

	// Apply editor's own editable text box style for native look
	const FEditableTextBoxStyle* EditorTextBoxStyle = &FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
	if (EditorTextBoxStyle)
	{
		PromptInput->WidgetStyle = *EditorTextBoxStyle;
	}

	UVerticalBoxSlot* InputSlot = VBox->AddChildToVerticalBox(PromptInput);
	InputSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	// ── Image browse row: [Browse Image] [filename] [X] ──
	UHorizontalBox* ImageRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ImageRow"));
	UVerticalBoxSlot* ImageRowSlot = VBox->AddChildToVerticalBox(ImageRow);
	ImageRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	BrowseImageButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BrowseImageButton"));
	BrowseImageButton->SetStyle(FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"));
	BrowseImageButton->OnClicked.AddDynamic(this, &UFalGeneratorWidget::OnBrowseImageClicked);

	UTextBlock* BrowseBtnText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BrowseBtnText"));
	BrowseBtnText->SetText(FText::FromString(TEXT("Browse Image")));
	BrowseBtnText->SetFont(SmallFont);
	BrowseBtnText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)));
	BrowseImageButton->AddChild(BrowseBtnText);

	UHorizontalBoxSlot* BrowseBtnSlot = ImageRow->AddChildToHorizontalBox(BrowseImageButton);
	BrowseBtnSlot->SetVerticalAlignment(VAlign_Center);
	BrowseBtnSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	BrowseBtnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

	ImageFileText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ImageFileText"));
	ImageFileText->SetText(FText::FromString(TEXT("No image (text-to-3D)")));
	ImageFileText->SetFont(TinyFont);
	ImageFileText->SetColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)));
	ImageFileText->SetAutoWrapText(false);
	UHorizontalBoxSlot* ImageTextSlot = ImageRow->AddChildToHorizontalBox(ImageFileText);
	ImageTextSlot->SetVerticalAlignment(VAlign_Center);
	ImageTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	ClearImageButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ClearImageButton"));
	ClearImageButton->SetStyle(FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"));
	ClearImageButton->OnClicked.AddDynamic(this, &UFalGeneratorWidget::OnClearImageClicked);
	ClearImageButton->SetVisibility(ESlateVisibility::Collapsed);

	UTextBlock* ClearBtnText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ClearBtnText"));
	ClearBtnText->SetText(FText::FromString(TEXT("X")));
	ClearBtnText->SetFont(SmallFont);
	ClearBtnText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.3f, 0.3f)));
	ClearImageButton->AddChild(ClearBtnText);

	UHorizontalBoxSlot* ClearBtnSlot = ImageRow->AddChildToHorizontalBox(ClearImageButton);
	ClearBtnSlot->SetVerticalAlignment(VAlign_Center);
	ClearBtnSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
	ClearBtnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

	// ── Image preview (hidden until an image is selected) ──
	ImagePreview = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ImagePreview"));
	ImagePreview->SetDesiredSizeOverride(FVector2D(256.f, 256.f));
	ImagePreview->SetVisibility(ESlateVisibility::Collapsed);
	UVerticalBoxSlot* PreviewSlot = VBox->AddChildToVerticalBox(ImagePreview);
	PreviewSlot->SetHorizontalAlignment(HAlign_Center);
	PreviewSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	// ── T-pose checkbox ──
	UHorizontalBox* TPoseRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TPoseRow"));
	UVerticalBoxSlot* TPoseRowSlot = VBox->AddChildToVerticalBox(TPoseRow);
	TPoseRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	TPoseCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("TPoseCheckBox"));
	TPoseCheckBox->SetCheckedState(ECheckBoxState::Unchecked);
	UHorizontalBoxSlot* CheckSlot = TPoseRow->AddChildToHorizontalBox(TPoseCheckBox);
	CheckSlot->SetVerticalAlignment(VAlign_Center);
	CheckSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));

	UTextBlock* TPoseLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TPoseLabel"));
	TPoseLabel->SetText(FText::FromString(TEXT("T-pose")));
	TPoseLabel->SetFont(SmallFont);
	TPoseLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)));
	UHorizontalBoxSlot* TPoseLabelSlot = TPoseRow->AddChildToHorizontalBox(TPoseLabel);
	TPoseLabelSlot->SetVerticalAlignment(VAlign_Center);

	// ── Generate button — editor flat style ──
	GenerateButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("GenerateButton"));
	GenerateButton->SetStyle(FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"));
	GenerateButton->OnClicked.AddDynamic(this, &UFalGeneratorWidget::OnGenerateClicked);

	GenerateButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GenerateBtnText"));
	GenerateButtonText->SetText(FText::FromString(TEXT("Generate 3D Model")));
	FSlateFontInfo BtnFont = NormalFont;
	BtnFont.Size = 11;
	GenerateButtonText->SetFont(BtnFont);
	GenerateButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	GenerateButtonText->SetJustification(ETextJustify::Center);
	GenerateButton->AddChild(GenerateButtonText);

	UVerticalBoxSlot* GenBtnSlot = VBox->AddChildToVerticalBox(GenerateButton);
	GenBtnSlot->SetHorizontalAlignment(HAlign_Fill);
	GenBtnSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	// ── Status text ──
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetText(FText::FromString(TEXT("")));
	StatusText->SetFont(SmallFont);
	StatusText->SetColorAndOpacity(FSlateColor(FalUI::TextAccent));
	StatusText->SetAutoWrapText(true);
	UVerticalBoxSlot* StatusSlot = VBox->AddChildToVerticalBox(StatusText);
	StatusSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));

	// ── Separator ──
	USpacer* Sep2 = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("Sep2"));
	Sep2->SetSize(FVector2D(0.f, 1.f));
	UVerticalBoxSlot* Sep2Slot = VBox->AddChildToVerticalBox(Sep2);
	Sep2Slot->SetHorizontalAlignment(HAlign_Fill);
	Sep2Slot->SetPadding(FMargin(0.f, 2.f, 0.f, 3.f));

	// ── Saved Characters — chevron-only dropdown, auto-loads on selection ──
	UHorizontalBox* HistoryRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HistoryRow"));
	UVerticalBoxSlot* HistRowSlot = VBox->AddChildToVerticalBox(HistoryRow);
	HistRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

	UTextBlock* HistoryLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HistoryLabel"));
	HistoryLabel->SetText(FText::FromString(TEXT("Saved Characters")));
	HistoryLabel->SetFont(TinyFont);
	HistoryLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f)));
	UHorizontalBoxSlot* HistLabelSlot = HistoryRow->AddChildToHorizontalBox(HistoryLabel);
	HistLabelSlot->SetVerticalAlignment(VAlign_Center);
	HistLabelSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));

	CharacterDropdown = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("CharacterDropdown"));
	CharacterDropdown->SetContentPadding(FMargin(0.f, 0.f));
	CharacterDropdown->OnSelectionChanged.AddDynamic(this, &UFalGeneratorWidget::OnCharacterSelected);

	// Wrap in a tiny SizeBox so only the chevron arrow is visible
	USizeBox* DropdownSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DropdownSizeBox"));
	DropdownSizeBox->SetWidthOverride(24.f);
	DropdownSizeBox->SetHeightOverride(20.f);
	DropdownSizeBox->AddChild(CharacterDropdown);

	UHorizontalBoxSlot* DropSlot = HistoryRow->AddChildToHorizontalBox(DropdownSizeBox);
	DropSlot->SetVerticalAlignment(VAlign_Center);

	// ── Log header label ──
	UTextBlock* LogLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LogLabel"));
	LogLabel->SetText(FText::FromString(TEXT("Output Log")));
	LogLabel->SetFont(SmallFont);
	LogLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)));
	UVerticalBoxSlot* LogLabelSlot = VBox->AddChildToVerticalBox(LogLabel);
	LogLabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

	// ── Log area — dark inner panel ──
	UBorder* LogBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LogBorder"));
	LogBorder->SetBrush(*FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"));
	LogBorder->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));

	LogScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("LogScrollBox"));
	LogBorder->SetContent(LogScrollBox);

	LogText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LogText"));
	LogText->SetText(FText::FromString(TEXT("Ready.")));
	LogText->SetFont(TinyFont);
	LogText->SetColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)));
	LogText->SetAutoWrapText(true);
	LogScrollBox->AddChild(LogText);

	UVerticalBoxSlot* LogSlot = VBox->AddChildToVerticalBox(LogBorder);
	LogSlot->SetHorizontalAlignment(HAlign_Fill);
	LogSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LogSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// ── Close button — editor flat style ──
	CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton"));
	CloseButton->SetStyle(FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"));
	CloseButton->OnClicked.AddDynamic(this, &UFalGeneratorWidget::OnCloseClicked);

	UTextBlock* CloseBtnText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseBtnText"));
	CloseBtnText->SetText(FText::FromString(TEXT("Close (P)")));
	CloseBtnText->SetFont(SmallFont);
	CloseBtnText->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)));
	CloseBtnText->SetJustification(ETextJustify::Center);
	CloseButton->AddChild(CloseBtnText);

	UVerticalBoxSlot* CloseBtnSlot = VBox->AddChildToVerticalBox(CloseButton);
	CloseBtnSlot->SetHorizontalAlignment(HAlign_Fill);

	bWidgetBuilt = true;
	UE_LOG(LogFalWidget, Log, TEXT("NativeConstruct: complete"));
}

void UFalGeneratorWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bSpinnerVisible && SpinnerImage)
	{
		SpinnerTime += InDeltaTime;
		float CycleTime = FMath::Fmod(SpinnerTime, 4.f);
		float T = CycleTime / 4.f;

		float RotPhase = T * 4.f;
		float RotIndex = FMath::FloorToFloat(RotPhase);
		float RotFrac = RotPhase - RotIndex;
		float EasedFrac = RotFrac < 0.5f
			? 2.f * RotFrac * RotFrac
			: 1.f - FMath::Pow(-2.f * RotFrac + 2.f, 2.f) / 2.f;
		SpinnerAngle = (RotIndex + EasedFrac) * 360.f;

		SpinnerImage->SetRenderTransformAngle(SpinnerAngle);
		SpinnerImage->SetColorAndOpacity(GetSpinnerColor(T));
	}
}

FLinearColor UFalGeneratorWidget::GetSpinnerColor(float T) const
{
	struct ColorStop { float T; FLinearColor C; };
	static const ColorStop Stops[] = {
		{0.00f, FalUI::FalPurple},
		{0.20f, FalUI::FalViolet},
		{0.25f, FalUI::FalPink},
		{0.45f, FalUI::FalRed},
		{0.50f, FalUI::FalViolet},
		{0.70f, FalUI::FalTeal},
		{0.75f, FalUI::FalBlue},
		{0.95f, FalUI::FalDeepBlue},
		{1.00f, FalUI::FalPurple},
	};
	static const int32 NumStops = UE_ARRAY_COUNT(Stops);

	for (int32 i = 0; i < NumStops - 1; i++)
	{
		if (T <= Stops[i + 1].T)
		{
			float Range = Stops[i + 1].T - Stops[i].T;
			float Alpha = (Range > 0.f) ? (T - Stops[i].T) / Range : 0.f;
			return FMath::Lerp(Stops[i].C, Stops[i + 1].C, Alpha);
		}
	}
	return FalUI::FalPurple;
}

void UFalGeneratorWidget::UpdateStatus(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
	}

	if (!Message.IsEmpty())
	{
		AddLogLine(Message);
	}

	bSpinnerVisible = !Message.IsEmpty()
		&& !Message.Contains(TEXT("Failed"))
		&& !Message.Contains(TEXT("spawned"))
		&& !Message.Contains(TEXT("You are now"));

	if (!bSpinnerVisible && SpinnerImage)
	{
		SpinnerImage->SetRenderTransformAngle(0.f);
		SpinnerImage->SetColorAndOpacity(FalUI::FalPurple);
	}
}

void UFalGeneratorWidget::SetGenerateEnabled(bool bEnabled)
{
	if (GenerateButton)
	{
		GenerateButton->SetIsEnabled(bEnabled);
	}
	if (BrowseImageButton)
	{
		BrowseImageButton->SetIsEnabled(bEnabled);
	}
	if (ClearImageButton)
	{
		ClearImageButton->SetIsEnabled(bEnabled);
	}
}

void UFalGeneratorWidget::AddLogLine(const FString& Line)
{
	FDateTime Now = FDateTime::Now();
	FString Timestamp = Now.ToString(TEXT("%H:%M:%S"));
	LogLines.Add(FString::Printf(TEXT("[%s] %s"), *Timestamp, *Line));

	while (LogLines.Num() > 50)
	{
		LogLines.RemoveAt(0);
	}

	RefreshLogText();
}

void UFalGeneratorWidget::RefreshLogText()
{
	if (!LogText) return;

	FString Combined = FString::Join(LogLines, TEXT("\n"));
	LogText->SetText(FText::FromString(Combined));

	if (LogScrollBox)
	{
		LogScrollBox->ScrollToEnd();
	}
}

void UFalGeneratorWidget::UpdateGenerateButtonLabel()
{
	if (!GenerateButtonText) return;

	if (SelectedImagePath.IsEmpty())
	{
		GenerateButtonText->SetText(FText::FromString(TEXT("Generate 3D Model")));
	}
	else
	{
		GenerateButtonText->SetText(FText::FromString(TEXT("Generate from Image")));
	}
}

void UFalGeneratorWidget::OnGenerateClicked()
{
	if (!SelectedImagePath.IsEmpty())
	{
		// Image-to-3D mode
		AddLogLine(FString::Printf(TEXT("Generating from image: \"%s\""), *FPaths::GetCleanFilename(SelectedImagePath)));
		OnImageGenerateRequested.Broadcast(SelectedImagePath);
		return;
	}

	// Text-to-3D mode (original flow)
	if (PromptInput)
	{
		FString Prompt = PromptInput->GetText().ToString();
		if (!Prompt.IsEmpty())
		{
			if (TPoseCheckBox && TPoseCheckBox->IsChecked())
			{
				Prompt += TEXT(" T-pose");
			}
			AddLogLine(FString::Printf(TEXT("Generating: \"%s\""), *Prompt));
			OnGenerateRequested.Broadcast(Prompt);
		}
	}
}

void UFalGeneratorWidget::OnCloseClicked()
{
	OnCloseRequested.Broadcast();
}

void UFalGeneratorWidget::OnBrowseImageClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	// Get the native window handle for the file dialog parent
	void* ParentWindowHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<SWindow> TopWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (TopWindow.IsValid())
		{
			ParentWindowHandle = TopWindow->GetNativeWindow()->GetOSWindowHandle();
		}
	}

	TArray<FString> OutFiles;
	bool bOpened = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("Select Character Image"),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("Image Files (*.png, *.jpg, *.jpeg, *.webp)|*.png;*.jpg;*.jpeg;*.webp|All Files (*.*)|*.*"),
		0,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		SelectedImagePath = OutFiles[0];

		if (ImageFileText)
		{
			ImageFileText->SetText(FText::FromString(FPaths::GetCleanFilename(SelectedImagePath)));
			ImageFileText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)));
		}

		if (ClearImageButton)
		{
			ClearImageButton->SetVisibility(ESlateVisibility::Visible);
		}

		// Load image preview
		TArray<uint8> FileData;
		if (FFileHelper::LoadFileToArray(FileData, *SelectedImagePath))
		{
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			EImageFormat Format = EImageFormat::PNG;
			FString Ext = FPaths::GetExtension(SelectedImagePath).ToLower();
			if (Ext == TEXT("jpg") || Ext == TEXT("jpeg")) Format = EImageFormat::JPEG;

			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(Format);
			if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
			{
				TArray<uint8> RawData;
				if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
				{
					int32 W = ImageWrapper->GetWidth();
					int32 H = ImageWrapper->GetHeight();
					PreviewTexture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
					if (PreviewTexture)
					{
						void* TextureData = PreviewTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
						FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
						PreviewTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
						PreviewTexture->UpdateResource();

						if (ImagePreview)
						{
							ImagePreview->SetBrushFromTexture(PreviewTexture, false);
							FSlateBrush Brush;
							Brush.SetResourceObject(PreviewTexture);
							Brush.ImageSize = FVector2D(256.f, 256.f);
							Brush.DrawAs = ESlateBrushDrawType::Image;
							ImagePreview->SetBrush(Brush);
							ImagePreview->SetVisibility(ESlateVisibility::Visible);
						}
					}
				}
			}
		}

		// Disable prompt and T-pose when image is selected
		if (PromptInput) PromptInput->SetIsEnabled(false);
		if (TPoseCheckBox) TPoseCheckBox->SetIsEnabled(false);

		UpdateGenerateButtonLabel();
		AddLogLine(FString::Printf(TEXT("Image selected: %s"), *FPaths::GetCleanFilename(SelectedImagePath)));
	}
}

void UFalGeneratorWidget::OnClearImageClicked()
{
	SelectedImagePath.Empty();

	if (ImageFileText)
	{
		ImageFileText->SetText(FText::FromString(TEXT("No image (text-to-3D)")));
		ImageFileText->SetColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)));
	}

	if (ClearImageButton)
	{
		ClearImageButton->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Hide preview and re-enable prompt and T-pose when image is cleared
	if (ImagePreview) ImagePreview->SetVisibility(ESlateVisibility::Collapsed);
	PreviewTexture = nullptr;
	if (PromptInput) PromptInput->SetIsEnabled(true);
	if (TPoseCheckBox) TPoseCheckBox->SetIsEnabled(true);

	UpdateGenerateButtonLabel();
	AddLogLine(TEXT("Image cleared, switched to text-to-3D mode"));
}

void UFalGeneratorWidget::SetCharacterList(const TArray<FString>& Names)
{
	if (!CharacterDropdown) return;

	CharacterDropdown->ClearOptions();
	for (const FString& Name : Names)
	{
		CharacterDropdown->AddOption(Name);
	}
	// Don't auto-select — dropdown shows empty (just chevron) until user clicks
	CharacterDropdown->ClearSelection();
}

void UFalGeneratorWidget::OnCharacterSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (!CharacterDropdown || SelectionType == ESelectInfo::Direct) return;

	int32 Index = CharacterDropdown->GetSelectedIndex();
	if (Index >= 0)
	{
		OnCharacterLoadRequested.Broadcast(Index);
		// Clear selection so dropdown shows empty (just chevron) again
		CharacterDropdown->ClearSelection();
	}
}
