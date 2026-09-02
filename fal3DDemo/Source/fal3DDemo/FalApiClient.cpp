#include "FalApiClient.h"
#include "FalQueueRequest.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Base64.h"

DEFINE_LOG_CATEGORY_STATIC(LogFalApi, Log, All);

const FString UFalApiClient::ConceptImageUrl = TEXT("https://queue.fal.run/fal-ai/nano-banana-2");
const FString UFalApiClient::ImageTo3DUrl = TEXT("https://queue.fal.run/meshy/v7/image-to-3d");

// Meshy default is 30k. Higher gives more surface detail for the character; the rigging
// step and glTFRuntime handle 100k comfortably at runtime.
const int32 UFalApiClient::TargetPolycount = 100000;
// Meshy-7 ultra mode: higher-fidelity geometry on the preview step (standard model_type only).
const bool UFalApiClient::bUltraMode = true;
// PBR maps (metallic/roughness/normal) cost extra refine time; base color alone matches the old pipeline.
const bool UFalApiClient::bEnablePbr = false;

//////////////////////////////////////////////////////////////////////////
// State

void UFalApiClient::SetState(EFalGenerationState NewState, const FString& Message)
{
	if (NewState == CurrentState && Message == StatusMessage)
	{
		return;
	}
	CurrentState = NewState;
	StatusMessage = Message;
	OnStateChanged.Broadcast(NewState);
}

void UFalApiClient::Fail(const FString& Error)
{
	SetState(EFalGenerationState::Error, Error);
	OnGenerationComplete.Broadcast(FString(), Error);
}

bool UFalApiClient::IsGenerating() const
{
	return Request && Request->IsActive();
}

bool UFalApiClient::BeginRequest()
{
	if (IsGenerating())
	{
		UE_LOG(LogFalApi, Warning, TEXT("Generation already in progress"));
		return false;
	}

	if (UFalQueueRequest::GetApiKey().IsEmpty())
	{
		Fail(TEXT("FAL_KEY not found. Add it to .env in the project folder or set it as an environment variable."));
		return false;
	}

	if (!Request)
	{
		Request = NewObject<UFalQueueRequest>(this);
	}
	return true;
}

void UFalApiClient::HandleProgress(const FString& Message)
{
	SetState(EFalGenerationState::Polling, FString::Printf(TEXT("%s: %s"), *StageLabel, *Message));
}

//////////////////////////////////////////////////////////////////////////
// Request building

FString UFalApiClient::BuildConceptPrompt(const FString& CharacterPrompt, bool bTPose)
{
	// Everything after the character description exists to make the image easy to rig:
	// one character, straight on, limbs clearly separated from the torso, nothing in the background.
	const TCHAR* Pose = bTPose
		? TEXT("arms held perfectly straight out horizontally to both sides in a T-pose, palms facing down, "
		       "with a clear gap between each arm and the torso")
		: TEXT("arms held perfectly straight and angled about 45 degrees down and away from the body in an A-pose, "
		       "palms facing inward, with a clear gap between each arm and the torso");

	return FString::Printf(
		TEXT("Full-body character design reference sheet of a single character: %s. ")
		TEXT("The character stands upright facing directly toward the camera in a symmetrical front view, %s. ")
		TEXT("Legs straight and slightly apart, feet flat and pointing forward, head level, neutral expression. ")
		TEXT("The entire body is visible from the top of the head to the soles of the feet, centered, with empty margin around it. ")
		TEXT("Plain solid pure white background, soft even studio lighting, no cast shadows, no floor, no text, no watermark, no other objects. ")
		TEXT("Clean high-detail stylized 3D game character render with a crisp silhouette."),
		*CharacterPrompt, Pose);
}

void UFalApiClient::AddMeshSettings(const TSharedRef<FJsonObject>& Body, bool bTPose) const
{
	Body->SetStringField(TEXT("model_type"), TEXT("standard"));
	Body->SetStringField(TEXT("topology"), TEXT("triangle"));
	Body->SetNumberField(TEXT("target_polycount"), TargetPolycount);
	Body->SetBoolField(TEXT("should_remesh"), true);
	Body->SetStringField(TEXT("symmetry_mode"), TEXT("auto"));
	Body->SetBoolField(TEXT("enable_pbr"), bEnablePbr);
	Body->SetBoolField(TEXT("ultra_mode"), bUltraMode);
	Body->SetBoolField(TEXT("should_texture"), true);

	// A rig-friendly pose. T-pose keeps arms well clear of the torso, which avoids
	// limb/torso vertex blending on characters whose arms hang close to the body.
	Body->SetStringField(TEXT("pose_mode"), bTPose ? TEXT("t-pose") : TEXT("a-pose"));
}

//////////////////////////////////////////////////////////////////////////
// Public entry points

void UFalApiClient::GenerateModel(const FString& Prompt, bool bTPose)
{
	if (!BeginRequest())
	{
		return;
	}

	// Text path always uses A-pose for both the concept image and Meshy pose_mode. Keeping a single,
	// consistent pose gives the most predictable rigging; the T-pose checkbox only affects your own photos.
	if (bTPose)
	{
		UE_LOG(LogFalApi, Log, TEXT("T-pose requested but the text path is fixed to A-pose; ignoring"));
	}
	bPendingTPose = false;
	UE_LOG(LogFalApi, Log, TEXT("Text-to-3D: \"%s\" (a-pose)"), *Prompt);
	SubmitConceptImage(Prompt);
}

