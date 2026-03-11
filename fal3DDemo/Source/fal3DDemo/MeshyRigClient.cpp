#include "MeshyRigClient.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogMeshyRig, Log, All);

const FString UMeshyRigClient::BaseUrl = TEXT("https://api.meshy.ai/openapi/v1");

UWorld* UMeshyRigClient::FindWorld() const
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
	return World;
}

FString UMeshyRigClient::GetApiKey()
{
	if (ApiKey.IsEmpty())
	{
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
					if (Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")))
					{
						Value = Value.Mid(1, Value.Len() - 2);
					}
					if (Key == TEXT("MESHY_KEY"))
					{
						ApiKey = Value;
						UE_LOG(LogMeshyRig, Log, TEXT("Loaded MESHY_KEY from .env file"));
						break;
					}
				}
			}
		}

		if (ApiKey.IsEmpty())
		{
			ApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("MESHY_KEY"));
			if (!ApiKey.IsEmpty())
			{
				UE_LOG(LogMeshyRig, Log, TEXT("Loaded MESHY_KEY from environment variable"));
			}
		}
	}
	return ApiKey;
}

void UMeshyRigClient::SetState(EMeshyRigState NewState, const FString& Message)
{
	CurrentState = NewState;
	StatusMessage = Message;
	OnStateChanged.Broadcast(NewState);
}

void UMeshyRigClient::RigAndAnimate(const FString& GlbUrl, const FString& TextureUrl)
{
	if (CurrentState != EMeshyRigState::Idle && CurrentState != EMeshyRigState::Completed && CurrentState != EMeshyRigState::Error)
	{
		UE_LOG(LogMeshyRig, Warning, TEXT("Rigging already in progress"));
		return;
	}

	if (GetApiKey().IsEmpty())
	{
		SetState(EMeshyRigState::Error, TEXT("MESHY_KEY not found. Add it to .env or set as environment variable."));
		OnRiggingComplete.Broadcast(FRiggedCharacterUrls(), TEXT("MESHY_KEY not found."));
		return;
	}

	RigTaskId.Empty();
	AnimTasks.Empty();
	ResultUrls = FRiggedCharacterUrls();
	CachedTextureUrl = TextureUrl;
	SubmitRigging(GlbUrl);
}

// ============================================================
// Rigging
// ============================================================

void UMeshyRigClient::SubmitRigging(const FString& GlbUrl)
{
	SetState(EMeshyRigState::RiggingSubmitting, TEXT("Submitting for rigging..."));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(BaseUrl + TEXT("/rigging"));
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *GetApiKey()));

	TSharedPtr<FJsonObject> JsonBody = MakeShareable(new FJsonObject());
	JsonBody->SetStringField(TEXT("model_url"), GlbUrl);
	JsonBody->SetNumberField(TEXT("height_meters"), 1.7);
	if (!CachedTextureUrl.IsEmpty())
	{
		JsonBody->SetStringField(TEXT("texture_image_url"), CachedTextureUrl);
	}

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonBody.ToSharedRef(), Writer);

	UE_LOG(LogMeshyRig, Log, TEXT("Submitting rigging with model_url: %s"), *GlbUrl);

	HttpRequest->SetContentAsString(RequestBody);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UMeshyRigClient::OnRiggingSubmitResponse);
	HttpRequest->ProcessRequest();
}

void UMeshyRigClient::OnRiggingSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		SetState(EMeshyRigState::Error, TEXT("Rigging connection failed"));
		OnRiggingComplete.Broadcast(FRiggedCharacterUrls(), TEXT("Rigging connection failed"));
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	FString ResponseBody = Response->GetContentAsString();
	UE_LOG(LogMeshyRig, Log, TEXT("Rigging submit response [%d]: %s"), ResponseCode, *ResponseBody);

	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		SetState(EMeshyRigState::Error, TEXT("Failed to parse rigging response"));
		OnRiggingComplete.Broadcast(FRiggedCharacterUrls(), TEXT("Failed to parse rigging response"));
		return;
	}

	if (ResponseCode < 200 || ResponseCode >= 300)
	{
		FString Message = JsonResponse->GetStringField(TEXT("message"));
		if (Message.IsEmpty()) Message = FString::Printf(TEXT("HTTP %d"), ResponseCode);
		SetState(EMeshyRigState::Error, Message);
		OnRiggingComplete.Broadcast(FRiggedCharacterUrls(), Message);
		return;
	}

	RigTaskId = JsonResponse->GetStringField(TEXT("result"));
	if (RigTaskId.IsEmpty())
	{
		SetState(EMeshyRigState::Error, TEXT("No task ID in rigging response"));
		OnRiggingComplete.Broadcast(FRiggedCharacterUrls(), TEXT("No task ID in rigging response"));
		return;
	}

	UE_LOG(LogMeshyRig, Log, TEXT("Rigging task ID: %s"), *RigTaskId);
	StartRiggingPoll();
}

void UMeshyRigClient::StartRiggingPoll()
{
	SetState(EMeshyRigState::RiggingPolling, TEXT("Rigging character..."));

	UWorld* World = FindWorld();
	if (World)
	{
		World->GetTimerManager().SetTimer(PollTimerHandle, FTimerDelegate::CreateUObject(this, &UMeshyRigClient::PollRigging), 3.0f, true);
	}
}

