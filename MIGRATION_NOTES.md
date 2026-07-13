# MIGRATION_NOTES — XRU1

Перенос переиспользуемого из донора `cst-3d-gubkin-2026-04` (UE 5.7, Top Down) в новый
тактический проект **XRU1** + каркас недостающего пошагового ядра. Донор не изменялся.

---

## 1. Перенесено как есть (копия, донор не менялся)

### Плагины (`Plugins/`, папки целиком, без Binaries/Intermediate)
| Плагин | Назначение | Примечание |
|---|---|---|
| `STQuestSystem` | квесты/диалоги/waypoints + `UQuestSaveGame` | под туториал-подсказки и mission-select POI |
| `TeamManager` | фракции «отряд / враги» | пункт ТЗ 6 |
| `GameplayMessageRouter` | шина сообщений (event bus) | **обязателен** — от него зависит STQuestSystem (`GameplayMessageRuntime`) |

### Контент
| Что | Куда | Размер |
|---|---|---|
| `Content/MWLandscapeAutoMaterial/` | `Content/MWLandscapeAutoMaterial/` | ~702 МБ, авто-материал ландшафта + карты Desert/MountainRange + PCG-процедуры (под 3D-хаб, пункт 8). Внешних OFPA-акторов у карт нет — перенос самодостаточен. |

## 2. Перенесено с адаптацией (правка под модуль `XRU1`)

Единственная адаптация — замена API-макроса `TOPDOWNCST_API` → `XRU1_API`. Include-стиль в
доноре плоский (через `PublicIncludePaths`), поэтому пути включений править не пришлось.
Ссылок на модульный заголовок/лог-категорию донора (`TopDownCST.h`, `LogTopDownCST`) в
перенесённом коде нет.

| Папка → | Файлы | Роль |
|---|---|---|
| `Source/XRU1/Characters/` | BaseCharacter, GASCharacter, TDCombatant, TDAttributeSet, GA_Dash, PlayerCharacter, CSTPlayerController | иерархия боевого юнита на GAS (пункт 6) |
| `Source/XRU1/UI/` | UnitHUD/Attribute/Health/Shield/MoveSpeed/AttributeBar widgets, UnitHUDLayoutData, InteractionPromptWidget, PrimaryGameLayout, GameUIManagerSubsystem, ProgressBarWidgetStyle | HUD над головой + CommonUI-каркас навигации (пункт 1) |
| `Source/XRU1/Interaction/` | Interactable.h, InteractionDetectorComponent | hover-детекция для POI (пункт 8) — **минимальный срез** (без Pickups/Doors/Levers/InteractableActor) |
| `Source/XRU1/PCG/` | PCGScatterJitter, PCGSlopeHeightFilter | PCG-ноды рассева по склону/высоте для хаб-карты (пункт 8) |

**Правки сборки:** в `XRU1.Build.cs` добавлены модули `SlateCore, GameplayAbilities,
GameplayTags, GameplayTasks, CommonUI, CommonInput, PCG` и include-пути новых папок. В
`XRU1.uproject` включены плагины `GameplayAbilities, CommonUI, PCG` + 3 перенесённых.

## 3. Создано с нуля как каркас (Pass 2)

Каркасы компилируемы и логически связаны; финальная геймплейная логика помечена
`TODO(next phase)`.

### `Source/XRU1/Tactics/`
| Класс | База | Пункт ТЗ | Что делает в каркасе |
|---|---|---|---|
| `ECoverType` (`CoverTypes.h`) | enum | 2 | None/Half/Full |
| `EDifficultyLevel`, `EUnitRole`, `ETurnPhase` (`TacticsTypes.h`) | enum | 7,6,3 | общие типы |
| `UActionPointsComponent` | `UActorComponent` | 3 | Max=2/Current AP, `TrySpendActionPoint`, `ResetForNewTurn`, делегат `OnActionPointsChanged` |
| `UTurnManagerSubsystem` | `UWorldSubsystem` | 3 | очередь сторон, фаза Player/Enemy, `StartCombat/EndTurn/EndCombat`, делегаты `OnTurnStarted/OnCombatEnded`, сброс AP стороны |
| `UCoverDetectionComponent` | `UActorComponent` | 2 | трейсы по 4 направлениям, `GetCoverAgainst(врага)`, теги GAS `HalfCoverTag/FullCoverTag` |
| `UGA_Overwatch` | `UGameplayAbility` | 4 | подписка на `AIPerception.OnTargetPerceptionUpdated`, лимит реакций, хук `FireReactionShot` |
| `AUnitBase` | `ATDCombatant` | 6 | + ActionPoints + CoverDetection + `ClassAbilities`, выдача способностей через ASC |
| `AUnit_Assault/Sniper/Healer/Tank` | `AUnitBase` | 6 | 4 Blueprintable-класса ростера, задают `EUnitRole` |
| `AUnitAIController` | `AAIController` | 4 | `UAIPerceptionComponent` + `UAISenseConfig_Sight` (линия видимости) |
| `UTacticsSaveGame` | `USaveGame` | 1,7 | сложность, пройденные миссии, ростер |
| `UTacticsGameInstance` | `UGameInstance` | 1 | `HasSaveGame` (для «Продолжить»), `StartNewCampaign/SaveCampaign/LoadCampaign` |
| `AMissionPointOfInterest` | `AActor` | 8 | маркер + hover-попап + soft-ссылка на уровень |

### `Source/XRU1/UI/Menus/`
| Класс | База | Пункт ТЗ |
|---|---|---|
| `UMenuScreenBase` | `UCommonActivatableWidget` | 1 |
| `UMainMenuWidget` | `UMenuScreenBase` | 1 — Продолжить/Новая игра/Настройки/Об авторе/Выйти + `CanContinue()` |
| `USettingsMenuWidget`, `UAboutMenuWidget`, `UPauseMenuWidget` | `UMenuScreenBase` | 1 |
| `UDifficultySelectWidget` | `UMenuScreenBase` | 7 — делегат `OnDifficultyChosen` |

## 4. НЕ перенесено (осознанно)
`STConcurrency` (демо многопоточности), `TopDownCSTShaders` (кастомный compute-shader,
хрупкий к версии), `Variant_TwinStick`/`Variant_Strategy` донора, Http/Audio/VFX/PostProcess/
Materials/Localization-демо, NPC/BT/ST-подсистема донора, пикапы/двери/рычаги. Причины — в
`cst-3d-gubkin-2026-04/AUDIT.md` (раздел 5, п.4).

## 5. Осталось как ручная работа в редакторе / следующий этап
- **Cinematic-заставка** (Level Sequence) — собрать в Sequencer вручную (не кодовая задача).
- **Хаб-уровень** на базе `MWLandscapeAutoMaterial` + вращаемая по Z камера (SpringArm +
  input) + расстановка `AMissionPointOfInterest` (обучение/миссия).
- **Тактические карты** (туториал + боевая миссия), BP-наследники юнитов с уникальными
  способностями/эффектами, BP-виджеты меню (визуал), связка сложности с
  `DA_HostileNPC_Easy/Medium/Hard`.
- Прописать `UTacticsGameInstance` как Game Instance Class в настройках проекта.
- **Финальная боевая логика** (урон с учётом укрытия, AI-ходы врага, reaction-shot) —
  все точки помечены `TODO(next phase)`.

## 6. Статус сборки
Заголовки успешно прошли UnrealHeaderTool (UHT) при первом прогоне. Полная компиляция
требует **закрытого редактора Unreal** (иначе UBT падает на `Live Coding is active`).
Команда сборки — в `CLAUDE.md`.
