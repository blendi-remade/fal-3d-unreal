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
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogFalWidget, Log, All);

// Unreal Editor color palette (matched from editor screenshots)
namespace FalUI
{
	const FLinearColor PanelBg(0.014f, 0.014f, 0.014f, 0.95f);       // #242424-ish, near opaque
	const FLinearColor RowBg(0.022f, 0.022f, 0.022f, 1.f);           // #2a2a2a — section bg
	const FLinearColor InputBg(0.008f, 0.008f, 0.008f, 1.f);         // #111111 — dark input field
	const FLinearColor TextPrimary(0.78f, 0.78f, 0.78f, 1.f);        // #c8c8c8
	const FLinearColor TextSecondary(0.45f, 0.45f, 0.45f, 1.f);      // #737373
	const FLinearColor TextHint(0.35f, 0.35f, 0.35f, 1.f);           // #595959 — hint/placeholder
	const FLinearColor TextAccent(0.15f, 0.45f, 0.85f, 1.f);         // Unreal blue accent
	const FLinearColor LogTextColor(0.38f, 0.38f, 0.38f, 1.f);       // #616161
	const FLinearColor LogBg(0.008f, 0.008f, 0.008f, 1.f);           // #111111
	const FLinearColor SepColor(0.08f, 0.08f, 0.08f, 1.f);           // #141414 — subtle
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

	// Root canvas
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	// Dark background (same structure as original that worked)
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
	Background->SetBrushColor(FalUI::PanelBg);
	Background->SetPadding(FMargin(20.f));

	UCanvasPanelSlot* BgSlot = RootCanvas->AddChildToCanvas(Background);
	BgSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	BgSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	BgSlot->SetSize(FVector2D(450.f, 500.f));
	BgSlot->SetPosition(FVector2D(0.f, 0.f));

	// Single vertical box (flat — same as original)
	UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VBox"));
	Background->SetContent(VBox);

	// ── Header row: Logo + Title ──
	UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeaderRow"));
	UVerticalBoxSlot* HeaderRowSlot = VBox->AddChildToVerticalBox(HeaderRow);
	HeaderRowSlot->SetHorizontalAlignment(HAlign_Center);
	HeaderRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

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

