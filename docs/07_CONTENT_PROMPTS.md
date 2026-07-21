# Промты генерации контента — «Тихий узел»

Единый файл для генерации ВСЕГО арт/аудио-контента ИИ-инструментами в общем
стиле. Каждая секция: промт (копируй как есть, подставь детали) + куда класть
результат. Лор и имена — из [02_LORE_SCRIPT.md](02_LORE_SCRIPT.md).

## 1. Общий стайл-гайд (вставлять в начало КАЖДОГО арт-промта)

> **Style block (EN, для генераторов):**
> "Near-future post-collapse military setting, Eastern European autumn palette:
> desaturated olive drab, rust orange, concrete gray, muted teal accents.
> Grounded low-sci-fi tech (no aliens, no lasers): patched kevlar, worn steel,
> analog radios, holographic tactical displays as the only 'clean' tech element
> (cyan-teal glow). Mood: quiet tension, overcast light, light fog. Style:
> semi-realistic digital painting, XCOM 2 / Into the Breach promo-art vibe,
> clean silhouettes, readable shapes, no text unless asked."

Русские тексты на картинках НЕ генерировать (шрифты лягут криво) — текст
добавляем в UMG поверх. Разрешения: фоны/брифинги 1920×1080, портреты
512×512, иконки 256×256 (PNG с альфой).

Куда класть: `Content/XRU1Game/UI/Art/<Категория>/` (импорт PNG в редактор),
имена по конвенции `T_<Что>_<Вариант>` (06_CONVENTIONS.md).

## 2. Стартовый синематик (видео-интро с лором)

### 2.1 Выбор сервиса (исследование июль 2026)

Рекомендация под наши задачи (image-to-video, милитари-синематик ~45 сек,
бюджет = 1 подписка на месяц):

| Сервис | Почему | Цена | Вердикт |
|---|---|---|---|
| **Kling 3.0 (Standard)** | лучший фотореал людей/движения, **мультишот-сториборд с общим звуком**, самый дешёвый за клип (~$0.10/сек), кадры можно генерить там же | ~$10/мес | ✅ **основной выбор** |
| **Google Veo 3.1** (в Google AI Pro) | сильнейшее следование промту + **нативный синхронный звук**, кадры Imagen в той же подписке | ~$20/мес | ✅ альтернатива, если удобна экосистема Google |
| Runway Gen-4.5 | про-контроль для монтажёров | дороже | избыточен для 45-сек интро |
| Sora | — | — | ❌ закрыт (веб/апп — апрель 2026, API — сентябрь 2026) |

**Пайплайн:** (1) сгенерировать 6 ключевых кадров (стиль-блок §1) →
(2) каждый кадр оживить image-to-video 5–8 сек (промты ниже) →
(3) склейка + закадровый голос (§6) + музыка (§7) в **DaVinci Resolve
(бесплатный)** → (4) экспорт MP4 1080p → `Content/Movies/Intro.mp4`
(проигрывание — 05_EDITOR_GUIDE §3.5). Опционально тем же способом:
6-сек victory-шот для эпилога и 10-сек луп для фона главного меню.

### 2.2 Сценарий интро — 6 шотов (~45 сек)

Каждый шот: **[кадр]** — промт изображения (прибавить style block §1);
**[видео]** — промт i2v (движение камеры + событие); **[закадр]** — реплика
(озвучка «Купол», §6). Тексты закадра = канон, менять только через 02_LORE_SCRIPT.

**Шот 1 — «Тихий удар» (0:00–0:07)**
- [кадр] "Orbital wide shot: a constellation of communication satellites above
  night-side Earth, one satellite sparking and tumbling, city light grids on
  the planet below beginning to flicker out in spreading waves"
- [видео] "Slow dolly-in toward Earth; satellites drift dead and dark, city
  grids below extinguish region by region like blown fuses; cold silence, faint
  radio static crackle fading to nothing"
- [закадр] «Однажды ночью спутники замолчали. Мы назвали это “Тихим ударом”.»

**Шот 2 — оглохшая земля (0:07–0:14)**
- [кадр] "Ground level: an abandoned provincial radio tower at grey dawn, dead
  antenna dishes, a family radio lying in tall grass, overcast sky, light fog"
- [видео] "Static camera with subtle push-in; wind moves the grass, a torn
  wire sways from the tower; only wind and one lonely bird; no music yet"
- [закадр] «Там, где не слышно соседей, быстро становится слышно выстрелы.»

**Шот 3 — «Ржавые» (0:14–0:22)**
- [кадр] "Convoy of scavenged armored trucks with armed marauders in welded
  scrap masks and rusty plate armor rolling through an abandoned industrial
  town at dusk, headlights cutting through dust, rust-orange and olive palette"
