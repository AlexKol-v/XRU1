"""
Наведение порядка в Content: всё, что нужно игре, переезжает под /Game/XRU1Game/.

ЗАЧЕМ СКРИПТ, А НЕ РУКАМИ ИЛИ ФАЙЛАМИ
-------------------------------------
1. Файловый перенос внутри проекта ЛОМАЕТ ссылки. `.uasset` хранит зависимости
   строками вида `/Game/OtherAssets/.../anim_CoverDown_Idle_Right` — стоит
   переложить файл, и ссылка указывает в никуда. (Перенос МЕЖДУ проектами по
   тому же пути безопасен — так сюда и попали новые ассеты.)
2. `EditorAssetLibrary.rename_asset` делает то же, что «Move» в Content Browser:
   переносит ассет И правит все ссылки на него, оставляя редиректор.
3. Руками это 300+ операций.

КАК ЗАПУСКАТЬ
-------------
1. Открыть проект в редакторе.
2. Включить плагин **Python Editor Script Plugin**, если ещё не включён
   (Edit → Plugins → искать "Python"), перезапустить редактор.
3. Tools → Execute Python Script… → выбрать этот файл.
   Либо в Output Log переключить ввод на `Python` и выполнить:
       exec(open(r"D:/Unrial_Projects/XRU1/Content/Python/reorganize_assets.py").read())
4. Сначала он идёт в РЕЖИМЕ ПРОВЕРКИ и только печатает план. Убедиться, что
   план разумный, затем поставить DRY_RUN = False и запустить снова.
5. После переноса: ПКМ по /Game → **Fix Up Redirectors in Folder**.
6. Сохранить всё (Ctrl+Shift+S) и закоммитить.

⚠️ ПЕРЕД ЗАПУСКОМ — СДЕЛАЙ КОММИТ. Операция массовая; откатиться проще всего
git-ом, а не руками.
"""

import unreal

# ⚠️ True — только напечатать план, ничего не менять. Первый запуск — только так.
DRY_RUN = True

# Куда всё складываем. Одна папка проекта — чтобы потом можно было удалить
# ВСЁ остальное как мусор шаблона и ничего не сломать.
ROOT = "/Game/XRU1Game/Units"

# (откуда, куда). Порядок значения не имеет — каждый перенос независим.
MOVES = [
    # --- Анимации, перенесённые из UE-HW -----------------------------------
    ("/Game/OtherAssets/FreeAnimationLibrary/Animations/Cover",  ROOT + "/Anim/Cover"),
    ("/Game/OtherAssets/FreeAnimationLibrary/Animations/Revive", ROOT + "/Anim/Revive"),
    ("/Game/Characters/Animation/Crouch",                        ROOT + "/Anim/Crouch"),

    # --- Анимации шаблона UE5, которые реально используем -------------------
    ("/Game/Characters/Mannequins/Anims/Rifle",                  ROOT + "/Anim/Rifle"),
    ("/Game/Characters/Mannequins/Anims/Death",                  ROOT + "/Anim/Death"),

    # --- Оружие -------------------------------------------------------------
    ("/Game/InfimaGames/ModernGunsBundle/ModernAssaultRifle",     ROOT + "/Weapons/AssaultRifle"),
    ("/Game/InfimaGames/ModernGunsBundle/ModernSMG",              ROOT + "/Weapons/SMG"),
    ("/Game/InfimaGames/ModernGunsBundle/ModernLightMachineGun",  ROOT + "/Weapons/LMG"),
    ("/Game/InfimaGames/ModernGunsBundle/ModernSniper",           ROOT + "/Weapons/Sniper"),
    ("/Game/InfimaGames/ModernGunsBundle/_Common",                ROOT + "/Weapons/Common"),
]

# Скелет библиотеки анимаций. Переносится ОТДЕЛЬНО и с ПЕРЕИМЕНОВАНИЕМ: в
# ROOT/Meshes уже лежит наш SK_Mannequin, и два ассета с одним именем в одной
# папке невозможны.
SKELETON_MOVE = (
    "/Game/OtherAssets/FreeAnimationLibrary/Demo/Characters/Mannequins/Meshes/SK_Mannequin",
    ROOT + "/Meshes/SK_Mannequin_AnimLib",
)


def move_asset(src, dst):
    """Перенос ОДНОГО ассета с правкой всех ссылок на него."""
    if not unreal.EditorAssetLibrary.does_asset_exist(src):
        unreal.log_warning("  пропуск (нет ассета): %s" % src)
        return False
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        unreal.log_warning("  пропуск (уже есть): %s" % dst)
        return False
    if DRY_RUN:
        unreal.log("  [план] %s  ->  %s" % (src, dst))
        return True
    if unreal.EditorAssetLibrary.rename_asset(src, dst):
        unreal.log("  ok: %s -> %s" % (src, dst))
        return True
    unreal.log_error("  ОШИБКА переноса: %s" % src)
    return False


def move_folder(src_dir, dst_dir):
    """
    Перенос ПАПКИ поассетно.

    ⚠️ Намеренно НЕ `rename_directory`: она падает целиком при первой же
    коллизии имён, и непонятно, что успело переехать. Поассетный перенос
    пропускает проблемные и честно печатает их — остальное переезжает.
    """
    if not unreal.EditorAssetLibrary.does_directory_exist(src_dir):
        unreal.log_warning("нет папки, пропуск: %s" % src_dir)
        return 0, 0
    unreal.log("=== %s -> %s" % (src_dir, dst_dir))
    assets = unreal.EditorAssetLibrary.list_assets(src_dir, recursive=True, include_folder=False)
    moved = 0
    for asset_path in assets:
        # /Game/A/B/Name.Name -> относительный путь внутри папки-источника
        package = asset_path.split(".")[0]
        rel = package[len(src_dir):].lstrip("/")
        if not move_asset(package, "%s/%s" % (dst_dir, rel)):
            continue
        moved += 1
    return moved, len(assets)


def main():
    unreal.log("")
    unreal.log("################ РЕОРГАНИЗАЦИЯ КОНТЕНТА ################")
    unreal.log("Режим: %s" % ("ПРОВЕРКА (ничего не меняется)" if DRY_RUN else "ПЕРЕНОС"))
    unreal.log("")

    total_moved = 0
    total_seen = 0
    for src, dst in MOVES:
        moved, seen = move_folder(src, dst)
        total_moved += moved
        total_seen += seen

    unreal.log("=== скелет библиотеки анимаций (с переименованием) ===")
    if move_asset(*SKELETON_MOVE):
        total_moved += 1
    total_seen += 1

    unreal.log("")
    unreal.log("ИТОГО: %d из %d" % (total_moved, total_seen))
    if DRY_RUN:
        unreal.log("Это была ПРОВЕРКА. Поставь DRY_RUN = False и запусти снова.")
    else:
        unreal.log("Готово. Теперь: ПКМ по /Game -> Fix Up Redirectors in Folder, затем Ctrl+Shift+S.")
    unreal.log("########################################################")


main()
