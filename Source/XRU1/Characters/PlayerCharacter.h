#pragma once

#include "CoreMinimal.h"
#include "TDCombatant.h"
#include "PlayerCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UInteractionDetectorComponent;
class UInteractionPromptWidget;

UCLASS(Abstract)
class XRU1_API APlayerCharacter : public ATDCombatant
{
	GENERATED_BODY()

public:
	APlayerCharacter();

	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> TopDownCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UInteractionDetectorComponent> InteractionDetector;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UInteractionPromptWidget> PromptWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UInteractionPromptWidget> PromptWidget;

	void OnMove(const FInputActionValue& Value);
	void OnInteract(const FInputActionValue& Value);
};
