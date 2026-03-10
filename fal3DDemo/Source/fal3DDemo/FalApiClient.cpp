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

DEFINE_LOG_CATEGORY_STATIC(LogFalApi, Log, All);

const FString UFalApiClient::BaseUrl = TEXT("https://queue.fal.run/fal-ai/hunyuan-3d/v3.1/pro/text-to-3d");

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

void UFalApiClient::GenerateModel(const FString& Prompt)
{
	if (CurrentState != EFalGenerationState::Idle && CurrentState != EFalGenerationState::Completed && CurrentState != EFalGenerationState::Error)
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
	SetState(EFalGenerationState::Submitting, TEXT("Submitting request..."));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(BaseUrl);
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
		// Try to get world from outer chain
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
