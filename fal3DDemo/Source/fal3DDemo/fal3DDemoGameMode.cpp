// Copyright Epic Games, Inc. All Rights Reserved.

#include "fal3DDemoGameMode.h"
#include "fal3DDemoCharacter.h"
#include "UObject/ConstructorHelpers.h"

Afal3DDemoGameMode::Afal3DDemoGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
