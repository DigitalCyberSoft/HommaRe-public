# Облик лончера из текстур игры

Статус: 2026-10-18, ветка `launcher-skin` (от `launcher-gog-install`). Работает на Mac; iOS-код собирается, на устройстве не проверялся.

Макет: холст Claude Design «Heroes V Launcher» (доски Main, Install, Settings, Kit).

## Что видит игрок

Если на устройстве есть установка игры, лончер рисуется её же текстурами:

| Элемент | Текстура в pak |
|---|---|
| Фон | небо 3D-меню `Arenas/Interface/MainMenu/pPlane1-2` (в ToE его нет, тогда размытый сплэш) |
| Логотип | `UI/MainMenu2/Logo` (у каждой игры свой, обрезается по непрозрачным пикселям) |
| Список игр и меню | свиток `UI/MainMenu2/SelectCampaign/MenuPanel`, 9-slice 34/14/46/14 |
| Выбранная строка, наведение | `UI/MainMenu2/ChangeProfile/ProfileList(Pre)Selection`, аддитивно |
| Описание игры | пергамент `UI/MainMenu2/SetupPanel/SetupPanel`, 9-slice 22 |
| Рамка сплэша, метка «Скоро» | `UI/MainMenu2/SetupPanel/DifficultyList`, 9-slice 8 / 6 |
| «Играть» | `UI/Common/Button_136x49` (Active / Pushed / Disabled, Highlight поверх при наведении) |
| Вторичные кнопки | `UI/Common/DlgButtons/Button_122_34_*` |
| Окно установки | `Textures/Interface/CommonBoxes/Box0p`, кнопка `button_ok`, значок `UI/MessageBox/Message`, полоса `ScrollBarBg` |
| Настройки | страница `UI/GameOptions/MainBackground`, флажки `CheckBox/Button_0/1_Normal` |

Шрифты: меню — Cormorant Garamond Bold Italic, тёмно-синий `#1D2A55`; кнопки — PT Sans Bold, золото `#F6C94C`; описания — PT Serif Italic. PT Sans и PT Serif есть в macOS. Cormorant Garamond в системе нет: пока вместо него Baskerville Bold Italic (на iOS тоже есть), шрифт из `Contents/Resources/Fonts/*.ttf|otf` подхватывается сам (Cormorant — OFL, его можно положить в бандл).

Пока идёт поиск игр, в подвале слева видно «Ищем ваши игры…».

Меню свитка: «Установить из GOG…» (Mac) или «Добавить файлы игры…» (iOS), «Настройки», «Выход» (Mac). Настройки: язык лончера (как в системе / русский / English, сразу перерисовывает), папки с играми и «Добавить…», папка сохранений и «Показать».

Без игры на устройстве, при `RECOMP_LAUNCHER_SKIN=0` и на узком экране (iPhone, iPad в портрете) — прежний плоский облик.

Окно меньше 1200 × 760 — экран раскладывается на 1200 × 760 и уменьшается целиком, как интерфейс самой игры.

## Как устроено

| Часть | Где |
|---|---|
| Чтение pak (zip без zip64, имена без учёта регистра, deflate через Compression), DDS (DXT1/3/5, несжатые по маскам каналов), каталог текстур с прямоугольниками, кэш | `kit/host/launcher/apple/LauncherSkin.swift` |
| Экраны: фон, свиток, пергамент, кнопки, окно установки, настройки, шрифты и цвета | `kit/host/launcher/apple/LauncherSkinView.swift` |
| Загрузка облика после каждого поиска (в той же фоновой задаче, поэтому `RECOMP_LAUNCHER_DUMP` снимает уже с ним), язык, список папок | `LauncherModel.swift` |
| Выбор облика и масштаб | `LauncherRootView` в `LauncherView.swift` |
| Тексты ru/en, выбор языка (`launcher.language` в UserDefaults) | `LauncherStrings.swift` |

Источник текстур — установка, которую лончер запустил бы первой: готовая игра этого приложения, затем любая найденная. Паки читаются в порядке игры: `data.pak`, потом `*-data.pak` по алфавиту, последний выигрывает.

Кэш: при первой загрузке текстуры декодируются (≈0,15 с) и сохраняются PNG-файлами в `~/Library/Application Support/<приложение>/LauncherSkin/<штамп>/` (на iOS — без бэкапа). Штамп — хеш путей, размеров и дат паков, так что обновлённая игра пересобирает кэш. Повторная загрузка из кэша — ≈4 мс. В репозиторий и в бандл ничего из игры не попадает.

Подробности текстур:
- Игра дополняет текстуры до степени двойки; картинка — в прямоугольнике, заданном в каталоге.
- `Highlight`-текстуры кнопок и подсветка строк списка — `CONVERT_TRANSPARENT_ADD` в `.xdb`: альфа пустая, цвет прибавляется. Их цвет в данных игры фиолетово-синий; на пергаменте это выглядело розовым, поэтому при декодировании яркость пикселя переносится на золото `#F6C94C`, и текстура рисуется непрозрачной с `.plusLighter` (свет на свитке). Выбранная строка — с непрозрачностью 0,4.

## Проверка

- Превью без сборки игры: ImageRenderer-харнесс собирает Swift-файлы лончера отдельно и рисует экраны в PNG (1280 × 800, 960 × 640, 1728 × 1080, ru/en, только ToE, без игры).
- Приложение: side-сборка `~/Games/homm5-skin-build` (симлинки на worktree `~/Games/homm5-skin`, APFS-клон `build/recomp` из `homm5-gog-build`), `RECOMP_LAUNCHER=1 RECOMP_LAUNCHER_DUMP=<png>`.
- iOS: `swiftc` для `iphoneos` и `iphonesimulator`, цель 17.0 — без ошибок и предупреждений.
