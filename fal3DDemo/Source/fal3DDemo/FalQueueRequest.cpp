#include "FalQueueRequest.h"
#include "HttpModule.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogFalQueue, Log, All);

FString UFalQueueRequest::CachedApiKey;
const float UFalQueueRequest::PollIntervalSeconds = 3.0f;

//////////////////////////////////////////////////////////////////////////
// API key

FString UFalQueueRequest::GetApiKey()
{
	if (!CachedApiKey.IsEmpty())
	{
		return CachedApiKey;
	}

	// 1. <ProjectDir>/.env
	const FString EnvPath = FPaths::Combine(FPaths::ProjectDir(), TEXT(".env"));
	FString EnvContents;
	if (FFileHelper::LoadFileToString(EnvContents, *EnvPath))
	{
		TArray<FString> Lines;
		EnvContents.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("#")))
			{
				continue;
			}

			FString Key, Value;
			if (!Trimmed.Split(TEXT("="), &Key, &Value))
			{
				continue;
			}

			if (Key.TrimStartAndEnd() != TEXT("FAL_KEY"))
			{
				continue;
			}

			Value = Value.TrimStartAndEnd();
			if (Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")))
			{
				Value = Value.Mid(1, Value.Len() - 2);
			}

			CachedApiKey = Value;
			UE_LOG(LogFalQueue, Log, TEXT("Loaded FAL_KEY from .env file"));
			break;
		}
	}

	// 2. OS environment variable
	if (CachedApiKey.IsEmpty())
	{
		CachedApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("FAL_KEY"));
		if (!CachedApiKey.IsEmpty())
		{
			UE_LOG(LogFalQueue, Log, TEXT("Loaded FAL_KEY from environment variable"));
		}
	}

	return CachedApiKey;
}

//////////////////////////////////////////////////////////////////////////
// Lifecycle

void UFalQueueRequest::Start(const FString& EndpointUrl, const TSharedRef<FJsonObject>& Body,
	FOnFalQueueComplete OnComplete, FOnFalQueueProgress OnProgress)
{
	Cancel();

	bActive = true;
	CompleteDelegate = OnComplete;
	ProgressDelegate = OnProgress;
	StatusUrl.Empty();
	ResponseUrl.Empty();
	LastProgressMessage.Empty();

	FString BodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(Body, Writer);

	UE_LOG(LogFalQueue, Log, TEXT("Submitting to %s (%d bytes)"), *EndpointUrl, BodyString.Len());

	SendRequest(EndpointUrl, TEXT("POST"), BodyString,
		[this](FHttpResponsePtr Response, bool bConnected) { OnSubmitResponse(Response, bConnected); });
}

void UFalQueueRequest::Cancel()
{
	StopPolling();
	Generation++;
	bActive = false;
	CompleteDelegate.Unbind();
	ProgressDelegate.Unbind();
}

void UFalQueueRequest::Finish(TSharedPtr<FJsonObject> Result, const FString& Error)
{
	StopPolling();
	Generation++;
	bActive = false;

	if (!Error.IsEmpty())
	{
		UE_LOG(LogFalQueue, Error, TEXT("Request failed: %s"), *Error);
	}

	// Copy so the delegate can safely start a new request from inside the callback.
	FOnFalQueueComplete Delegate = CompleteDelegate;
	CompleteDelegate.Unbind();
	ProgressDelegate.Unbind();
	Delegate.ExecuteIfBound(Result, Error);
}

//////////////////////////////////////////////////////////////////////////
// HTTP plumbing

UWorld* UFalQueueRequest::FindWorld() const
{
	for (const UObject* Outer = this; Outer; Outer = Outer->GetOuter())
	{
		if (UWorld* World = Outer->GetWorld())
		{
			return World;
		}
	}
	return nullptr;
}