- [видео] "Camera tracks sideways with the convoy; a marauder on the lead
  truck slowly turns his welded mask toward the camera; engine rumble, chains
  clinking, distant dog barking"
- [закадр] «Мародёры. “Ржавые”. Им выгодна тишина — в тишине их никто не ждёт.»

**Шот 4 — отряд XRU-1 (0:22–0:30)**
- [кадр] "Four-person special operations squad walking toward camera out of
  hangar backlight, silhouettes resolving into detail: heavy trooper with
  shield plates, female sniper with long rifle, wiry medic adjusting gloves,
  young assault scout grinning; teal holographic map table glowing behind them"
- [видео] "Slow-motion walk toward camera, backlight flares softly, each
  soldier passes through a beam of light one by one; low determined percussion
  starts; boots on concrete"
- [закадр] «Против них — мы. Четверо. Экспедиционная разведгруппа узла. XRU-1.»

**Шот 5 — «Узел-7» и заряд (0:30–0:38)**
- [кадр] "Isolated radio relay station on a hill at night, red beacon light,
  storm clouds; at the base of the antenna — an improvised explosive device
  with a glowing amber countdown timer, wires snaking into the dark"
- [видео] "Drone-style slow orbit around the station; lightning flash reveals
  marauder silhouettes on the catwalks; camera ends on close-up of the ticking
  timer; each tick audible, heartbeat-like"
- [закадр] «Узел-7. Последний ретранслятор сектора. Они не будут его удерживать —
  они его взорвут. Таймер уже идёт.»

**Шот 6 — титульный (0:38–0:45)**
- [кадр] "Tactical holographic map table in a dark command bunker, teal
  topographic contours, a single objective marker pulsing at a relay station
  icon; empty frame center for title"
- [видео] "Camera descends toward the hologram; the marker pulses in sync with
  the timer ticks from previous shot; all sound cuts to silence on the last
  beat" (титул «ТИХИЙ УЗЕЛ» — накладываем в монтаже, не генерим)
- [закадр] «Снять заряд. Забрать всех. Уйти. — Работаем, XRU-1.»

### 2.3 Стыковка и правила генерации видео
- Один шот = один клип 5–8 сек; переходы — резкая склейка по звуку (тик таймера).
- Консистентность: все кадры из одного style block §1; персонажей в видео не
  «переигрывать» (минимум лицевой анимации — маски/силуэты прощают артефакты).
- Каждому i2v-промту добавлять: "cinematic 24fps, no text, no watermark,
  no camera shake, consistent lighting with source image".
- Звук: если сервис даёт нативный аудио-трек (Veo/Kling multishot) — брать его
  как черновой, финальный звук всё равно собираем в Resolve (закадр §6 + музыка §7).

## 3. Картинки UI

| Ассет | Промт (style block +) | Куда |
|---|---|---|
| Фон главного меню | "Tactical command bunker interior, large holographic map table glowing teal in the dark, rain on a small window, empty chair — calm before the operation" | `UI/Art/Menu/T_MainMenu_BG` |
| Фон экрана сложности | "Three worn military dossier folders on a steel desk, green/yellow/red wax seals, holographic projector beside" | `UI/Art/Menu/T_Difficulty_BG` |
| Брифинг туториала | "Military training ground 'Dome': indoor polygon with concrete barriers and holographic target dummies, cyan hologram shimmer" | `UI/Art/Briefing/T_Brief_Tutorial` |
| Брифинг миссии | "Aerial recon photo style: radio relay station courtyard with containers and sandbags, marked enemy positions as red holographic diamonds, bomb icon at north building" | `UI/Art/Briefing/T_Brief_Mission01` |
| Экран победы | "The same relay station at sunrise, beacon light green, drop-ship leaving, hopeful warm light breaking the palette" | `UI/Art/Result/T_Result_Victory` |
| Экран поражения | "Distant explosion flash over the relay station seen from a ridge, silhouettes lowering their heads, cold palette" | `UI/Art/Result/T_Result_Defeat` |
| Финальный экран демо | "Squad of four walking away from camera into morning fog toward a drop-ship, mission patch 'XRU-1' floating as hologram" | `UI/Art/Result/T_DemoComplete` |

## 4. Портреты отряда (512×512, по пояс, единый ракурс ¾)

Общее продолжение промта: "…character portrait, three-quarter view, chest-up,
neutral gray-teal background, same lighting across set".
- **Молот (танк):** "heavyset stoic male trooper ~40s, buzz cut, scarred jaw, oversized shoulder shield plates, calm heavy gaze"
- **Оса (снайпер):** "lean focused female sniper ~30s, tight dark braid, thin optics visor raised, long rifle strap across chest"
- **Шприц (медик):** "wiry male combat medic ~35, tired kind eyes, stubble, red-cross shoulder patch worn to pink, syringe pens in chest rig"
- **Клин (штурмовик):** "young restless male assault scout ~25, short mohawk, cocky half-smile, light armor, compact SMG"
- **Мародёр (враг):** "faceless raider in welded scrap mask and rusty plate armor, olive rags, menacing but human" (+ вариант с другой маской)

