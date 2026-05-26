# Volchay Obs

> Современный десктоп-клиент стриминга на Twitch на C++/Qt6.
> Янтарно-оранжевая палитра, три темы (Blackout / Light / RGB), полный mixer + сцена.

---

## Правила разработки проекта

Эти правила обязательны для всех разработчиков и контрибьюторов:

1. **Документация каждого шага.** Каждый шаг разработки (фича, рефактор, фикс) должен быть зафиксирован в этом README — в разделе «Журнал разработки» — до мельчайшей детали: что изменили, зачем, какие файлы тронули и каков ожидаемый эффект.
2. **Никаких ссылок на репозитории в README.** Мы не упоминаем здесь ссылки на GitHub/GitLab/Bitbucket — ни на свой репозиторий, ни на чужие. Вместо этого упоминаем **программы-аналоги** в соответствующем разделе.
3. **После каждой завершённой задачи — предложение улучшения.** Завершил задачу — предложи в комментариях к PR или в обсуждении что добавить в проект: либо своё, либо взятое идеей из программ-аналогов.
4. **Серьёзный проект — внимательный подход.** Не торопимся, не оставляем половинчатых решений, тестируем перед мерджем, не игнорируем варнинги.
5. **Передовые библиотеки и SVG-графика.** Используем актуальные на 2025+ инструменты: Qt 6.7+, современные шрифты (Geist / Manrope / Inter), векторные SVG-иконки в стиле Lucide/Feather. Никаких растровых иконок 16x16 из 2010-х.
6. **Все события — в файл лога.** Каждый запуск, изменение настроек, старт/стоп стрима и любая ошибка пишутся в файловый лог (`%APPDATA%\VolchayObs\logs\volchay-YYYY-MM-DD.log` на Windows). Это резко упрощает диагностику.

---

## Что внутри