void UFalQueueRequest::SendRequest(const FString& Url, const FString& Verb, const FString& Body,
	TFunction<void(FHttpResponsePtr, bool)> Handler)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
	Http->SetURL(Url);
	Http->SetVerb(Verb);
	Http->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Key %s"), *GetApiKey()));
	if (!Body.IsEmpty())
	{
		Http->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Http->SetContentAsString(Body);
	}

	const int32 RequestGeneration = Generation;
	Http->OnProcessRequestComplete().BindWeakLambda(this,
		[this, RequestGeneration, Handler](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			if (!bActive || RequestGeneration != Generation)
			{
				return; // Cancelled or superseded while in flight
			}
			Handler(Response, bConnected);
		});
	Http->ProcessRequest();
}

TSharedPtr<FJsonObject> UFalQueueRequest::ParseJson(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
	{
		return Json;
	}
	return nullptr;
}

FString UFalQueueRequest::ExtractErrorDetail(const TSharedPtr<FJsonObject>& Json, int32 ResponseCode)
{
	if (Json.IsValid())
	{
		FString Detail;
		if (Json->TryGetStringField(TEXT("detail"), Detail) && !Detail.IsEmpty())
		{
			return Detail;
		}

		const TArray<TSharedPtr<FJsonValue>>* DetailArray = nullptr;
		if (Json->TryGetArrayField(TEXT("detail"), DetailArray))
		{
			TArray<FString> Messages;
			for (const TSharedPtr<FJsonValue>& Item : *DetailArray)
			{
				const TSharedPtr<FJsonObject>* ItemObj = nullptr;
				FString Msg;
				if (Item->TryGetObject(ItemObj) && (*ItemObj)->TryGetStringField(TEXT("msg"), Msg))
				{
					Messages.Add(Msg);
				}
				else if (Item->Type == EJson::String)
				{
					Messages.Add(Item->AsString());
				}
			}
			if (Messages.Num() > 0)
			{
				return FString::Join(Messages, TEXT("; "));
			}
		}

		FString Message;
		if (Json->TryGetStringField(TEXT("message"), Message) && !Message.IsEmpty())
		{
			return Message;
		}
	}

	return ResponseCode > 0 ? FString::Printf(TEXT("HTTP %d"), ResponseCode) : FString(TEXT("Unknown error"));
}

//////////////////////////////////////////////////////////////////////////
// Submit