Куда: `UI/Art/Portraits/T_Portrait_Tank|Sniper|Medic|Assault|Marauder`. 

## 5. Иконки (256×256, PNG-alpha, единый стиль)

Промт-обёртка: "Flat military UI icon, single glyph, off-white on transparent,
thin cyan outer glow, XCOM-style minimalism, no text: …"

| Иконка | Глиф |
|---|---|
| Атака | "rifle crosshair" |
| Перемещение | "boot print with motion chevrons" |
| Overwatch | "open eye inside a sight reticle" |
| Глухая оборона | "figure crouching behind a shield" |
| Полевая медицина | "medical cross with a pulse line" |
| Рывок и удар | "double chevron forward with muzzle flash" |
| Прицел отряда | "reticle connected to two ally dots by lines" |
| Провокация | "shield with taunting exclamation, radiating arcs" |
| Пропустить ход | "hourglass with fast-forward arrows" |
| Обезвредить | "wire cutters over a bomb outline" |
| Эвакуация | "figure running into smoke column, up arrow" |
| Half cover | "half shield" / Full cover: "full shield" |
| Классы (4) | helmet variants: heavy / hood+optic / cross / arrow |
| Статусы | "downed: kneeling figure", "taunt aura", "hunker shield down" |
| Завершить ход | "circular arrow around hourglass" |
| Счётчик врагов | "minimal skull, front view" |

Куда: `UI/Icons/T_Icon_<Name>`.

## 6. Озвучка (TTS: ElevenLabs Starter ~$5 / Silero бесплатно, RU)

Тексты — СТРОГО из 02_LORE_SCRIPT §5–6 и §2.2 этого файла (не перефразировать
при генерации, иначе субтитры разойдутся с голосом).

**Голоса и манера:**
- **Купол** (инструктор, ~80% реплик): мужской, низкий, размеренный; суховатая
  ирония БЕЗ улыбки в голосе; никогда не кричит — на «ТРИ хода» лишь ускоряется
  и твердеет. Референс-инструкция TTS: "calm, dry, unhurried military radio
  operator; subtle irony; never shouts".
- **Молот**: очень низкий, медленный, слова поштучно. **Оса**: ровный,
  чеканный, «по уставу». **Шприц**: быстрый, ворчливый, тёплый на дне.
  **Клин**: молодой, азартный, чуть торопится.

**Обработка всех VO:** radio-фильтр (highpass 300Hz, lowpass 3.4kHz, лёгкая
компрессия + шорох эфира на -30dB); интро-закадр — БЕЗ радиофильтра (чистый
«голос рассказчика»), фильтр включается с первой игровой реплики.

**Набор:** закадр интро (6 реплик, §2.2) · туториал A1–D3 (Купол + вставки
бойцов: A4 Шприц, A9 Клин, B2/B5-закрытие Оса, B4 Молот, C2 Клин, D2 Оса) ·
миссия (9 реплик таблицы 02 §6) · эпилог + 2 поражения.
Куда: `Content/XRU1Game/Audio/VO/VO_<Кто>_<Метка>` (пример: `VO_Kupol_A5`).

## 7. Музыка и звуки (если генерить: Suno/Stable Audio)

- Меню: "slow ambient military drone, cold synth pads, distant morse blips,
  2 min loop, melancholic but resolute" → `Audio/Music/S_Music_Menu`.
- Бой: "tense minimal percussion loop, muted taiko + ticking clock texture,
  builds subtly, loopable 90s" → `S_Music_Combat`.
- Хаб: "quiet hopeful ambient, radio static swells, warm pad" → `S_Music_Hub`.
- SFX проще взять готовые (Fab/freesound, этап 2), генерация не нужна.

## 8. Текстуры/материалы окружения

НЕ генерировать (качество/тайлинг хуже готовых) — брать Megascans/Fab
(этап 2 роадмапа). Исключение — **голограмма карты хаба**: эмиссив-текстура
"topographic contour lines pattern, thin cyan lines on black, seamless" →
`Content/XRU1Game/FX/T_HoloGrid` (в материал M_Hologram).

## 9. Порядок работы

1. Сгенерировать по секции, класть в исходники `RawContent/` вне репо ИЛИ
   сразу импорт в указанные папки Content (LFS подхватит PNG/WAV).
2. После импорта — вписать ассет в таблицу «Об авторе» (титры, GDD §15/§4).
3. Один стиль = один style block: если меняешь палитру/настроение — правь
   §1 здесь, а не в отдельных промтах.
