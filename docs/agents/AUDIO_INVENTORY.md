# Опись звуков XRU1 — что где играет

Снято 2026-08-03 из проекта. `Тишина` — сколько миллисекунд в начале
файла ниже -34 dB; `До пика` — когда звук выходит на максимум. Для
интерфейса ориентир: **до пика < 30 мс**, иначе отклик ощущается вялым.


## Интерфейс и мир (DA_TacticsAudio)

| Слот | Ассет | Тишина, мс | До пика, мс | Длит., с |
|---|---|---|---|---|
| `UIHover` | `S_UI_Hover` | 54 | 63 | 0.70 |
| `UIClick` | `S_UI_Click` | 21 | 54 | 0.69 |
| `UIDenied` | `S_UI_Denied` | 0 | 114 | 0.32 |
| `UnitSelected` | `S_UI_Select` | 21 | 27 | 0.70 |
| `TurnStartedPlayer` | `S_UI_TurnPlayer` | 69 | 170 | 0.69 |
| `TurnStartedEnemy` | `S_UI_TurnPlayer` | 69 | 170 | 0.69 |
| `BombTick` | `S_Bomb_TickAlt_01` | 30 | 56 | 0.12 |
| `BombDefuseStep` | `S_Defuse_Mech_01` | 18 | 74 | 0.23 |
| `BombDisarmed` | `S_Alarm_Whole` | 12 | 5020 | 8.00 |
| `EvacZoneActivated` | `S_Alarm_Quarter` | 13 | 3239 | 5.00 |
| `EvacUnit` | `— ПУСТО —` |  |  |  |
| `MenuMusic` | `S_Music_Menu` | 62 | 13288 | 218.49 |
| `HubMusic` | `S_Music_Hub` | 0 | 76374 | 190.61 |
| `CombatMusic` | `S_Music_Combat` | 0 | 47856 | 268.21 |
| `VictoryStinger` | `S_Music_Victory` | 0 | 7668 | 20.01 |
| `DefeatStinger` | `S_Music_Defeat` | 10 | 1408 | 20.01 |
| `HubArrivalVoice` | `VO_Hub_Arrival_Kupol` | 75 | 475 | 16.00 |

## Сценарии (DA_Scenario_Tutorial)

| Слот | Ассет | Тишина, мс | До пика, мс | Длит., с |
|---|---|---|---|---|
| `BriefingVoice` | `VO_Tut_Brief_Kupol` | 44 | 4318 | 18.90 |
| `VictoryVoice` | `VO_Tut_Result_Kupol` | 37 | 1613 | 4.45 |
| `DefeatVoice` | `— ПУСТО —` |  |  |  |
| `DefeatByTimeoutVoice` | `— ПУСТО —` |  |  |  |

## Сценарии (DA_Scenario_Mission01)

| Слот | Ассет | Тишина, мс | До пика, мс | Длит., с |
|---|---|---|---|---|
| `BriefingVoice` | `VO_M01_Brief_Kupol` | 55 | 6171 | 27.49 |
| `VictoryVoice` | `VO_M01_Epilogue_Kupol` | 32 | 4925 | 5.34 |
| `DefeatVoice` | `VO_M01_DefeatWipe_Kupol` | 44 | 211 | 3.08 |
| `DefeatByTimeoutVoice` | `VO_M01_DefeatTimer_Kupol` | 35 | 721 | 3.68 |

## Юниты (DA_UnitAudio_Common)