void UMeshyRigClient::PollRigging()
{
	FString PollUrl = FString::Printf(TEXT("%s/rigging/%s"), *BaseUrl, *RigTaskId);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(PollUrl);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *GetApiKey()));
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UMeshyRigClient::OnRiggingPollResponse);
	HttpRequest->ProcessRequest();
}

void UMeshyRigClient::OnRiggingPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		UE_LOG(LogMeshyRig, Warning, TEXT("Rigging poll connection failed, will retry..."));
		return;
	}

	FString ResponseBody = Response->GetContentAsString();
	UE_LOG(LogMeshyRig, Log, TEXT("Rigging poll: %s"), *ResponseBody);

	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		UE_LOG(LogMeshyRig, Warning, TEXT("Failed to parse rigging poll, will retry..."));
		return;
	}

	FString Status = JsonResponse->GetStringField(TEXT("status"));
	int32 Progress = JsonResponse->GetIntegerField(TEXT("progress"));

	if (Status == TEXT("SUCCEEDED"))
	{
		StopPolling();

		// Extract result URLs
		const TSharedPtr<FJsonObject>* ResultObj = nullptr;
		if (JsonResponse->TryGetObjectField(TEXT("result"), ResultObj))
		{
			ResultUrls.RiggedGlbUrl = (*ResultObj)->GetStringField(TEXT("rigged_character_glb_url"));
			// Skip basic_animations walk/run — they have inconsistent bone scaling.
			// All animations are requested via the Animation API instead.
		}

		UE_LOG(LogMeshyRig, Log, TEXT("Rigging succeeded! Rigged GLB: %s"), *ResultUrls.RiggedGlbUrl);

		// Request all animations from the Animation API for consistent bone scaling
		SubmitAnimations();
	}
	else if (Status == TEXT("FAILED"))
	{
		StopPolling();
		FString ErrorMsg = TEXT("Rigging failed on server");
		SetState(EMeshyRigState::Error, ErrorMsg);
		OnRiggingComplete.Broadcast(FRiggedCharacterUrls(), ErrorMsg);
	}
	else
	{
		SetState(EMeshyRigState::RiggingPolling, FString::Printf(TEXT("Rigging... %d%%"), Progress));
	}
}

// ============================================================
// Animations
// ============================================================

void UMeshyRigClient::SubmitAnimations()
{
	SetState(EMeshyRigState::AnimationsSubmitting, TEXT("Requesting animations..."));

	// All animations from Animation API for consistent bone scaling:
	// idle (0), walk (30=Casual_Walk), run (14=Run_02), jump (466=Regular_Jump)
	// sprint (16=RunFast), boxing (87=Boxing_Practice), kick (94=Flying_Fist_Kick), punch (96=Kung_Fu_Punch)
	TArray<int32> ActionIds = { 0, 30, 14, 466, 16, 87, 94, 96 };
	for (int32 ActionId : ActionIds)
	{
		FAnimTask Task;
		Task.ActionId = ActionId;
		Task.bCompleted = false;
		AnimTasks.Add(Task);
	}

	for (const FAnimTask& Task : AnimTasks)
	{
		SubmitSingleAnimation(Task.ActionId);
	}
}

void UMeshyRigClient::SubmitSingleAnimation(int32 ActionId)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(BaseUrl + TEXT("/animations"));
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *GetApiKey()));

	TSharedPtr<FJsonObject> JsonBody = MakeShareable(new FJsonObject());
	JsonBody->SetStringField(TEXT("rig_task_id"), RigTaskId);
	JsonBody->SetNumberField(TEXT("action_id"), ActionId);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonBody.ToSharedRef(), Writer);

	UE_LOG(LogMeshyRig, Log, TEXT("Submitting animation action_id=%d"), ActionId);

	HttpRequest->SetContentAsString(RequestBody);
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[this, ActionId](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
		{
			OnAnimationSubmitResponse(Req, Resp, bSuccess, ActionId);
		});
	HttpRequest->ProcessRequest();
}

void UMeshyRigClient::OnAnimationSubmitResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, int32 ActionId)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		UE_LOG(LogMeshyRig, Error, TEXT("Animation submit failed for action_id=%d"), ActionId);
		return;
	}

	FString ResponseBody = Response->GetContentAsString();
	UE_LOG(LogMeshyRig, Log, TEXT("Animation submit [action_id=%d]: %s"), ActionId, *ResponseBody);

	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		UE_LOG(LogMeshyRig, Error, TEXT("Failed to parse animation submit response for action_id=%d"), ActionId);
		return;
	}

	FString TaskId = JsonResponse->GetStringField(TEXT("result"));

	// Store the task ID
	for (FAnimTask& Task : AnimTasks)
	{
		if (Task.ActionId == ActionId)
		{
			Task.TaskId = TaskId;
			break;
		}
	}

	UE_LOG(LogMeshyRig, Log, TEXT("Animation task for action_id=%d: %s"), ActionId, *TaskId);

	// Check if all tasks have been submitted, then start polling
	bool bAllSubmitted = true;
	for (const FAnimTask& Task : AnimTasks)
	{
		if (Task.TaskId.IsEmpty())
		{
			bAllSubmitted = false;
			break;
		}
	}

	if (bAllSubmitted)
	{
		StartAnimationPoll();
	}
}

