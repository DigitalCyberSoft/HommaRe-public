# Дополнения: Владыки Севера и Повелители Орды

Статус: 2026-10-18, ветка `expansions` (от `ios`).

На macOS обе игры работают из общего лончера: главное меню, профиль, сценарий, карта приключений. На iPad-симуляторе обе запускаются из того же лончера и доходят до главного меню. На устройстве ещё не проверялось.

## 1. Что это за игры для kit

| | Базовая игра | Hammers of Fate | Tribes of the East |
|---|---|---|---|
| Каталог игры | корень репозитория | `games/hof` | `games/toe` |
| exe | `bin/H5_Game.exe` 1.60 | `bina1/H5_Game.exe` 2.1.3.1068 | `bin/H5_Game.exe` 3.1.2.99, отдельная установка |
| `game.id` / `app_name` | `homm5` / `HommaRe` | `homm5-hof` / `HommaRe-HoF` | `homm5-toe` / `HommaRe-ToE` |
| Точка входа | `009fd7c3` | `009e4653` | `00949fe3` |
| Функций в Ghidra | 69 515 | 67 469 | 55 325 |
| granny2.dll | общая | та же, что у базы | та же, но с подписью Authenticode (свой SHA-256) |
| Папка профилей (My Games) | `Heroes of Might and Magic V` | своя | `Heroes of Might and Magic V — Tribes of the East` |

У каждой игры свой `original/` — симлинк на папку с её exe. Листинги Ghidra лежат в её `analysis/`, сборка — в её `build/`; всё это в `.gitignore`.

## 2. Подготовка и сборка

```bash
# Листинги Ghidra с автоанализом (setup.py импортирует с -noanalysis, так нельзя)
python3 games/run_analysis.py games/hof
# Трансляция и сборка (нужен Python с capstone, Pillow, pefile: ~/Games/homm5-ios/.venv)
python kit/tools/build.py --game-dir games/hof --regenerate --target app --game-modules
python kit/tools/build.py --game-dir games/hof --target ios-sim --game-modules --no-install
```

Трансляция ToE занимает около 8 минут, HoF около 15. Компиляция — около 25 минут на игру и платформу.

## 3. Одно приложение — несколько игр

На iOS нельзя запустить другой exe, поэтому каждая игра собирается ещё и модулем: `libgame.<id>.dylib` с точкой входа `recomp_game_main`. Это CMake `RECOMP_GAME_MODULES`, флаг `build.py --game-modules`.

- **Приложение базовой игры** — оболочка с лончером. В этом режиме SDL у него динамический (`libSDL3.0.dylib` в Frameworks), общий с модулями: две копии SDL не поделили бы UIKit/AppKit.
- **Выбор другой игры в лончере** (`sdl/main.cpp`, `run_game_module`): приложение освобождает своё окно и Metal-устройство, делает `dlopen` модуля из `Frameworks` и вызывает `recomp_game_main --exe <путь>`. Модуль сам создаёт окно на том же SDL. Так устроено и на Mac, и на iOS.
- **Модуль при входе** (`module_prepare`):
  - берёт ресурсы из `Resources/games/<app_name>` (на iOS — `games/<app_name>`);
  - заводит профиль `os_user_data_dir(<app_name>)`, свой у каждой игры;
  - выставляет `RECOMP_MODULE=1`, и `layout.cpp` перестаёт искать checkout.
- **Сборка общего приложения:** `kit/tools/assemble_games.py --app <приложение базы> --game games/hof --game games/toe [--platform ios-sim|ios]`. Скрипт кладёт модули и ресурсы, затем переподписывает приложение той же подписью (ad hoc для Mac и симулятора).
- **Лончер:** `launcher-games.json` → `module`. Игра доступна, если её модуль есть в приложении; иначе на ней метка «Скоро».
- **Отдельная сборка HoF или ToE** (`build.py --game-dir games/toe`, без `--game-modules`): лончер тот же, `[launcher] config_dir = "../.."` в их `game.toml` берёт `launcher-games.json`, манифесты и заставку из корня репозитория. iOS-сборка с файлами игры кладёт в `game/` всю установку: если у exe в его папке нет `[setup] required_dirs`, а у папки выше есть, берётся папка выше (`game_config` `developer_stage_dir`). При первом запуске лончер импортирует её в `Documents/Games`. У базы `game-flat` не меняется. `assemble_games.py` файлы лончера в ресурсы модулей не копирует. Дерево, где раньше была отдельная iOS-сборка, держит в `game/` старые файлы из `bin/`: стейджинг ничего не удаляет, их нужно убрать руками.

Отладочные переключатели:
- `RECOMP_LAUNCHER_PICK=<id игры>` — выбрать игру в лончере;
- `RECOMP_LAUNCHER_AUTOPLAY=1` — нажать «Играть»;
- `RECOMP_LAUNCHER_VIA_MODULE=1` — запустить базовую игру через её модуль (проверка пути через `dlopen`);
- `RECOMP_LAUNCHER_GAMES_DIR` — где искать установки.

## 4. Импорты, которых не было у базы

Импорты есть в kit:
- `d3dx9_25` — как алиас `d3dx9_26`;
- `D3DXLoadSurfaceFromMemory` и `D3DXSaveTextureToFileA` — пока только сообщают о себе в лог; до карты приключений ни одна игра их не вызывает;
- `um.dll!TrackingCount`, `zlib deflateInit2_`, `_msize`, `_ftol`, `GetFileAttributesExA`.

## 5. Нативный код с адресами игры

`native/touch.c` (адаптер касаний) и `native/save.cpp` (сохранение настроек при уходе в фон) общие для всех игр. Адреса они берут из `RECOMP_ADDRESS_HEADER` — это `games/<id>/native/addresses.h` (ключ `addresses` в `[translate] native`); значения базовой игры остаются значениями по умолчанию. Этот заголовок видят только нативные исходники, а не сгенерированные чанки, поэтому правка адреса пересобирает только их. В `overrides.h` остаются лишь `FN_<addr>`, которые включает каждый чанк.

Адреса найдены сопоставителем `games/map_addresses.py`:
- функции сравниваются по байтам, в которых замаскированы релокации и смещения переходов (capstone);
- данные определяются голосованием по операндам совпавших функций;
- функция без прямого совпадения находится по вызывающим её функциям;
- vtable — по RTTI-имени класса. Подобъект `IScreen` у экранов карты и боя в дополнениях лежит на другом смещении: у HoF `+0x778`/`+0x53c`, у ToE `+0x844`/`+0x564`. Порядок подобъектов при этом тот же.

```bash
python games/map_addresses.py . games/toe 0070cf80 0114da18 00fab2ac
```

`SCREEN_WINDOWS` и `WINDOWS_MODAL` (смещения внутри объекта экрана) перенесены без проверки.

## 6. Проверено

- **macOS, через лончер и модуль:**
  - ToE: меню 3.1 → профиль → «Сценарии» → «Боевой клич свободы» → вступительная сцена → карта;
  - HoF: меню 2.1 → профиль → «Сценарии» → первая карта → карта, ход героя;
  - падений и нереализованных импортов нет.
- **iPad-симулятор:**
  - лончер со скином показывает обе игры как готовые;
  - HoF (2.1) и ToE (3.1) через `libgame.homm5-hof.dylib` / `libgame.homm5-toe.dylib` доходят до главного меню;
  - базовая игра через свой модуль тоже доходит до меню.
- **Не проверено:**
  - iPad или iPhone с подписью;
  - глубже главного меню на iOS (сценарии и карта проверены только на Mac);
  - адаптер касаний дополнений на адресах из раздела 5.
