#include "PlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "Blueprint/UserWidget.h"

#include "InteractionDetectorComponent.h"
#include "InteractionPromptWidget.h"

APlayerCharacter::APlayerCharacter()
{
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw   = false;
    bUseControllerRotationRoll  = false;

    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->SetUsingAbsoluteRotation(true);
    CameraBoom->TargetArmLength    = 800.f;
    CameraBoom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
    CameraBoom->bDoCollisionTest   = false;

    TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
    TopDownCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    TopDownCamera->bUsePawnControlRotation = false;

    InteractionDetector =
        CreateDefaultSubobject<UInteractionDetectorComponent>(TEXT("InteractionDetector"));
    InteractionDetector->SetupAttachment(RootComponent);
}

void APlayerCharacter::PawnClientRestart()
{
    Super::PawnClientRestart();

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;

    if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
    {
        Subsystem->ClearAllMappings();
        if (DefaultMappingContext)
        {
            Subsystem->AddMappingContext(DefaultMappingContext, 0);
        }
    }

    if (PromptWidgetClass && !PromptWidget)
    {
        PromptWidget = CreateWidget<UInteractionPromptWidget>(PC, PromptWidgetClass);
        if (PromptWidget)
        {
            PromptWidget->AddToViewport();
            PromptWidget->BindToDetector(InteractionDetector);
        }
    }
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* InputComp)
{
    Super::SetupPlayerInputComponent(InputComp);

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComp))
    {
        if (MoveAction)
            EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::OnMove);
        if (InteractAction)
            EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &APlayerCharacter::OnInteract);
    }
}

void APlayerCharacter::OnMove(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();
    AddMovementInput(FVector::ForwardVector, Axis.Y);
    AddMovementInput(FVector::RightVector,   Axis.X);
}

void APlayerCharacter::OnInteract(const FInputActionValue& /*Value*/)
{
    if (InteractionDetector)
    {
        InteractionDetector->TryInteract();
    }
}