void UMeshyRigClient::StartAnimationPoll()
{
	SetState(EMeshyRigState::AnimationsPolling, TEXT("Generating animations..."));

	UWorld* World = FindWorld();
	if (World)
	{
		World->GetTimerManager().SetTimer(PollTimerHandle, FTimerDelegate::CreateUObject(this, &UMeshyRigClient::PollAnimations), 3.0f, true);
	}
}

void UMeshyRigClient::PollAnimations()
{
	for (FAnimTask& Task : AnimTasks)
	{
		if (Task.bCompleted || Task.TaskId.IsEmpty()) continue;

		FString PollUrl = FString::Printf(TEXT("%s/animations/%s"), *BaseUrl, *Task.TaskId);

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetURL(PollUrl);
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *GetApiKey()));

		int32 ActionId = Task.ActionId;
		HttpRequest->OnProcessRequestComplete().BindLambda(
			[this, ActionId](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
			{
				OnAnimationPollResponse(Req, Resp, bSuccess, ActionId);
			});
		HttpRequest->ProcessRequest();
	}
}

void UMeshyRigClient::OnAnimationPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, int32 ActionId)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		UE_LOG(LogMeshyRig, Warning, TEXT("Animation poll failed for action_id=%d, will retry..."), ActionId);
		return;
	}

	FString ResponseBody = Response->GetContentAsString();
	UE_LOG(LogMeshyRig, Log, TEXT("Animation poll [action_id=%d]: %s"), ActionId, *ResponseBody);

	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		return;
	}

	FString Status = JsonResponse->GetStringField(TEXT("status"));

	if (Status == TEXT("SUCCEEDED"))
	{
		const TSharedPtr<FJsonObject>* ResultObj = nullptr;
		FString GlbUrl;
		if (JsonResponse->TryGetObjectField(TEXT("result"), ResultObj))
		{
			GlbUrl = (*ResultObj)->GetStringField(TEXT("animation_glb_url"));
		}

		for (FAnimTask& Task : AnimTasks)
		{
			if (Task.ActionId == ActionId)
			{
				Task.bCompleted = true;
				Task.ResultGlbUrl = GlbUrl;
				break;
			}
		}

		UE_LOG(LogMeshyRig, Log, TEXT("Animation action_id=%d completed: %s"), ActionId, *GlbUrl);

		// Map to result URLs
		if (ActionId == 0) ResultUrls.IdleAnimGlbUrl = GlbUrl;
		else if (ActionId == 30) ResultUrls.WalkAnimGlbUrl = GlbUrl;
		else if (ActionId == 14) ResultUrls.RunAnimGlbUrl = GlbUrl;
		else if (ActionId == 466) ResultUrls.JumpAnimGlbUrl = GlbUrl;
		else if (ActionId == 16) ResultUrls.SprintAnimGlbUrl = GlbUrl;
		else if (ActionId == 87) ResultUrls.BoxingAnimGlbUrl = GlbUrl;
		else if (ActionId == 94) ResultUrls.KickAnimGlbUrl = GlbUrl;
		else if (ActionId == 96) ResultUrls.PunchAnimGlbUrl = GlbUrl;

		if (AllAnimationsComplete())
		{
			StopPolling();
			SetState(EMeshyRigState::Completed, TEXT("Rigging and animations complete!"));
			UE_LOG(LogMeshyRig, Log, TEXT("All animations complete!"));
			OnRiggingComplete.Broadcast(ResultUrls, TEXT(""));
		}
		else
		{
			// Count completed
			int32 Done = 0;
			for (const FAnimTask& Task : AnimTasks)
			{
				if (Task.bCompleted) Done++;
			}
			SetState(EMeshyRigState::AnimationsPolling, FString::Printf(TEXT("Animations: %d/%d complete"), Done, AnimTasks.Num()));
		}
	}
	else if (Status == TEXT("FAILED"))
	{
		UE_LOG(LogMeshyRig, Error, TEXT("Animation action_id=%d failed"), ActionId);
		// Mark as completed with empty URL to not block others
		for (FAnimTask& Task : AnimTasks)
		{
			if (Task.ActionId == ActionId)
			{
				Task.bCompleted = true;
				break;
			}
		}

		if (AllAnimationsComplete())
		{
			StopPolling();
			SetState(EMeshyRigState::Completed, TEXT("Rigging complete (some animations failed)"));
			OnRiggingComplete.Broadcast(ResultUrls, TEXT(""));
		}
	}
}

bool UMeshyRigClient::AllAnimationsComplete() const
{
	for (const FAnimTask& Task : AnimTasks)
	{
		if (!Task.bCompleted) return false;
	}
	return true;
}

void UMeshyRigClient::StopPolling()
{
	UWorld* World = FindWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}
}
