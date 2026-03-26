#include "FalApiClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Base64.h"

DEFINE_LOG_CATEGORY_STATIC(LogFalApi, Log, All);

const FString UFalApiClient::TextTo3DUrl = TEXT("https://queue.fal.run/fal-ai/hunyuan-3d/v3.1/pro/text-to-3d");
const FString UFalApiClient::ImageTo3DUrl = TEXT("https://queue.fal.run/fal-ai/hunyuan-3d/v3.1/pro/image-to-3d");
const FString UFalApiClient::NanoBananaEditUrl = TEXT("https://queue.fal.run/fal-ai/nano-banana-pro/edit");

FString UFalApiClient::GetApiKey()
{
	if (ApiKey.IsEmpty())
	{
		// Try .env file in project root first
		FString EnvPath = FPaths::Combine(FPaths::ProjectDir(), TEXT(".env"));
		FString EnvContents;
		if (FFileHelper::LoadFileToString(EnvContents, *EnvPath))
		{
			TArray<FString> Lines;
			EnvContents.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				FString Trimmed = Line.TrimStartAndEnd();
				if (Trimmed.StartsWith(TEXT("#")) || Trimmed.IsEmpty()) continue;

				FString Key, Value;
				if (Trimmed.Split(TEXT("="), &Key, &Value))
				{
					Key = Key.TrimStartAndEnd();
					Value = Value.TrimStartAndEnd();
					// Strip surrounding quotes if present
					if (Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")))
					{
						Value = Value.Mid(1, Value.Len() - 2);
					}
					if (Key == TEXT("FAL_KEY"))
					{
						ApiKey = Value;
						UE_LOG(LogFalApi, Log, TEXT("Loaded FAL_KEY from .env file"));
						break;
					}
				}
			}
		}

		// Fall back to OS environment variable
		if (ApiKey.IsEmpty())
		{
			ApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("FAL_KEY"));
			if (!ApiKey.IsEmpty())
			{
				UE_LOG(LogFalApi, Log, TEXT("Loaded FAL_KEY from environment variable"));
			}
		}
	}
	return ApiKey;
}

void UFalApiClient::SetState(EFalGenerationState NewState, const FString& Message)
{
	CurrentState = NewState;
	StatusMessage = Message;
	OnStateChanged.Broadcast(NewState);
}

bool UFalApiClient::IsGenerating() const
{
	return CurrentState != EFalGenerationState::Idle
		&& CurrentState != EFalGenerationState::Completed
		&& CurrentState != EFalGenerationState::Error;
}

//////////////////////////////////////////////////////////////////////////
// Text-to-3D (existing flow)

void UFalApiClient::GenerateModel(const FString& Prompt)
{
	if (IsGenerating())
	{
		UE_LOG(LogFalApi, Warning, TEXT("Generation already in progress"));
		return;
	}

	if (GetApiKey().IsEmpty())
	{
		SetState(EFalGenerationState::Error, TEXT("FAL_KEY not found. Add it to .env in project root or set as environment variable."));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("FAL_KEY not found. Add it to .env in project root or set as environment variable."));
		return;
	}

	RequestId.Empty();
	SubmitRequest(Prompt);
}

void UFalApiClient::SubmitRequest(const FString& Prompt)
{
	SetState(EFalGenerationState::Submitting, TEXT("Submitting text-to-3D request..."));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(TextTo3DUrl);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Key %s"), *GetApiKey()));

	TSharedPtr<FJsonObject> JsonBody = MakeShareable(new FJsonObject());
	JsonBody->SetStringField(TEXT("prompt"), Prompt);
	JsonBody->SetStringField(TEXT("generate_type"), TEXT("Normal"));
	JsonBody->SetNumberField(TEXT("face_count"), 300000);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonBody.ToSharedRef(), Writer);

	HttpRequest->SetContentAsString(RequestBody);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UFalApiClient::OnSubmitResponse);
	HttpRequest->ProcessRequest();
}

