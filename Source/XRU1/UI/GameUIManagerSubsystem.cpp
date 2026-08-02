// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameUIManagerSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "PrimaryGameLayout.h"
#include "XRU1Log.h"

void UGameUIManagerSubsystem::CreateLayout(APlayerController* OwningPlayer, TSubclassOf<UPrimaryGameLayout> LayoutClass)
{
    // Логируется КАЖДЫЙ выход. Молчащие ранние return'ы уже стоили одного
    // расследования: HUD хаба не появлялся, а в логе не было ни строки, и
    // отличить «не дошли до создания» от «создали, но не видно» было нечем.
    if (!OwningPlayer)
    {
        UE_LOG(LogXRU1UI, Error, TEXT("[UI] CreateLayout: нет OwningPlayer — лейаут не создан"));
        return;
    }
    if (!LayoutClass)
    {
        UE_LOG(LogXRU1UI, Error,
            TEXT("[UI] CreateLayout: у контроллера '%s' не назначен класс корневого лейаута ")
            TEXT("(RootLayoutClass в Class Defaults) — лейаут не создан"),
            *GetNameSafe(OwningPlayer));
        return;
    }

    // ⚠️ Сравнивать владельца НЕДОСТАТОЧНО, и это стоило целого расследования
    // «в хабе нет HUD». `UUserWidget::GetOwningPlayer()` спрашивает не
    // сохранённый указатель, а ULocalPlayer -> PlayerController; локальный
    // игрок переживает смену уровня, и его PlayerController уже НОВЫЙ. То есть
    // лейаут из прошлого мира отвечал «да, я принадлежу этому контроллеру»,
    // подсистема молча выходила — а виджет к тому моменту был снят с экрана
    // (`UWorld::CleanupWorld` чистит виджеты viewport), и экран после travel
    // оставался без интерфейса. Признак «тот же лейаут» — только явно
    // запомненный мир (у виджета не спросишь: его Outer — GameInstance).
    UWorld* PlayerWorld = OwningPlayer->GetWorld();
    const bool bSameWorld = RootLayout && LayoutWorld.IsValid() && LayoutWorld.Get() == PlayerWorld;

    if (RootLayout && bSameWorld && RootLayout->GetOwningPlayer() == OwningPlayer)
    {
        if (RootLayout->IsInViewport())
        {
            UE_LOG(LogXRU1UI, Verbose,
                TEXT("[UI] CreateLayout: лейаут '%s' уже на экране в этом же мире — повтор не нужен"),
                *GetNameSafe(RootLayout));
            return;
        }
        UE_LOG(LogXRU1UI, Warning,
            TEXT("[UI] CreateLayout: лейаут '%s' того же мира, но снят с экрана — возвращаю в viewport"),
            *GetNameSafe(RootLayout));
        RootLayout->AddToViewport();
        return;
    }

    // СТАРЫЙ лейаут обязан уйти с экрана и из этой ссылки. Пока он оставался в
    // viewport, после туториала экран результата (слой Menu) висел поверх хаба
    // и перехватывал клики; а пока он оставался в UPROPERTY — держал живым весь
    // прошлый мир, потому что World у виджета Outer.
    if (RootLayout)
    {
        UE_LOG(LogXRU1UI, Display,
            TEXT("[UI] лейаут пересоздаётся (мир сменился: %s) — снимаю прежний '%s'"),
            bSameWorld ? TEXT("нет") : TEXT("да"), *GetNameSafe(RootLayout));
        RootLayout->RemoveFromParent();
        RootLayout = nullptr;
        LayoutWorld = nullptr;
    }

    RootLayout = CreateWidget<UPrimaryGameLayout>(OwningPlayer, LayoutClass);
    if (!RootLayout)
    {
        UE_LOG(LogXRU1UI, Error, TEXT("[UI] не удалось создать корневой лейаут класса '%s'"),
            *GetNameSafe(LayoutClass));
        return;
    }

    LayoutWorld = PlayerWorld;
    RootLayout->AddToViewport();
    UE_LOG(LogXRU1UI, Display,
        TEXT("[UI] корневой лейаут '%s' (класс '%s') создан для '%s' в мире '%s', в viewport: %d"),
        *GetNameSafe(RootLayout), *GetNameSafe(LayoutClass), *GetNameSafe(OwningPlayer),
        *GetNameSafe(PlayerWorld), RootLayout->IsInViewport() ? 1 : 0);
}
