# Моды интерфейса: рецепты вместо файлов

Статус: 2026-10-21, ветка `ios-gog-install` (после слияния `integrate` c1f0174).

Моды интерфейса раньше собирались при сборке из файлов игры разработчика и лежали в приложении (`profile-seed/` через `[ios]`/`[macos] profile_files`). Теперь в репозитории и в приложении только **рецепты** — что поменять, без самих файлов; мод собирается из установки игрока при запуске игры, на iOS и на Mac.

Какие рецепты попадают в приложение, говорит `game.toml` каждой игры: `[ios] ui_mods` и `[macos] ui_mods` (имена файлов `ui-mods/<имя>.recipe`). Сейчас:

| Мод | Генератор | iOS | Mac | Куда в профиле (база, ToE / HoF) |
|---|---|---|---|---|
| Widescreen | `tools/widescreen_mod.py --wide-dialogues` (HoF ещё `--border` экрана загрузки) | все три | все три | `UserMODs/Widescreen.h5u` / `data/zz-widescreen.pak` |
| PhoneUI | `tools/phone_ui_mod.py` (нейтральный: раскладка в `native/phone_layout.c`) | только iPhone (или «всегда») | — | `UserMODs/PhoneUI.h5u` / `data/zz-phone-ui.pak` |
| TouchUI | `tools/touch_ui_mod.py` (рамка перетаскиваемого существа под палец) | все три | — | `UserMODs/TouchUI.h5u` / `data/zz-touch-ui.pak` |
| MobileOptions | `tools/mobile_options_mod.py` (без выбора разрешения) | все три | — | `UserMODs/MobileOptions.h5u` / `data/zz-mobile-options.pak` |

Широкий мод ToE — только диалоговые сцены на весь экран (свои якоря у ToE есть). Тихий щелчок кнопок — не мод, а `[texts]` (`host/text_overrides.cpp`).

**Формат** — `tools/ui_recipe.py` (там же описание и эталонное применение на Python): заголовок (имя, путь в профиле, дата записей zip, слои паков по порядку, мод-основа), затем для каждого файла — SHA-256 исходной копии и правки по строкам (`@ строка удалить` + новые строки). Формат 2 добавил `turn left`: картинка — исходный 32-битный DDS, повёрнутый на четверть влево (рамка TouchUI). Новые строки проверяются при записи: только раскладка интерфейса (Placement, якоря, HorAlign/VerAlign, Visible/Hide, Background/Foreground/Shared/Item/Texture href, размеры и имена текстуры, ObjectRecordID). Файл с другой SHA-256 (иная сборка игры) пропускается.

**Что не из игры** (пишется целиком, `new`): шторки широкого экрана (`UI/Common/BlackBorder/*`, `WideBorder/*`) и объявления `HorAlign`/`VerAlign` в `types.xml` — `tools/ui_mod_parts.py` (`widescreen_mod.py` сверяет их с копиями ToE разработчика; отличаются только наши слова в двух `<Description>`), плитки пергаментов диалогов (`DialogueScreen/*-Wide.(BackgroundTiledTexture).xdb`) и записка нейтрального PhoneUI.

**На устройстве:** `kit/host/ui_mods.cpp`, вызывается из `host/sdl/main.cpp` после `platform_ui_seed_profile` на всех платформах (рецептов нет — ничего не делает). Читает паки игрока сам (центральный каталог по размеру: в `data.pak` ToE 84 312 записей — больше 16-битного счётчика zip), применяет правки, пишет zip как `text_overrides.cpp`. Штамп `<профиль>/.ui-mods/<имя>.stamp` (рецепт + размеры/даты паков + штамп основы) — повторный запуск ничего не делает. Телефонные (`out` с `phone/`) — только при `platform_ui_phone_interface()`, иначе удаляются. Рецепты кладут в бандл `cmake/IosBundle.cmake` и `cmake/MacBundle.cmake` (`ui-mods/`, только перечисленные в `ui_mods`; старые `ui-mods/` и `profile-seed/` прошлой сборки удаляются); для HoF/ToE — их сборки, `assemble_games.py` переносит в `games/<app>/`.

**Обновить рецепты** после правок генераторов (нужны свои файлы игры) — одна команда, она же проверка:

```sh
.venv/bin/python tools/ui_recipes.py --game ~/Heroes\ V/game --toe ~/Heroes\ V/game-toe
```

Она запускает 12 генераторов (4 мода × 3 игры) на установке разработчика, кладёт моды в `build/ui-mods/`, пишет рецепты в `ui-mods/`, `games/hof/ui-mods/`, `games/toe/ui-mods/`, применяет каждый рецепт к той же установке (`ui_recipe.apply`) и сверяет с модом генератора: те же файлы побайтно и те же даты записей. `--check-only` — только проверка; `--reference build` — ещё и сравнение содержимого с модами, которые лежат в `build/` другой сборки.

**Проверено** (2026-10-21): все 12 рецептов, применённые и Python-эталоном, и `ui_mods.cpp` (собран на Mac отдельно с miniz), к базовой игре и ToE дают моды с теми же файлами (побайтно) и датами, что генераторы. Сами zip побайтно не совпадают: сжимают miniz и zlib по-разному. С модами integrate (`build/` той сборки, широкие — из `homm5-wsfix` 12:35): TouchUI, MobileOptions, Widescreen HoF и ToE — те же файлы; у базового Widescreen — `types.xml` (наши слова в `<Description>`); PhoneUI — нейтральный, как пишет `phone_ui_mod.py` integrate сейчас (в `build/phone` integrate лежит старый мод с раскладкой от 2026-10-19).