void UFalApiClient::OnSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		SetState(EFalGenerationState::Error, TEXT("Connection failed"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Connection failed"));
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	FString ResponseBody = Response->GetContentAsString();

	UE_LOG(LogFalApi, Log, TEXT("Submit response [%d]: %s"), ResponseCode, *ResponseBody);

	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		SetState(EFalGenerationState::Error, TEXT("Failed to parse submit response"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Failed to parse submit response"));
		return;
	}

	if (ResponseCode != 200)
	{
		FString Detail = JsonResponse->GetStringField(TEXT("detail"));
		if (Detail.IsEmpty()) Detail = FString::Printf(TEXT("HTTP %d"), ResponseCode);
		SetState(EFalGenerationState::Error, Detail);
		OnGenerationComplete.Broadcast(TEXT(""), Detail);
		return;
	}

	RequestId = JsonResponse->GetStringField(TEXT("request_id"));
	StatusUrl = JsonResponse->GetStringField(TEXT("status_url"));
	ResponseUrl = JsonResponse->GetStringField(TEXT("response_url"));

	if (RequestId.IsEmpty())
	{
		SetState(EFalGenerationState::Error, TEXT("No request_id in response"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("No request_id in response"));
		return;
	}

	UE_LOG(LogFalApi, Log, TEXT("Got request_id: %s"), *RequestId);
	UE_LOG(LogFalApi, Log, TEXT("Status URL: %s"), *StatusUrl);
	UE_LOG(LogFalApi, Log, TEXT("Response URL: %s"), *ResponseUrl);
	StartPolling();
}

void UFalApiClient::StartPolling()
{
	SetState(EFalGenerationState::Polling, TEXT("Generating 3D model..."));

	UWorld* World = GetWorld();
	if (!World)
	{
		for (UObject* Outer = GetOuter(); Outer; Outer = Outer->GetOuter())
		{
			World = Outer->GetWorld();
			if (World) break;
		}
	}

	if (World)
	{
		World->GetTimerManager().SetTimer(PollTimerHandle, FTimerDelegate::CreateUObject(this, &UFalApiClient::PollStatus), 3.0f, true);
	}
	else
	{
		UE_LOG(LogFalApi, Error, TEXT("Cannot get World for timer - polling manually once"));
		PollStatus();
	}
}

void UFalApiClient::PollStatus()
{
	FString PollUrl = StatusUrl;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(PollUrl);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Key %s"), *GetApiKey()));
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UFalApiClient::OnPollResponse);
	HttpRequest->ProcessRequest();
}

void UFalApiClient::OnPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		UE_LOG(LogFalApi, Warning, TEXT("Poll connection failed, will retry..."));
		return;
	}

	FString ResponseBody = Response->GetContentAsString();
	UE_LOG(LogFalApi, Log, TEXT("Poll response: %s"), *ResponseBody);

	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		UE_LOG(LogFalApi, Warning, TEXT("Failed to parse poll response, will retry..."));
		return;
	}

	FString Status = JsonResponse->GetStringField(TEXT("status"));

	if (Status == TEXT("COMPLETED"))
	{
		StopPolling();
		FetchResult();
	}
	else if (Status == TEXT("FAILED"))
	{
		StopPolling();
		SetState(EFalGenerationState::Error, TEXT("Generation failed on server"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Generation failed on server"));
	}
	else
	{
		// IN_QUEUE or IN_PROGRESS - keep polling
		SetState(EFalGenerationState::Polling, FString::Printf(TEXT("Status: %s"), *Status));
	}
}

void UFalApiClient::FetchResult()
{
	SetState(EFalGenerationState::FetchingResult, TEXT("Fetching result..."));

	FString ResultUrl = ResponseUrl;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(ResultUrl);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Key %s"), *GetApiKey()));
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UFalApiClient::OnFetchResponse);
	HttpRequest->ProcessRequest();
}