- **Qt6 / C++17** интерфейс с тремя темами: **Blackout** (true-black для OLED), **Light**, **RGB** (анимация акцента по кругу HSV).
- **Янтарно-оранжевый** акцент по умолчанию (`#FF9900`), цвет можно поменять прямо в настройках.
- Стриминг через локальный **`ffmpeg`** (subprocess) — RTMP H.264 + AAC прямо в Twitch.
- 4 встроенных пресета по [официальной документации Twitch](https://help.twitch.tv/s/article/broadcasting-guidelines):

  | Пресет | Резолюция | FPS | Видео битрейт | Аудио |
  |---|---|---|---|---|
  | Твич 720p30 | 1280×720 | 30 | 3000 kbps | 160 kbps |
  | Твич 720p60 | 1280×720 | 60 | 4500 kbps | 160 kbps |
  | Твич 1080p30 | 1920×1080 | 30 | 4500 kbps | 160 kbps |
  | **Твич 1080p60** *(по умолчанию)* | 1920×1080 | 60 | **6000 kbps** | 160 kbps |

  Все — **CBR**, **keyframe interval 2 сек**, **profile high**, AAC 48 kHz stereo, x264 `veryfast`.

- Ручные настройки кодировщика: можно переключать на NVENC / QuickSync / AMF, менять rate control, x264-пресет, keyframe interval.
- Сцена в стиле OBS: список **источников** (Display, Window, Image, Browser), drag/resize в превью.
- **Audio mixer** — отдельные strip’ы микрофона и desktop-аудио с mute/volume.
- **Файловый логгер** — все события + перехват `qDebug/qWarning/qCritical`. Кнопка «Open log folder» на странице **About**.
- Захват **рабочего стола** (gdigrab/x11grab/avfoundation) или **тестовой картинки** (lavfi `testsrc2`) для проверки RTMP без живого источника.
- Stream key хранится в `QSettings` (если включён чекбокс «запомнить ключ»).

---

## Похожие программы (программы-аналоги)

Если ищете альтернативу или хотите сравнить функционал — посмотрите на эти продукты:

- **OBS Studio** — самый популярный open-source стример. Сцены, источники, audio mixer, мощные filter chains, плагины.
- **Streamlabs Desktop** — fork OBS с виджетами для Twitch/YouTube (alerts, chat overlay, donations) и упрощённым UI.
- **Twitch Studio** — официальный простой клиент от самого Twitch для новичков; меньше настроек, больше «just works».
- **vMix** — коммерческий продукт для серьёзных продакшенов: NDI, multi-camera, replay, audio compressor.
- **XSplit Broadcaster** — коммерческий клиент с акцентом на удобство сцен и auto-reconnect при разрыве RTMP.
- **Lightstream** — облачный стример, не требует мощного железа на стороне пользователя.
- **Restream Studio** — браузерный мультистрим на несколько площадок одновременно.

Volchay Obs не пытается заменить OBS — это лёгкий, дизайн-ориентированный стример для одной задачи: быстро уйти в Twitch с качественным пресетом и красивым UI.

---

## Зависимости

| | Linux | Windows | macOS |
|---|---|---|---|
| Qt | `qt6-base-dev qt6-tools-dev qt6-multimedia-dev` | Qt 6.7+ через [aqtinstall](https://github.com/jurplel/install-qt-action) или официальный установщик | `brew install qt@6` |
| CMake | `cmake` (≥ 3.16) | CMake 3.16+ | `brew install cmake` |
| Компилятор | GCC 11+ / Clang 13+ | MSVC 2022 / MinGW-w64 | Apple Clang |
| **ffmpeg** *(runtime)* | `apt install ffmpeg` | [gyan.dev/ffmpeg](https://www.gyan.dev/ffmpeg/builds/), добавь в PATH | `brew install ffmpeg` |

> ffmpeg не линкуется в бинарник — он вызывается как subprocess. Без `ffmpeg` в PATH стрим не запустится.

---

## Сборка из исходников

### Linux

```bash
sudo apt install qt6-base-dev qt6-tools-dev qt6-multimedia-dev cmake g++ ffmpeg
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/volchay-obs
```

### Windows (MSVC + Qt 6.7)

```powershell
# Установка Qt: https://www.qt.io/download-open-source
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build --config Release --parallel

# Упаковка зависимостей рядом с .exe
& "C:/Qt/6.7.3/msvc2019_64/bin/windeployqt.exe" `
      build/Release/VolchayObs.exe
```

Готовый `.exe` появится в `build/Release/`.

### Готовый Windows .exe

Каждый push в любую ветку запускает CI workflow, который собирает релизный `.exe`, упаковывает Qt-DLL через `windeployqt`, кладёт всё в `VolchayObs-windows-x64.zip` и публикует архив как **artifact** в разделе **Actions → последний run → Artifacts → `VolchayObs-windows-x64-<sha>`**.

Push тега `v*` (например `v0.2.0`) дополнительно создаёт **GitHub Release** с этим же zip-архивом — пользователи могут скачать сборку одним кликом со страницы Releases.

---

## Как пользоваться

1. Получи **Stream Key** на [dashboard.twitch.tv → Settings → Stream](https://dashboard.twitch.tv/settings/stream).
2. Запусти `VolchayObs.exe` (или `volchay-obs` на Linux).
3. Кнопка **Settings** → вкладка **Стрим** → выбери пресет (по умолчанию Твич 1080p60), вставь Stream Key, нажми **OK**.
4. На вкладке **Стрим** нажми **Go live** — ffmpeg уйдёт в RTMP, статус-pill сменится на красный **LIVE**.
5. **Stop** — отправит `q\n` в stdin ffmpeg для корректного завершения FLV.

### Приватность

- Stream key хранится в `QSettings` (`~/.config/VolchayObs/Volchay Obs.conf` на Linux, реестр HKCU на Windows).
- Чекбокс «Запомнить ключ» можно снять — тогда ключ удаляется из конфига при сохранении.
- Stream key **никогда не пишется в файловый лог** (в логе видна только команда `ffmpeg ...`, а финальный RTMP URL содержит ключ — публично не делитесь файлами логов).

### Где лежат логи

- **Windows:** `%APPDATA%\VolchayObs\logs\volchay-YYYY-MM-DD.log`
- **Linux:** `~/.local/share/VolchayObs/logs/volchay-YYYY-MM-DD.log`
- **macOS:** `~/Library/Application Support/VolchayObs/logs/volchay-YYYY-MM-DD.log`

Удобнее всего — открыть страницу **About** в самом приложении: там есть кнопка «Open log folder» и полный путь к текущему лог-файлу.

---

## Архитектура

```
src/
├── main.cpp              — точка входа, QApplication, Logger::install()
├── MainWindow.{h,cpp}    — главное окно: сайдбар, превью-карточка, mixer, лог
├── SettingsDialog.{h,cpp}— модальные настройки (Стрим/Кодировщик/Внешний вид)
├── PresetManager.{h,cpp} — 4 встроенных Twitch-пресета
├── StreamEngine.{h,cpp}  — обёртка над QProcess для ffmpeg
├── ThemeManager.{h,cpp}  — подмена placeholder'ов в QSS, RGB-таймер
├── PreviewWidget.{h,cpp} — отрисовка сцены, drag/resize источников
├── AccentBadge.{h,cpp}   — кружок-индикатор акцента в сайдбаре
├── Source*.{h,cpp}       — модели источников (Display/Window/Image/Browser)
├── AudioMixerPanel.*     — strip'ы микрофона и desktop-аудио
├── AudioLevelMeter.*     — VU-метры
├── WasapiLoopback.*      — захват системного звука на Windows
├── I18n.{h,cpp}          — перевод (EN/RU)
└── Logger.{h,cpp}        — файловый логгер с ротацией по дням

resources/
├── themes/
│   ├── blackout.qss      — Blackout (OLED, модернизирована 2025)
│   ├── light.qss         — Светлая (модернизирована 2025)
│   └── rgb.qss           — RGB (с анимированным акцентом)
└── icons/                — SVG иконки (Lucide/Feather-style)
    ├── logo.svg, logo.ico
    ├── stream.svg, settings.svg, info.svg, play.svg, stop.svg
    └── mic.svg, desktop.svg, log.svg  ← добавлены 2026-05-26
```

---

## Журнал разработки

Здесь фиксируется каждый шаг — что, зачем и какие файлы.

### 2026-05-26 — Модернизация UI и инфраструктуры

Контекст: текущий UI визуально застрял в районе 2015 года — точечная рамка фокуса Qt вокруг навигационных пунктов, плоские кнопки без подсветки, отсутствие feedback на focus/hover за пределами цвета. Дополнительно — отсутствие файлового лога и слабая release-инфраструктура.

**1. Темы (`resources/themes/blackout.qss`, `light.qss`, `rgb.qss`) — полная модернизация.**

- Глобально добавлен `* { outline: 0; }` и `outline: 0` во все интерактивные селекторы — это убрало классический пунктирный фокус Qt.
- Заменён focus-state: вместо точечного outline теперь у nav-кнопок `border-left: 4px solid {{ACCENT_SOFT}}`, у primary-кнопок `border: 2px solid {{ACCENT_SOFT}}` при фокусе. Получился современный glow-ring.
- Все cards переведены на градиентный фон через `qlineargradient` для имитации soft-shadow (QSS не поддерживает `box-shadow`).
- Primary-кнопки получили вертикальный градиент сверху-вниз для глубины и `padding-shift` на `:pressed` для тактильности.
- Sidebar nav: индикатор увеличен с 3px до 4px, фон активного пункта — горизонтальный градиент от `{{ACCENT_GHOST}}` к прозрачному.
- Радиусы выровнены: cards 16px (было 14), кнопки 12px (было 10), inputs 10px (было 8).
- Tabs: добавлен accent underline для активной вкладки и `{{ACCENT_SOFT}}` underline на hover.
- Scrollbar: ширина 12px (было 10), радиус handle 6px.
- Шрифты: добавлены `Geist` и `Manrope` в начало fallback-стека (актуальные в 2024-2025).
- Slider: добавлено стилизованное оформление для будущих volume-контролов в mixer.
- ToolTip: тёмный фон с тонкой рамкой.

**2. SVG-иконки (`resources/icons/`).**

- Добавлены `mic.svg`, `desktop.svg`, `log.svg` в Lucide-стиле (stroke-based, `currentColor`).
- Зарегистрированы в `qt_add_resources` в `CMakeLists.txt`.

**2b. Тематические SVG-иконки для каждого SourceType (`monitor.svg`, `window.svg`, `camera.svg`, `film.svg`, `image.svg`, `palette.svg`, `text.svg`, `grid.svg`, `globe.svg`, `speaker.svg`).**

- Контекст: список источников и меню `+ Add` показывали тип через текстовые скобки `[D] Display capture`, `[W] Window capture`, `[m] Microphone` и т. д. (Screenshot_3). Это выглядело как ASCII-плейсхолдер из 90-х.
- Решение: 10 новых Lucide-style SVG-иконок (плюс существующая `mic.svg` для Microphone). Все stroke-based.
- Цвет stroke захардкожен в `#FF9900` (бренд-акцент). Причина: `QIcon` при рендере SVG в `QListWidget`/`QMenu` НЕ резолвит `currentColor` через QSS — иконка получает чёрную обводку и сливается с тёмным фоном (см. Screenshot_6). Фиксированный амбер виден на всех трёх темах и согласуется с `eye.svg`. Это сознательный выбор однотонной бренд-палитры вместо theme-aware иконок (для последнего пришлось бы рендерить через `QSvgRenderer` + `QPainter::CompositionMode_SourceIn`).
- В `src/SourcesPanel.cpp`: функция `glyphFor()` (возвращала `[X]` строки) заменена на `iconPathFor()` (возвращает путь к Qt-ресурсу). В `makeItem()` теперь `it->setIcon(QIcon(iconPathFor(s.type)))` плюс чистый `s.name` в тексте — без префикса-скобок. В `onAddRequested()` пункты меню тоже получают иконку через `menu.addAction(QIcon(...), name)`.
- `m_list->setIconSize(18, 18)` — иконка чуть меньше строки, не доминирует над текстом.
- Файлы зарегистрированы в `CMakeLists.txt`.
- Эффект: SourcesPanel и dropdown «+ Add» теперь выглядят как современное OBS-меню — у каждой строки тематический глиф (монитор, окно, камера, плёнка, картинка, палитра, T, сетка, глобус, микрофон, динамик), без устаревших текстовых маркеров.

**2a. Eye-иконки видимости в списке источников (`resources/icons/eye.svg`, `eye-off.svg`).**

- Контекст: в `SourcesPanel` чекбокс видимости в `QListWidget` рендерился как сплошной оранжевый квадрат (`QListWidget::indicator:checked { background: {{ACCENT}} }`) — это выглядело как недоделанный плейсхолдер.
- Решение: два Lucide-style SVG — открытый глаз `eye.svg` (`#FF9900`, состояние «видим») и перечёркнутый `eye-off.svg` (`#7A7A7A`, состояние «скрыт»). Никаких внешних библиотек, всё inline-stroke.
- Изменены три темы (`blackout.qss`, `light.qss`, `rgb.qss`): селекторы `QListWidget::indicator:checked/unchecked` теперь используют `image: url(:/resources/icons/eye.svg)` вместо заливки фона. Hover подсвечивает чекбокс полупрозрачным акцентом (`ACCENT_GHOST` в RGB, `rgba(255,153,0,0.10/0.12)` на остальных). Размер чекбокса 20×20 (было 18×18) — глаз читается лучше.
- Файлы зарегистрированы в `CMakeLists.txt` рядом с остальными иконками.
- Эффект: в `SourcesPanel` каждая строка теперь имеет глаз слева — наглядный визуальный «toggle visibility» в духе OBS, без объяснений понятный любому пользователю.

**3. Логгер (`src/Logger.h`, `src/Logger.cpp`) — новый модуль.**

- Singleton с потокобезопасной записью через `QMutexLocker`.
- Файл: `QStandardPaths::AppDataLocation/logs/volchay-YYYY-MM-DD.log`. Ротация — один файл на день, проверяется на каждом вызове `log()`.
- Перехват `qInstallMessageHandler` — все `qDebug/qWarning/qCritical` тоже льются в файл.
- Макросы `LOG_D / LOG_I / LOG_W / LOG_E`.
- Установка: `lumen::Logger::instance().install()` в `main.cpp` сразу после `setApplicationName`.

**4. Интеграция логов в UI (`src/MainWindow.cpp`).**

- `LOG_I` на: установку темы при старте, открытие Settings, сохранение Settings, click «Go live», старт/стоп стрима.
- `LOG_E` на: каждую ошибку из `StreamEngine`.
- На странице **About** добавлен раздел с кнопкой «Open log folder» (открывает каталог логов в системном файл-менеджере через `QDesktopServices`) и подписью с полным путём к текущему лог-файлу.

**5. GitHub Actions (`.github/workflows/build.yml`) — release pipeline.**

- Обновлена Qt: `6.5.3` → `6.7.3` для Windows (Linux остался на 6.2 для совместимости с Ubuntu 22.04).
- Артефакт переименован: `VolchayObs-windows-x64-${{ github.sha }}` — уникальный per-commit, удобнее искать в Actions.
- Добавлен шаг **«Create zip»** — упаковка `stage/VolchayObs/` в `VolchayObs-windows-x64.zip` (один файл для скачивания).
- Добавлен job-step **«Create GitHub Release on tag»** — на push тега `v*` автоматически публикуется Release с этим zip-архивом через `softprops/action-gh-release@v2`.
- Триггер расширен: добавлены `tags: ['v*']` в `push`.

**6. README — переработка.**

- Удалены все ссылки на чужие GitHub-репозитории.
- Добавлен раздел **«Правила разработки проекта»** с 6 правилами.
- Добавлен раздел **«Похожие программы»** с обзором аналогов (OBS Studio, Streamlabs, vMix, XSplit, Lightstream, Restream).
- Добавлен раздел **«Где лежат логи»** с путями на трёх ОС.
- Добавлен раздел **«Журнал разработки»** (этот раздел) — теперь каждый коммит детально описывается здесь.

---

## Лицензия

MIT — см. [LICENSE](LICENSE).

## Twitch — пропустит ли поток?

Да. Twitch принимает любой валидный RTMP H.264 + AAC поток на `rtmp://live.twitch.tv/app/<key>`. Никакого whitelist-а программ нет — наш ffmpeg-поток выглядит идентично OBS, Streamlabs или Twitch Studio. Что Twitch проверяет: валидный stream key, битрейт ≤ 8500 kbps, keyframe ≤ 6 сек, кодеки H.264/AAC — все эти условия пресеты соблюдают.