void UFalQueueRequest::OnSubmitResponse(FHttpResponsePtr Response, bool bConnected)
{
	if (!bConnected || !Response.IsValid())
	{
		Finish(nullptr, TEXT("Connection to fal.ai failed"));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	const FString Body = Response->GetContentAsString();
	UE_LOG(LogFalQueue, Log, TEXT("Submit response [%d]: %s"), Code, *Body);

	TSharedPtr<FJsonObject> Json = ParseJson(Body);
	if (Code < 200 || Code >= 300)
	{
		Finish(nullptr, ExtractErrorDetail(Json, Code));
		return;
	}
	if (!Json.IsValid())
	{
		Finish(nullptr, TEXT("Could not parse fal.ai submit response"));
		return;
	}

	const FString RequestId = Json->GetStringField(TEXT("request_id"));
	StatusUrl = Json->GetStringField(TEXT("status_url"));
	ResponseUrl = Json->GetStringField(TEXT("response_url"));

	if (RequestId.IsEmpty() || StatusUrl.IsEmpty() || ResponseUrl.IsEmpty())
	{
		Finish(nullptr, TEXT("fal.ai submit response is missing request_id / status_url / response_url"));
		return;
	}

	UE_LOG(LogFalQueue, Log, TEXT("Queued request %s"), *RequestId);
	ReportProgress(TEXT("In queue..."));

	if (UWorld* World = FindWorld())
	{
		World->GetTimerManager().SetTimer(PollTimerHandle,
			FTimerDelegate::CreateUObject(this, &UFalQueueRequest::PollStatus), PollIntervalSeconds, true);
	}
	else
	{
		UE_LOG(LogFalQueue, Error, TEXT("No world available for polling timer; polling once"));
		PollStatus();
	}
}

//////////////////////////////////////////////////////////////////////////
// Poll

void UFalQueueRequest::PollStatus()
{
	// logs=1 lets us show the model's own progress lines (e.g. Meshy stage names) to the user.
	SendRequest(StatusUrl + TEXT("?logs=1"), TEXT("GET"), FString(),
		[this](FHttpResponsePtr Response, bool bConnected) { OnStatusResponse(Response, bConnected); });
}

void UFalQueueRequest::OnStatusResponse(FHttpResponsePtr Response, bool bConnected)
{
	if (!bConnected || !Response.IsValid())
	{
		UE_LOG(LogFalQueue, Warning, TEXT("Status poll connection failed, will retry"));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	const FString Body = Response->GetContentAsString();
	UE_LOG(LogFalQueue, Verbose, TEXT("Status [%d]: %s"), Code, *Body);

	TSharedPtr<FJsonObject> Json = ParseJson(Body);

	if (Code >= 500)
	{
		UE_LOG(LogFalQueue, Warning, TEXT("Status poll returned HTTP %d, will retry"), Code);
		return;
	}
	if (Code < 200 || Code >= 300)
	{
		Finish(nullptr, ExtractErrorDetail(Json, Code));
		return;
	}
	if (!Json.IsValid())
	{
		UE_LOG(LogFalQueue, Warning, TEXT("Could not parse status response, will retry"));
		return;
	}

	const FString Status = Json->GetStringField(TEXT("status"));

	if (Status == TEXT("COMPLETED"))
	{
		StopPolling();
		FetchResult();
		return;
	}

	if (Status == TEXT("FAILED") || Status == TEXT("CANCELLED"))
	{
		Finish(nullptr, FString::Printf(TEXT("Request %s on server"), *Status.ToLower()));
		return;
	}

	// IN_QUEUE / IN_PROGRESS
	FString Message;
	if (Status == TEXT("IN_QUEUE"))
	{
		int32 QueuePosition = 0;
		Message = Json->TryGetNumberField(TEXT("queue_position"), QueuePosition)
			? FString::Printf(TEXT("In queue (position %d)"), QueuePosition)
			: FString(TEXT("In queue..."));
	}
	else
	{
		const TArray<TSharedPtr<FJsonValue>>* Logs = nullptr;
		if (Json->TryGetArrayField(TEXT("logs"), Logs) && Logs->Num() > 0)
		{
			const TSharedPtr<FJsonObject>* LastLog = nullptr;
			if ((*Logs)[Logs->Num() - 1]->TryGetObject(LastLog))
			{
				Message = (*LastLog)->GetStringField(TEXT("message")).TrimStartAndEnd();
			}
		}
		if (Message.IsEmpty())
		{
			Message = TEXT("Processing...");
		}
	}
	ReportProgress(Message);
}

void UFalQueueRequest::StopPolling()
{
	if (UWorld* World = FindWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}
}

void UFalQueueRequest::ReportProgress(const FString& Message)
{
	if (Message == LastProgressMessage)
	{
		return;
	}
	LastProgressMessage = Message;
	ProgressDelegate.ExecuteIfBound(Message);
}

//////////////////////////////////////////////////////////////////////////
// Result

void UFalQueueRequest::FetchResult()
{
	ReportProgress(TEXT("Fetching result..."));
	SendRequest(ResponseUrl, TEXT("GET"), FString(),
		[this](FHttpResponsePtr Response, bool bConnected) { OnResultResponse(Response, bConnected); });
}

void UFalQueueRequest::OnResultResponse(FHttpResponsePtr Response, bool bConnected)
{
	if (!bConnected || !Response.IsValid())
	{
		Finish(nullptr, TEXT("Failed to fetch result from fal.ai"));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	const FString Body = Response->GetContentAsString();
	UE_LOG(LogFalQueue, Log, TEXT("Result [%d]: %s"), Code, *Body);

	TSharedPtr<FJsonObject> Json = ParseJson(Body);
	if (!Json.IsValid())
	{
		Finish(nullptr, FString::Printf(TEXT("Could not parse result (HTTP %d)"), Code));
		return;
	}

	// fal reports model-side failures (content policy, validation) as a 'detail' payload,
	// sometimes even with a COMPLETED queue status and a 2xx code.
	if (Code < 200 || Code >= 300 || Json->HasField(TEXT("detail")))
	{
		Finish(nullptr, ExtractErrorDetail(Json, Code));
		return;
	}

	Finish(Json, FString());
}