| Слот | Ассет | Тишина, мс | До пика, мс | Длит., с |
|---|---|---|---|---|
| `HIT` | `S_Impact_FleshWet_01` | 1 | 48 | 0.30 |
| `DEATH` | `S_Pain_Female_02` | 33 | 52 | 0.33 |
| `DOWNED` | `S_Pain_Female_02` | 33 | 52 | 0.33 |
| `REVIVE` | `S_Effort_Male_02` | 27 | 113 | 0.58 |
| `RUN_AND_GUN` | `S_Effort_Male_02` | 27 | 113 | 0.58 |
| `TAUNT` | `S_Effort_Male_02` | 27 | 113 | 0.58 |
| `HEAL` | `S_Gear_Carabiner_02` | 40 | 49 | 0.23 |
| `HEAL` | `S_Gear_Carabiner_03` | 29 | 39 | 0.22 |
| `OVERWATCH_ENTER` | `S_Gear_Carabiner_02` | 40 | 49 | 0.23 |
| `OVERWATCH_ENTER` | `S_Gear_Carabiner_03` | 29 | 39 | 0.22 |
| `HUNKER_DOWN` | `S_Gear_Carabiner_02` | 40 | 49 | 0.23 |
| `HUNKER_DOWN` | `S_Gear_Carabiner_03` | 29 | 39 | 0.22 |
| `EVACUATED` | `S_Gear_Carabiner_02` | 40 | 49 | 0.23 |
| `EVACUATED` | `S_Gear_Carabiner_03` | 29 | 39 | 0.22 |
| `шаги Default` | `S_Footstep_01` | 1 | 7 | 0.25 |

## Юниты (DA_UnitAudio_Assault)

| Слот | Ассет | Тишина, мс | До пика, мс | Длит., с |
|---|---|---|---|---|
| `FIRE` | `S_Shot_Rifle` | 12 | 23 | 1.23 |
| `REACTION_FIRE` | `S_Shot_Rifle` | 12 | 23 | 1.23 |
| `шаги Default` | `— ПУСТО —` |  |  |  |

## Юниты (DA_UnitAudio_Marauder)

| Слот | Ассет | Тишина, мс | До пика, мс | Длит., с |
|---|---|---|---|---|
| `FIRE` | `S_Shot_Rifle` | 12 | 23 | 1.23 |
| `REACTION_FIRE` | `S_Shot_Rifle` | 12 | 23 | 1.23 |
| `шаги Default` | `— ПУСТО —` |  |  |  |

## Юниты (DA_UnitAudio_Medic)

| Слот | Ассет | Тишина, мс | До пика, мс | Длит., с |
|---|---|---|---|---|
| `FIRE` | `S_Shot_Rifle` | 12 | 23 | 1.23 |
| `REACTION_FIRE` | `S_Shot_Rifle` | 12 | 23 | 1.23 |
| `шаги Default` | `— ПУСТО —` |  |  |  |

## Юниты (DA_UnitAudio_Sniper)

| Слот | Ассет | Тишина, мс | До пика, мс | Длит., с |
|---|---|---|---|---|
| `FIRE` | `S_Shot_Sniper` | 0 | 3 | 3.29 |
| `REACTION_FIRE` | `S_Shot_Sniper` | 0 | 3 | 3.29 |
| `шаги Default` | `— ПУСТО —` |  |  |  |

## Юниты (DA_UnitAudio_Tank)

| Слот | Ассет | Тишина, мс | До пика, мс | Длит., с |
|---|---|---|---|---|
| `FIRE` | `S_Shot_Rifle` | 12 | 23 | 1.23 |
| `REACTION_FIRE` | `S_Shot_Rifle` | 12 | 23 | 1.23 |
| `шаги Default` | `— ПУСТО —` |  |  |  |

## Не найдены в просканированных слотах

⚠️ Это НЕ значит «не используются». Просканированы только `DA_TacticsAudio`,
оба `DA_Scenario_*` и профили `DA_UnitAudio_*`. Реплики туториала и миссии
назначаются в шагах квеста (`FTacticalTutorialBeat.Voice` в `ST_Quest_*`), а
`VO_Hub_Locked_Kupol` — в хуке отказа POI; проверять их надо там.


