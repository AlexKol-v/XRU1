#include "BaseCharacter.h"

ABaseCharacter::ABaseCharacter()
{
}

void ABaseCharacter::SetGenericTeamId(const FGenericTeamId& NewTeamID) { TeamId = NewTeamID; }
FGenericTeamId       ABaseCharacter::GetGenericTeamId() const          { return TeamId; }

void ABaseCharacter::BeginPlay()
{
    Super::BeginPlay();
    TeamId = FGenericTeamId(DefaultTeamId);
}
