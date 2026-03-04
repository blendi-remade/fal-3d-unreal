#include "FalGeneratorWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "Blueprint/WidgetTree.h"

void UFalGeneratorWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Root canvas
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	// Dark background border
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
	Background->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.85f));
	Background->SetPadding(FMargin(20.f));

	UCanvasPanelSlot* BgSlot = RootCanvas->AddChildToCanvas(Background);
	BgSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	BgSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	BgSlot->SetSize(FVector2D(420.f, 340.f));
	BgSlot->SetPosition(FVector2D(0.f, 0.f));

	// Vertical box layout
	UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VBox"));
	Background->SetContent(VBox);

	// Title
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("fal.ai 3D Generator")));
	FSlateFontInfo TitleFont = Title->GetFont();
	TitleFont.Size = 20;
	Title->SetFont(TitleFont);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	UVerticalBoxSlot* TitleSlot = VBox->AddChildToVerticalBox(Title);
	TitleSlot->SetHorizontalAlignment(HAlign_Center);
	TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	// Prompt label
	UTextBlock* PromptLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PromptLabel"));
	PromptLabel->SetText(FText::FromString(TEXT("Prompt:")));
	PromptLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)));
	UVerticalBoxSlot* LabelSlot = VBox->AddChildToVerticalBox(PromptLabel);
	LabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	// Prompt input
	PromptInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("PromptInput"));
	PromptInput->SetHintText(FText::FromString(TEXT("e.g. a cute robot toy")));
	UVerticalBoxSlot* InputSlot = VBox->AddChildToVerticalBox(PromptInput);
	InputSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	// Spacer
	USpacer* Spacer1 = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("Spacer1"));
	Spacer1->SetSize(FVector2D(0.f, 4.f));
	VBox->AddChildToVerticalBox(Spacer1);

	// Generate button
	GenerateButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("GenerateButton"));
	GenerateButton->SetBackgroundColor(FLinearColor(0.0f, 0.45f, 0.85f, 1.f));
	GenerateButton->OnClicked.AddDynamic(this, &UFalGeneratorWidget::OnGenerateClicked);

	UTextBlock* BtnText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GenerateBtnText"));
	BtnText->SetText(FText::FromString(TEXT("Generate 3D Model")));
	BtnText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	BtnText->SetJustification(ETextJustify::Center);
	GenerateButton->AddChild(BtnText);

	UVerticalBoxSlot* GenBtnSlot = VBox->AddChildToVerticalBox(GenerateButton);
	GenBtnSlot->SetHorizontalAlignment(HAlign_Fill);
	GenBtnSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	// Status text
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetText(FText::FromString(TEXT("")));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 1.0f)));
	StatusText->SetAutoWrapText(true);
	UVerticalBoxSlot* StatusSlot = VBox->AddChildToVerticalBox(StatusText);
	StatusSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	// Close button
	CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton"));
	CloseButton->SetBackgroundColor(FLinearColor(0.4f, 0.1f, 0.1f, 1.f));
	CloseButton->OnClicked.AddDynamic(this, &UFalGeneratorWidget::OnCloseClicked);

	UTextBlock* CloseBtnText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseBtnText"));
	CloseBtnText->SetText(FText::FromString(TEXT("Close (Tab)")));
	CloseBtnText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	CloseBtnText->SetJustification(ETextJustify::Center);
	CloseButton->AddChild(CloseBtnText);

	UVerticalBoxSlot* CloseBtnSlot = VBox->AddChildToVerticalBox(CloseButton);
	CloseBtnSlot->SetHorizontalAlignment(HAlign_Fill);
}

void UFalGeneratorWidget::UpdateStatus(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
	}
}

void UFalGeneratorWidget::SetGenerateEnabled(bool bEnabled)
{
	if (GenerateButton)
	{
		GenerateButton->SetIsEnabled(bEnabled);
	}
}

void UFalGeneratorWidget::OnGenerateClicked()
{
	if (PromptInput)
	{
		FString Prompt = PromptInput->GetText().ToString();
		if (!Prompt.IsEmpty())
		{
			OnGenerateRequested.Broadcast(Prompt);
		}
	}
}

void UFalGeneratorWidget::OnCloseClicked()
{
	OnCloseRequested.Broadcast();
}