	// Title
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("fal.ai 3D Generator")));
	FSlateFontInfo TitleFont = Title->GetFont();
	TitleFont.Size = 18;
	Title->SetFont(TitleFont);
	Title->SetColorAndOpacity(FSlateColor(FalUI::TextPrimary));
	UHorizontalBoxSlot* TitleSlot = HeaderRow->AddChildToHorizontalBox(Title);
	TitleSlot->SetVerticalAlignment(VAlign_Center);

	// ── Separator ──
	UBorder* Sep1 = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Sep1"));
	Sep1->SetBrushColor(FalUI::SepColor);
	USpacer* Sep1Inner = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("Sep1Inner"));
	Sep1Inner->SetSize(FVector2D(0.f, 1.f));
	Sep1->SetContent(Sep1Inner);
	UVerticalBoxSlot* Sep1Slot = VBox->AddChildToVerticalBox(Sep1);
	Sep1Slot->SetHorizontalAlignment(HAlign_Fill);
	Sep1Slot->SetPadding(FMargin(0.f, 6.f, 0.f, 10.f));

	// ── Prompt label ──
	UTextBlock* PromptLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PromptLabel"));
	PromptLabel->SetText(FText::FromString(TEXT("PROMPT")));
	FSlateFontInfo LabelFont = PromptLabel->GetFont();
	LabelFont.Size = 9;
	PromptLabel->SetFont(LabelFont);
	PromptLabel->SetColorAndOpacity(FSlateColor(FalUI::TextSecondary));
	UVerticalBoxSlot* LabelSlot = VBox->AddChildToVerticalBox(PromptLabel);
	LabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	// ── Prompt input ──
	PromptInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("PromptInput"));
	PromptInput->SetHintText(FText::FromString(TEXT("Describe what you want to generate...")));

	// Dark input field matching Unreal editor style
	PromptInput->WidgetStyle.BackgroundImageNormal.TintColor = FSlateColor(FalUI::InputBg);
	PromptInput->WidgetStyle.BackgroundImageHovered.TintColor = FSlateColor(FLinearColor(0.015f, 0.015f, 0.015f, 1.f));
	PromptInput->WidgetStyle.BackgroundImageFocused.TintColor = FSlateColor(FLinearColor(0.015f, 0.015f, 0.015f, 1.f));
	PromptInput->WidgetStyle.ForegroundColor = FSlateColor(FalUI::TextPrimary);

	UVerticalBoxSlot* InputSlot = VBox->AddChildToVerticalBox(PromptInput);
	InputSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	// ── Generate button ──
	GenerateButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("GenerateButton"));
	GenerateButton->SetBackgroundColor(FalUI::BtnPrimary);
	GenerateButton->OnClicked.AddDynamic(this, &UFalGeneratorWidget::OnGenerateClicked);

	UTextBlock* BtnText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GenerateBtnText"));
	BtnText->SetText(FText::FromString(TEXT("Generate 3D Model")));
	FSlateFontInfo BtnFont = BtnText->GetFont();
	BtnFont.Size = 12;
	BtnText->SetFont(BtnFont);
	BtnText->SetColorAndOpacity(FSlateColor(FalUI::TextPrimary));
	BtnText->SetJustification(ETextJustify::Center);
	GenerateButton->AddChild(BtnText);

	UVerticalBoxSlot* GenBtnSlot = VBox->AddChildToVerticalBox(GenerateButton);
	GenBtnSlot->SetHorizontalAlignment(HAlign_Fill);
	GenBtnSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	// ── Status text ──
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetText(FText::FromString(TEXT("")));
	FSlateFontInfo StatusFont = StatusText->GetFont();
	StatusFont.Size = 10;
	StatusText->SetFont(StatusFont);
	StatusText->SetColorAndOpacity(FSlateColor(FalUI::TextAccent));
	StatusText->SetAutoWrapText(true);
	UVerticalBoxSlot* StatusSlot = VBox->AddChildToVerticalBox(StatusText);
	StatusSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// ── Separator ──
	UBorder* Sep2 = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Sep2"));
	Sep2->SetBrushColor(FalUI::SepColor);
	USpacer* Sep2Inner = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("Sep2Inner"));
	Sep2Inner->SetSize(FVector2D(0.f, 1.f));
	Sep2->SetContent(Sep2Inner);
	UVerticalBoxSlot* Sep2Slot = VBox->AddChildToVerticalBox(Sep2);
	Sep2Slot->SetHorizontalAlignment(HAlign_Fill);
	Sep2Slot->SetPadding(FMargin(0.f, 2.f, 0.f, 4.f));

	// ── Log header label ──
	UTextBlock* LogLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LogLabel"));
	LogLabel->SetText(FText::FromString(TEXT("OUTPUT LOG")));
	FSlateFontInfo LogLabelFont = LogLabel->GetFont();
	LogLabelFont.Size = 8;
	LogLabel->SetFont(LogLabelFont);
	LogLabel->SetColorAndOpacity(FSlateColor(FalUI::TextSecondary));
	UVerticalBoxSlot* LogLabelSlot = VBox->AddChildToVerticalBox(LogLabel);
	LogLabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	// ── Log area: border + scroll box ──
	UBorder* LogBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LogBorder"));
	LogBorder->SetBrushColor(FalUI::LogBg);
	LogBorder->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));

	LogScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("LogScrollBox"));
	LogBorder->SetContent(LogScrollBox);

	LogText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LogText"));
	LogText->SetText(FText::FromString(TEXT("Ready.")));
	FSlateFontInfo LogFont = LogText->GetFont();
	LogFont.Size = 8;
	LogText->SetFont(LogFont);
	LogText->SetColorAndOpacity(FSlateColor(FalUI::LogTextColor));
	LogText->SetAutoWrapText(true);
	LogScrollBox->AddChild(LogText);

	UVerticalBoxSlot* LogSlot = VBox->AddChildToVerticalBox(LogBorder);
	LogSlot->SetHorizontalAlignment(HAlign_Fill);
	LogSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LogSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	// ── Close button ──
	CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton"));
	CloseButton->SetBackgroundColor(FalUI::BtnDanger);
	CloseButton->OnClicked.AddDynamic(this, &UFalGeneratorWidget::OnCloseClicked);

	UTextBlock* CloseBtnText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseBtnText"));
	CloseBtnText->SetText(FText::FromString(TEXT("Close (Tab)")));
	FSlateFontInfo CloseFont = CloseBtnText->GetFont();
	CloseFont.Size = 10;
	CloseBtnText->SetFont(CloseFont);
	CloseBtnText->SetColorAndOpacity(FSlateColor(FalUI::TextPrimary));
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

		// Rotation with ease-in-out per quarter
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

	// Spinner active during generation (not on final/error states)
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

void UFalGeneratorWidget::OnGenerateClicked()
{
	if (PromptInput)
	{
		FString Prompt = PromptInput->GetText().ToString();
		if (!Prompt.IsEmpty())
		{
			AddLogLine(FString::Printf(TEXT("Generating: \"%s\""), *Prompt));
			OnGenerateRequested.Broadcast(Prompt);
		}
	}
}

void UFalGeneratorWidget::OnCloseClicked()
{
	OnCloseRequested.Broadcast();
}