void UFalApiClient::OnFetchResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		SetState(EFalGenerationState::Error, TEXT("Failed to fetch result"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Failed to fetch result"));
		return;
	}

	FString ResponseBody = Response->GetContentAsString();
	UE_LOG(LogFalApi, Log, TEXT("Fetch response: %s"), *ResponseBody);

	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		SetState(EFalGenerationState::Error, TEXT("Failed to parse result"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Failed to parse result"));
		return;
	}

	// Navigate to model_glb.url
	const TSharedPtr<FJsonObject>* ModelGlbObj = nullptr;
	FString GlbUrl;

	if (JsonResponse->TryGetObjectField(TEXT("model_glb"), ModelGlbObj))
	{
		GlbUrl = (*ModelGlbObj)->GetStringField(TEXT("url"));
	}

	// Extract texture URL for rigging
	const TSharedPtr<FJsonObject>* ModelUrlsObj = nullptr;
	if (JsonResponse->TryGetObjectField(TEXT("model_urls"), ModelUrlsObj))
	{
		const TSharedPtr<FJsonObject>* TextureObj = nullptr;
		if ((*ModelUrlsObj)->TryGetObjectField(TEXT("texture"), TextureObj))
		{
			LastTextureUrl = (*TextureObj)->GetStringField(TEXT("url"));
			UE_LOG(LogFalApi, Log, TEXT("Texture URL: %s"), *LastTextureUrl);
		}
	}

	if (GlbUrl.IsEmpty())
	{
		SetState(EFalGenerationState::Error, TEXT("No GLB URL in response"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("No GLB URL in response"));
		return;
	}

	UE_LOG(LogFalApi, Log, TEXT("GLB URL: %s"), *GlbUrl);
	SetState(EFalGenerationState::Completed, TEXT("Model ready!"));
	OnGenerationComplete.Broadcast(GlbUrl, TEXT(""));
}

void UFalApiClient::StopPolling()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		for (UObject* Outer = GetOuter(); Outer; Outer = Outer->GetOuter())
		{
			World = Outer->GetWorld();
			if (World) break;
		}
	}

	if (World)
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}
}

//////////////////////////////////////////////////////////////////////////
// Image-to-3D (new flow: preprocess -> image-to-3d -> same poll/fetch)

void UFalApiClient::GenerateModelFromImage(const FString& LocalImagePath)
{
	if (IsGenerating())
	{
		UE_LOG(LogFalApi, Warning, TEXT("Generation already in progress"));
		return;
	}

	if (GetApiKey().IsEmpty())
	{
		SetState(EFalGenerationState::Error, TEXT("FAL_KEY not found. Add it to .env in project root or set as environment variable."));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("FAL_KEY not found"));
		return;
	}

	// Read image file and base64 encode it
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *LocalImagePath))
	{
		SetState(EFalGenerationState::Error, FString::Printf(TEXT("Failed to read image: %s"), *LocalImagePath));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Failed to read image file"));
		return;
	}

	// Determine MIME type from extension
	FString Extension = FPaths::GetExtension(LocalImagePath).ToLower();
	FString MimeType = TEXT("image/png");
	if (Extension == TEXT("jpg") || Extension == TEXT("jpeg"))
	{
		MimeType = TEXT("image/jpeg");
	}
	else if (Extension == TEXT("webp"))
	{
		MimeType = TEXT("image/webp");
	}

	FString Base64 = FBase64::Encode(FileData);
	FString DataUrl = FString::Printf(TEXT("data:%s;base64,%s"), *MimeType, *Base64);

	UE_LOG(LogFalApi, Log, TEXT("Image loaded: %s (%d bytes, %s)"), *LocalImagePath, FileData.Num(), *MimeType);

	RequestId.Empty();
	PreprocessRequestId.Empty();
	SubmitPreprocessing(DataUrl);
}

void UFalApiClient::SubmitPreprocessing(const FString& Base64DataUrl)
{
	SetState(EFalGenerationState::PreprocessingSubmitting, TEXT("Preprocessing image (A-pose conversion)..."));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(NanoBananaEditUrl);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Key %s"), *GetApiKey()));

	TSharedPtr<FJsonObject> JsonBody = MakeShareable(new FJsonObject());

	// nano-banana-pro/edit takes "image_urls" (array of strings), not "image_url"
	TArray<TSharedPtr<FJsonValue>> ImageUrlsArray;
	ImageUrlsArray.Add(MakeShareable(new FJsonValueString(Base64DataUrl)));
	JsonBody->SetArrayField(TEXT("image_urls"), ImageUrlsArray);

	JsonBody->SetStringField(TEXT("prompt"), TEXT("Photo of the subject in an A-pose, with a white neutral background, clean and no added details"));
	JsonBody->SetNumberField(TEXT("num_images"), 1);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonBody.ToSharedRef(), Writer);

	HttpRequest->SetContentAsString(RequestBody);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UFalApiClient::OnPreprocessSubmitResponse);
	HttpRequest->ProcessRequest();

	UE_LOG(LogFalApi, Log, TEXT("Submitted image to nano-banana-pro/edit for A-pose preprocessing"));
}

