#include "FalRigClient.h"
#include "FalQueueRequest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogFalRig, Log, All);

const FString UFalRigClient::MultiAnimationUrl = TEXT("https://queue.fal.run/fal-ai/meshy/rigging/multi-animation");
const float UFalRigClient::CharacterHeightMeters = 1.7f;

namespace
{
	// Meshy animation-library preset -> destination field on FRiggedCharacterUrls.
	struct FAnimationSlot
	{
		int32 ActionId;
		const TCHAR* Name;
		FString FRiggedCharacterUrls::* UrlField;
	};
}

// Action IDs from https://docs.meshy.ai/en/api/animation-library.
// The endpoint accepts at most 10 distinct clips per request.
static const TArray<FAnimationSlot>& GetAnimationSlots()
{
	static const TArray<FAnimationSlot> Slots = {
		{ 0,   TEXT("Idle"),             &FRiggedCharacterUrls::IdleAnimGlbUrl },
		{ 30,  TEXT("Casual Walk"),      &FRiggedCharacterUrls::WalkAnimGlbUrl },
		{ 14,  TEXT("Run"),              &FRiggedCharacterUrls::RunAnimGlbUrl },
		{ 16,  TEXT("Fast Run"),         &FRiggedCharacterUrls::SprintAnimGlbUrl },
		{ 466, TEXT("Regular Jump"),     &FRiggedCharacterUrls::JumpAnimGlbUrl },
		{ 87,  TEXT("Boxing Practice"),  &FRiggedCharacterUrls::BoxingAnimGlbUrl },
		{ 94,  TEXT("Flying Fist Kick"), &FRiggedCharacterUrls::KickAnimGlbUrl },
		{ 96,  TEXT("Kung Fu Punch"),    &FRiggedCharacterUrls::PunchAnimGlbUrl },
	};
	return Slots;
}

//////////////////////////////////////////////////////////////////////////
// State

void UFalRigClient::SetState(EFalRigState NewState, const FString& Message)
{
	if (NewState == CurrentState && Message == StatusMessage)
	{
		return;
	}
	CurrentState = NewState;
	StatusMessage = Message;
	OnStateChanged.Broadcast(NewState);
}

void UFalRigClient::Fail(const FString& Error)
{
	SetState(EFalRigState::Error, Error);
	OnRiggingComplete.Broadcast(FRiggedCharacterUrls(), Error);
}

bool UFalRigClient::IsRigging() const
{
	return Request && Request->IsActive();
}

//////////////////////////////////////////////////////////////////////////
// Request

void UFalRigClient::RigAndAnimate(const FString& GlbUrl)
{
	if (IsRigging())
	{
		UE_LOG(LogFalRig, Warning, TEXT("Rigging already in progress"));
		return;
	}

	if (UFalQueueRequest::GetApiKey().IsEmpty())
	{
		Fail(TEXT("FAL_KEY not found. Add it to .env in the project folder or set it as an environment variable."));
		return;
	}

	if (!Request)
	{
		Request = NewObject<UFalQueueRequest>(this);
	}

	TArray<TSharedPtr<FJsonValue>> ActionIds;
	for (const FAnimationSlot& Slot : GetAnimationSlots())
	{
		ActionIds.Add(MakeShared<FJsonValueNumber>(Slot.ActionId));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model_url"), GlbUrl);
	Body->SetNumberField(TEXT("height_meters"), CharacterHeightMeters);
	Body->SetArrayField(TEXT("animation_action_ids"), ActionIds);

	UE_LOG(LogFalRig, Log, TEXT("Rig + %d animations for %s"), ActionIds.Num(), *GlbUrl);
	SetState(EFalRigState::Submitting, FString::Printf(TEXT("Submitting rig + %d animations..."), ActionIds.Num()));

	Request->Start(MultiAnimationUrl, Body,
		FOnFalQueueComplete::CreateUObject(this, &UFalRigClient::HandleResult),
		FOnFalQueueProgress::CreateUObject(this, &UFalRigClient::HandleProgress));
}

//////////////////////////////////////////////////////////////////////////
// Callbacks

void UFalRigClient::HandleProgress(const FString& Message)
{
	SetState(EFalRigState::Polling, FString::Printf(TEXT("Rigging and animating: %s"), *Message));
}

void UFalRigClient::HandleResult(TSharedPtr<FJsonObject> Result, const FString& Error)
{
	if (!Error.IsEmpty())
	{
		Fail(FString::Printf(TEXT("Rigging failed: %s"), *Error));
		return;
	}

	FRiggedCharacterUrls Urls;

	const TSharedPtr<FJsonObject>* RiggedGlb = nullptr;
	if (Result->TryGetObjectField(TEXT("rigged_character_glb"), RiggedGlb))
	{
		Urls.RiggedGlbUrl = (*RiggedGlb)->GetStringField(TEXT("url"));
	}
	if (Urls.RiggedGlbUrl.IsEmpty())
	{
		Fail(TEXT("Rigging finished but the result contains no rigged_character_glb URL"));
		return;
	}
	UE_LOG(LogFalRig, Log, TEXT("Rigged GLB: %s"), *Urls.RiggedGlbUrl);

	// animations: [{ action_id, animation_glb: { url } }, ...]
	int32 ClipsFound = 0;
	const TArray<TSharedPtr<FJsonValue>>* Animations = nullptr;
	if (Result->TryGetArrayField(TEXT("animations"), Animations))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Animations)
		{
			const TSharedPtr<FJsonObject>* Clip = nullptr;
			if (!Value->TryGetObject(Clip))
			{
				continue;
			}

			int32 ActionId = -1;
			if (!(*Clip)->TryGetNumberField(TEXT("action_id"), ActionId))
			{
				continue;
			}

			FString ClipUrl;
			const TSharedPtr<FJsonObject>* ClipGlb = nullptr;
			if ((*Clip)->TryGetObjectField(TEXT("animation_glb"), ClipGlb))
			{
				ClipUrl = (*ClipGlb)->GetStringField(TEXT("url"));
			}

			const FAnimationSlot* Slot = GetAnimationSlots().FindByPredicate(
				[ActionId](const FAnimationSlot& S) { return S.ActionId == ActionId; });

			if (!Slot)
			{
				UE_LOG(LogFalRig, Warning, TEXT("Unexpected animation action_id=%d in result"), ActionId);
				continue;
			}
			if (ClipUrl.IsEmpty())
			{
				UE_LOG(LogFalRig, Warning, TEXT("Animation '%s' (action_id=%d) has no GLB URL"), Slot->Name, ActionId);
				continue;
			}

			Urls.*(Slot->UrlField) = ClipUrl;
			ClipsFound++;
			UE_LOG(LogFalRig, Log, TEXT("Animation '%s' (action_id=%d): %s"), Slot->Name, ActionId, *ClipUrl);
		}
	}

	const int32 ClipsRequested = GetAnimationSlots().Num();
	for (const FAnimationSlot& Slot : GetAnimationSlots())
	{
		if ((Urls.*(Slot.UrlField)).IsEmpty())
		{
			UE_LOG(LogFalRig, Warning, TEXT("Missing animation '%s' (action_id=%d)"), Slot.Name, Slot.ActionId);
		}
	}

	const FString Summary = ClipsFound == ClipsRequested
		? TEXT("Rigging and animations complete!")
		: FString::Printf(TEXT("Rigging complete (%d/%d animations)"), ClipsFound, ClipsRequested);

	SetState(EFalRigState::Completed, Summary);
	OnRiggingComplete.Broadcast(Urls, FString());
}