- `S_Explosion_02` — 6.00 с (тишина 264 мс)
- `S_Footstep_02` — 0.25 с (тишина 1 мс)
- `S_Footstep_03` — 0.25 с (тишина 0 мс)
- `S_Footstep_04` — 0.25 с (тишина 0 мс)
- `S_Footstep_05` — 0.25 с (тишина 0 мс)
- `S_Footstep_06` — 0.25 с (тишина 0 мс)
- `S_Footstep_07` — 0.25 с (тишина 1 мс)
- `S_Impact_Concrete_01` — 0.36 с (тишина 30 мс)
- `S_Impact_Metal_01` — 0.32 с (тишина 32 мс)
- `VO_Hub_Locked_Kupol` — 6.36 с (тишина 44 мс)
- `VO_M01_Contact_Molot` — 1.54 с (тишина 11 мс)
- `VO_M01_Defuse1_Shpritz` — 5.26 с (тишина 16 мс)
- `VO_M01_Defused_Kupol` — 6.44 с (тишина 42 мс)
- `VO_M01_Epilogue_Narr` — 8.80 с (тишина 35 мс)
- `VO_M01_FirstEvac_Kupol` — 3.34 с (тишина 26 мс)
- `VO_M01_FirstKill_Klin` — 1.89 с (тишина 33 мс)
- `VO_M01_HalfTimer_Kupol` — 4.35 с (тишина 20 мс)
- `VO_M01_Start_Osa` — 3.47 с (тишина 50 мс)
- `VO_M01_ThreeTurns_Kupol` — 3.08 с (тишина 18 мс)
- `VO_M01_Wounded_Shpritz` — 2.53 с (тишина 12 мс)
- `VO_Tut_A1_Kupol` — 14.12 с (тишина 42 мс)
- `VO_Tut_A2_Kupol` — 13.61 с (тишина 1 мс)
- `VO_Tut_A3_Kupol` — 7.50 с (тишина 42 мс)
- `VO_Tut_A4_Kupol` — 10.37 с (тишина 44 мс)
- `VO_Tut_A4_Shpritz` — 1.50 с (тишина 22 мс)
- `VO_Tut_A5_Kupol` — 9.08 с (тишина 30 мс)
- `VO_Tut_A6_Kupol` — 7.87 с (тишина 28 мс)
- `VO_Tut_A7_Kupol` — 4.48 с (тишина 54 мс)
- `VO_Tut_A8_Kupol` — 11.41 с (тишина 14 мс)
- `VO_Tut_A9_Klin` — 1.75 с (тишина 35 мс)
- `VO_Tut_A9_Kupol` — 9.25 с (тишина 21 мс)
- `VO_Tut_A_Close_Shpritz` — 2.52 с (тишина 6 мс)
- `VO_Tut_B0_Kupol` — 6.56 с (тишина 65 мс)
- `VO_Tut_B1_Kupol` — 5.25 с (тишина 46 мс)
- `VO_Tut_B2_Kadet` — 3.61 с (тишина 32 мс)
- `VO_Tut_B2_Kupol` — 3.44 с (тишина 21 мс)
- `VO_Tut_B3_Kupol` — 15.24 с (тишина 39 мс)
- `VO_Tut_B4_Kupol` — 3.30 с (тишина 41 мс)
- `VO_Tut_B4_Molot` — 0.74 с (тишина 24 мс)
- `VO_Tut_B5_Kupol` — 15.12 с (тишина 45 мс)
- `VO_Tut_B5_Molot` — 0.51 с (тишина 18 мс)
- `VO_Tut_B5_Osa` — 2.77 с (тишина 48 мс)
- `VO_Tut_C0_Kupol` — 16.71 с (тишина 36 мс)
- `VO_Tut_C0_Osa` — 4.11 с (тишина 67 мс)
- `VO_Tut_C1_Kupol` — 4.67 с (тишина 28 мс)
- `VO_Tut_C1_Molot` — 0.92 с (тишина 17 мс)
- `VO_Tut_C2_Kupol` — 9.44 с (тишина 45 мс)
- `VO_Tut_C3_Kadet` — 3.15 с (тишина 36 мс)
- `VO_Tut_C3_Kupol` — 9.28 с (тишина 41 мс)
- `VO_Tut_C4_Kupol` — 7.74 с (тишина 63 мс)
- `VO_Tut_D1_Kupol` — 9.54 с (тишина 77 мс)
- `radio-noise-interference` — 78.22 с (тишина 199 мс)