void UFalApiClient::OnPreprocessSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		SetState(EFalGenerationState::Error, TEXT("Preprocessing connection failed"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Preprocessing connection failed"));
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	FString ResponseBody = Response->GetContentAsString();

	UE_LOG(LogFalApi, Log, TEXT("Preprocess submit response [%d]: %s"), ResponseCode, *ResponseBody);

	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		SetState(EFalGenerationState::Error, TEXT("Failed to parse preprocessing response"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Failed to parse preprocessing response"));
		return;
	}

	if (ResponseCode != 200)
	{
		FString Detail = JsonResponse->GetStringField(TEXT("detail"));
		if (Detail.IsEmpty()) Detail = FString::Printf(TEXT("HTTP %d"), ResponseCode);
		SetState(EFalGenerationState::Error, FString::Printf(TEXT("Preprocessing failed: %s"), *Detail));
		OnGenerationComplete.Broadcast(TEXT(""), Detail);
		return;
	}

	PreprocessRequestId = JsonResponse->GetStringField(TEXT("request_id"));
	PreprocessStatusUrl = JsonResponse->GetStringField(TEXT("status_url"));
	PreprocessResponseUrl = JsonResponse->GetStringField(TEXT("response_url"));

	if (PreprocessRequestId.IsEmpty())
	{
		SetState(EFalGenerationState::Error, TEXT("No request_id in preprocessing response"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("No request_id in preprocessing response"));
		return;
	}

	UE_LOG(LogFalApi, Log, TEXT("Preprocess request_id: %s"), *PreprocessRequestId);
	StartPreprocessPolling();
}

void UFalApiClient::StartPreprocessPolling()
{
	SetState(EFalGenerationState::PreprocessingPolling, TEXT("Processing image to A-pose..."));

	UWorld* World = GetWorld();
	if (!World)
	{
		for (UObject* Outer = GetOuter(); Outer; Outer = Outer->GetOuter())
		{
			World = Outer->GetWorld();
			if (World) break;
		}
	}

	if (World)
	{
		World->GetTimerManager().SetTimer(PreprocessPollTimerHandle,
			FTimerDelegate::CreateUObject(this, &UFalApiClient::PollPreprocessStatus), 3.0f, true);
	}
	else
	{
		PollPreprocessStatus();
	}
}

void UFalApiClient::PollPreprocessStatus()
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(PreprocessStatusUrl);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Key %s"), *GetApiKey()));
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UFalApiClient::OnPreprocessPollResponse);
	HttpRequest->ProcessRequest();
}

void UFalApiClient::OnPreprocessPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		UE_LOG(LogFalApi, Warning, TEXT("Preprocess poll connection failed, will retry..."));
		return;
	}

	FString ResponseBody = Response->GetContentAsString();
	UE_LOG(LogFalApi, Log, TEXT("Preprocess poll response: %s"), *ResponseBody);

	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		UE_LOG(LogFalApi, Warning, TEXT("Failed to parse preprocess poll response, will retry..."));
		return;
	}

	FString Status = JsonResponse->GetStringField(TEXT("status"));

	if (Status == TEXT("COMPLETED"))
	{
		StopPreprocessPolling();
		FetchPreprocessResult();
	}
	else if (Status == TEXT("FAILED"))
	{
		StopPreprocessPolling();
		SetState(EFalGenerationState::Error, TEXT("Image preprocessing failed on server"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Image preprocessing failed on server"));
	}
	else
	{
		SetState(EFalGenerationState::PreprocessingPolling, FString::Printf(TEXT("Preprocessing: %s"), *Status));
	}
}

void UFalApiClient::FetchPreprocessResult()
{
	SetState(EFalGenerationState::PreprocessingFetching, TEXT("Fetching preprocessed image..."));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(PreprocessResponseUrl);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Key %s"), *GetApiKey()));
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UFalApiClient::OnPreprocessFetchResponse);
	HttpRequest->ProcessRequest();
}