void UFalApiClient::GenerateModelFromImage(const FString& LocalImagePath, bool bTPose)
{
	if (!BeginRequest())
	{
		return;
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *LocalImagePath))
	{
		Fail(FString::Printf(TEXT("Failed to read image: %s"), *LocalImagePath));
		return;
	}

	const FString Extension = FPaths::GetExtension(LocalImagePath).ToLower();
	FString MimeType;
	if (Extension == TEXT("png"))
	{
		MimeType = TEXT("image/png");
	}
	else if (Extension == TEXT("jpg") || Extension == TEXT("jpeg"))
	{
		MimeType = TEXT("image/jpeg");
	}
	else
	{
		Fail(FString::Printf(TEXT("Unsupported image type '.%s'. Use PNG or JPG."), *Extension));
		return;
	}

	bPendingTPose = bTPose;
	UE_LOG(LogFalApi, Log, TEXT("Image-to-3D: %s (%d bytes, %s, %s)"),
		*FPaths::GetCleanFilename(LocalImagePath), FileData.Num(), *MimeType, bTPose ? TEXT("t-pose") : TEXT("a-pose"));

	// fal accepts base64 data URIs directly for file inputs, so no upload step is needed.
	const FString DataUrl = FString::Printf(TEXT("data:%s;base64,%s"), *MimeType, *FBase64::Encode(FileData));
	SubmitImageTo3D(DataUrl, TEXT("Submitting image-to-3D request (Meshy-7)..."));
}

//////////////////////////////////////////////////////////////////////////
// Stage 1: concept image (text path only)

void UFalApiClient::SubmitConceptImage(const FString& Prompt)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("prompt"), BuildConceptPrompt(Prompt, bPendingTPose));
	Body->SetNumberField(TEXT("num_images"), 1);
	Body->SetStringField(TEXT("aspect_ratio"), TEXT("3:4"));   // portrait: full body fills the frame
	Body->SetStringField(TEXT("resolution"), TEXT("1K"));
	Body->SetStringField(TEXT("output_format"), TEXT("png"));

	StageLabel = TEXT("Designing character");
	SetState(EFalGenerationState::Submitting, TEXT("Submitting concept image request (nano-banana-2)..."));

	Request->Start(ConceptImageUrl, Body,
		FOnFalQueueComplete::CreateUObject(this, &UFalApiClient::HandleConceptResult),
		FOnFalQueueProgress::CreateUObject(this, &UFalApiClient::HandleProgress));
}

void UFalApiClient::HandleConceptResult(TSharedPtr<FJsonObject> Result, const FString& Error)
{
	if (!Error.IsEmpty())
	{
		Fail(FString::Printf(TEXT("Concept image failed: %s"), *Error));
		return;
	}

	// { "images": [ { "url": "...", ... } ], "description": "..." }
	FString ImageUrl;
	const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
	if (Result->TryGetArrayField(TEXT("images"), Images) && Images->Num() > 0)
	{
		const TSharedPtr<FJsonObject>* First = nullptr;
		if ((*Images)[0]->TryGetObject(First))
		{
			ImageUrl = (*First)->GetStringField(TEXT("url"));
		}
	}

	if (ImageUrl.IsEmpty())
	{
		Fail(TEXT("Concept image finished but the result contains no image URL"));
		return;
	}

	UE_LOG(LogFalApi, Log, TEXT("Concept image: %s"), *ImageUrl);
	OnConceptImageReady.Broadcast(ImageUrl);

	SubmitImageTo3D(ImageUrl, TEXT("Concept ready. Submitting image-to-3D request (Meshy-7)..."));
}

//////////////////////////////////////////////////////////////////////////
// Stage 2: image -> 3D

void UFalApiClient::SubmitImageTo3D(const FString& ImageUrl, const FString& SubmitMessage)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("image_url"), ImageUrl);
	AddMeshSettings(Body, bPendingTPose);

	StageLabel = TEXT("Generating 3D model");
	SetState(EFalGenerationState::Submitting, SubmitMessage);

	Request->Start(ImageTo3DUrl, Body,
		FOnFalQueueComplete::CreateUObject(this, &UFalApiClient::HandleModelResult),
		FOnFalQueueProgress::CreateUObject(this, &UFalApiClient::HandleProgress));
}

void UFalApiClient::HandleModelResult(TSharedPtr<FJsonObject> Result, const FString& Error)
{
	if (!Error.IsEmpty())
	{
		Fail(FString::Printf(TEXT("Generation failed: %s"), *Error));
		return;
	}

	// Preferred: model_glb.url. Fallback: model_urls.glb.url.
	FString GlbUrl;
	const TSharedPtr<FJsonObject>* ModelGlb = nullptr;
	if (Result->TryGetObjectField(TEXT("model_glb"), ModelGlb))
	{
		GlbUrl = (*ModelGlb)->GetStringField(TEXT("url"));
	}
	if (GlbUrl.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* ModelUrls = nullptr;
		const TSharedPtr<FJsonObject>* Glb = nullptr;
		if (Result->TryGetObjectField(TEXT("model_urls"), ModelUrls) && (*ModelUrls)->TryGetObjectField(TEXT("glb"), Glb))
		{
			GlbUrl = (*Glb)->GetStringField(TEXT("url"));
		}
	}

	if (GlbUrl.IsEmpty())
	{
		Fail(TEXT("Generation finished but the result contains no GLB URL"));
		return;
	}

	UE_LOG(LogFalApi, Log, TEXT("GLB URL: %s"), *GlbUrl);
	SetState(EFalGenerationState::Completed, TEXT("Model ready!"));
	OnGenerationComplete.Broadcast(GlbUrl, FString());
}