void UFalApiClient::OnPreprocessFetchResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		SetState(EFalGenerationState::Error, TEXT("Failed to fetch preprocessed image"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Failed to fetch preprocessed image"));
		return;
	}

	FString ResponseBody = Response->GetContentAsString();
	UE_LOG(LogFalApi, Warning, TEXT("Preprocess fetch response body: %s"), *ResponseBody);

	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		SetState(EFalGenerationState::Error, TEXT("Failed to parse preprocessed image result"));
		OnGenerationComplete.Broadcast(TEXT(""), TEXT("Failed to parse preprocessed image result"));
		return;
	}

	// Log all top-level keys for debugging
	TArray<FString> Keys;
	JsonResponse->Values.GetKeys(Keys);
	UE_LOG(LogFalApi, Warning, TEXT("Preprocess result keys: %s"), *FString::Join(Keys, TEXT(", ")));

	FString ProcessedImageUrl;

	// Try "images" array: { "images": [{ "url": "..." }] }
	const TArray<TSharedPtr<FJsonValue>>* ImagesArray = nullptr;
	if (JsonResponse->TryGetArrayField(TEXT("images"), ImagesArray) && ImagesArray->Num() > 0)
	{
		const TSharedPtr<FJsonObject>* FirstImage = nullptr;
		if ((*ImagesArray)[0]->TryGetObject(FirstImage))
		{
			ProcessedImageUrl = (*FirstImage)->GetStringField(TEXT("url"));
		}
	}

	// Try "image" object: { "image": { "url": "..." } }
	if (ProcessedImageUrl.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* ImageObj = nullptr;
		if (JsonResponse->TryGetObjectField(TEXT("image"), ImageObj))
		{
			ProcessedImageUrl = (*ImageObj)->GetStringField(TEXT("url"));
		}
	}

	// Try "output" object: { "output": { "url": "..." } }
	if (ProcessedImageUrl.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* OutputObj = nullptr;
		if (JsonResponse->TryGetObjectField(TEXT("output"), OutputObj))
		{
			ProcessedImageUrl = (*OutputObj)->GetStringField(TEXT("url"));
		}
	}

	// Try top-level "url": { "url": "..." }
	if (ProcessedImageUrl.IsEmpty())
	{
		ProcessedImageUrl = JsonResponse->GetStringField(TEXT("url"));
	}

	if (ProcessedImageUrl.IsEmpty())
	{
		FString ErrorDetail = FString::Printf(TEXT("No image URL in preprocessing result. Keys: %s"), *FString::Join(Keys, TEXT(", ")));
		SetState(EFalGenerationState::Error, ErrorDetail);
		OnGenerationComplete.Broadcast(TEXT(""), ErrorDetail);
		return;
	}

	UE_LOG(LogFalApi, Log, TEXT("Preprocessed image URL: %s"), *ProcessedImageUrl);

	// Now submit to image-to-3D
	SubmitImageTo3D(ProcessedImageUrl);
}

void UFalApiClient::StopPreprocessPolling()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		for (UObject* Outer = GetOuter(); Outer; Outer = Outer->GetOuter())
		{
			World = Outer->GetWorld();
			if (World) break;
		}
	}

	if (World)
	{
		World->GetTimerManager().ClearTimer(PreprocessPollTimerHandle);
	}
}

void UFalApiClient::SubmitImageTo3D(const FString& ImageUrl)
{
	SetState(EFalGenerationState::Submitting, TEXT("Submitting image-to-3D request..."));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(ImageTo3DUrl);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Key %s"), *GetApiKey()));

	TSharedPtr<FJsonObject> JsonBody = MakeShareable(new FJsonObject());
	JsonBody->SetStringField(TEXT("input_image_url"), ImageUrl);
	JsonBody->SetStringField(TEXT("generate_type"), TEXT("Normal"));
	JsonBody->SetNumberField(TEXT("face_count"), 300000);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonBody.ToSharedRef(), Writer);

	HttpRequest->SetContentAsString(RequestBody);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UFalApiClient::OnImageTo3DSubmitResponse);
	HttpRequest->ProcessRequest();

	UE_LOG(LogFalApi, Log, TEXT("Submitted preprocessed image to image-to-3D"));
}

void UFalApiClient::OnImageTo3DSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	// Reuse the same response handler as text-to-3D since the queue format is identical
	OnSubmitResponse(Request, Response, bConnectedSuccessfully);
}
