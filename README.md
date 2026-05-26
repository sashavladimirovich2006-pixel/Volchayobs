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

### 2026-05-27 — Чистка превью: оранжевые рамки cards (RGB) + лишний caption (Screenshot_12/13)

**1. Оранжевая обводка cards на RGB-теме (Screenshot_13).**

Контекст: на RGB-теме все `QFrame[role="card"]` (превью, источники, mixer, log, header) имели `border: 1px solid {{ACCENT}}` — постоянная толстая янтарная линия по периметру каждой карточки. На фоне анимированного RGB-акцента это создавало «зебру» из подсвеченных прямоугольников по всему экрану, особенно заметную на вертикальных боках превью.

Решение: `resources/themes/rgb.qss` — для `QFrame[role="card"]` заменено `border: 1px solid {{ACCENT}}` → `border: 1px solid #222222`. Это синхронизирует RGB с Blackout-темой (где такой же `#222222`) — карточки теперь имеют едва заметный технический контур вместо бренд-обводки. Сам акцент по-прежнему живёт в активных state'ах (selected nav, primary-кнопки, focus-ring), поэтому RGB не теряет «живой» цвет.

**2. Лишний hint-caption «Click a source on the canvas or in the sources list to edit» (Screenshot_12).**

Контекст: пока есть хотя бы один источник, но ни один не выбран — внизу превью выводился подсказывающий текст «Click a source on the canvas or in the sources list to edit». После того как мы научились гасить selection при клике вне превью (см. запись выше), эта надпись стала появляться при каждом deselect и визуально шуметь — превью просит юзера снова кликнуть туда же, куда он только что не хотел кликать.

Решение: `src/PreviewWidget.cpp` — ветка `else if (anyEnabledVideo())` удалена; теперь caption остаётся пустым, когда есть источники но ни один не выбран. Caption всё ещё рисуется в трёх осмысленных случаях: `ON AIR` во время стрима, `Selected: %1 · drag corners ...` когда есть active source, и `Add a source from the «Sources» panel` когда сцена пустая. Чтобы `m_summary` («1920×1080 @ 60 fps · 6000 kbps») сохранил свою позицию относительно нижнего края превью, `captionRect` теперь объявлен на одном уровне с веткой отрисовки — отрисовывается только при непустом caption, но геометрия для summary считается всегда.

### 2026-05-27 — Deselect handles при клике вне превью (Screenshot_11)

Контекст: на Screenshot_11 видно, что 8 оранжевых «кубиков» масштабирования (resize-handles) активного источника продолжают висеть на превью, даже когда юзер уже работает с другой панелью (audio mixer, sources list, top-bar). PreviewWidget сам умеет гасить selection при клике на пустой холст внутри своих границ (`mousePressEvent` lines 807-817), но снаружи превью клики на него никак не действуют — handles остаются как «зависший» indicator, шум на сцене.

Ожидание: клик в любую другую область программы → handles исчезают; клик обратно на источник в превью → handles появляются (это уже работало через `PreviewWidget::sourceSelected` сигнал, ничего трогать не надо).

**Что сделано:**

- `src/MainWindow.h`: добавлен override `bool eventFilter(QObject* obj, QEvent* ev)` в `protected:` секцию.
- `src/MainWindow.cpp`:
  - Подключены `<QApplication>` и `<QEvent>`.
  - В конструкторе после `refreshStreamSummary()` добавлено `qApp->installEventFilter(this)` — глобальный watcher на все события приложения.
  - Реализован `MainWindow::eventFilter`: на `MouseButtonPress` проверяется, что target widget принадлежит этому окну (`w->window() == this`, отсекает SettingsDialog и комбобокс-попапы), и что он НЕ является `m_preview` или его потомком (BrowserLayer для Browser-источников) И НЕ принадлежит `m_sourcesPanel`. Если оба условия true — гасим selection: `m_preview->setActiveSourceId("")` + `m_sourcesPanel->setSelectedSourceId("")` для синхронизации списка справа.
  - Возвращаем `QMainWindow::eventFilter(obj, ev)` чтобы не глотать событие — клик нормально доходит до целевого виджета (mixer, log и т.д.).

**Почему именно eventFilter, а не override `mousePressEvent` на MainWindow:** `QMainWindow::mousePressEvent` вызывается только когда клик пришёлся на пустое место самого `central widget`, не пробрасывая клики от дочерних виджетов. Чтобы поймать клик «куда угодно в окне», нужен либо `installEventFilter(qApp)`, либо subclass'инг каждого ребёнка — eventFilter короче и сосредоточен в одном месте.

**Почему `m_sourcesPanel` исключён из триггера:** клик на строку источника в правой панели меняет активный источник через сигнал `selectionChanged`. Если бы eventFilter сначала чистил selection, а потом панель его восстанавливала — был бы лишний раунд событий и потенциальный flicker. Проще не трогать selection когда клик попал внутрь `SourcesPanel`.

**Эффект:** теперь UX как в OBS Studio / Photoshop — handles живут только пока фокус на сцене. Юзер двигает фейдер микшера или скроллит лог — превью моментально успокаивается, не отвлекая визуальным шумом.

### 2026-05-27 — Sidebar nav: «парящий dynamic island» вместо edge-to-edge полоски

Контекст: на Screenshot_10 активный пункт «Stream» был визуально привязан к самому краю сайдбара через `border-left: 4px solid ACCENT` + горизонтальный градиент — классический «active rail» из 2015-х. Хотелось перевести индикацию активной вкладки в современную dynamic-island эстетику: компактный закруглённый pill, отделённый от краёв воздухом, с акцентной обводкой по всему периметру вместо одной полоски слева.

**Что сделано (`resources/themes/blackout.qss`, `light.qss`, `rgb.qss`):**

- `QPushButton[role="nav"]` теперь имеет `margin: 3px 10px` — благодаря этому пункт визуально отделяется от стенок сайдбара (220px wide → пункт ≈200px) и читается как «парящий» элемент.
- `border-radius: 14px` на всех состояниях — углы скруглены по-крупному, как у iOS dynamic island и современных pill-индикаторов.
- Базовый `border: 1.5px solid transparent` вместо прежнего `border: none` + `border-left: 4px solid transparent` — box model теперь стабилен между состояниями (никаких сдвигов текста на 4px при переключении).
- Hover: убран горизонтальный fade-out градиент, заменён на ровную заливку `{{ACCENT_GHOST}}` (акцент @18% alpha) — на закруглённом pill ровный фон выглядит чище, чем градиент в один край.
- Selected: добавлена обводка по периметру `border: 1.5px solid {{ACCENT}}`, фон стал вертикальным градиентом `{{ACCENT_GHOST}} → transparent` (top→bottom вместо left→right) — добавляет лёгкий «glass»-эффект сверху. Жирность поднята до 700.
- Focus: тоже `border: 1.5px solid {{ACCENT_SOFT}}` по всему контуру — консистентность с selected.
- Padding сжат `13px 18px → 10px 16px` чтобы скомпенсировать визуальное добавление margin и не раздувать высоту строки.

**Эффект:** на всех трёх темах активный «Stream» теперь выглядит как обособленный закруглённый pill с янтарной обводкой внутри сайдбара, а не как «вкладка приклеенная к краю». Hover-состояние тоже стало pill — наводка показывает где будет следующий выбор. На RGB-теме обводка плавно меняет оттенок вместе с анимированным акцентом — никаких дополнительных правок не требовалось, всё через `{{ACCENT}}` placeholder.

### 2026-05-27 — Замена кружка-индикатора на логотип в сайдбаре

Контекст: на Screenshot_9 видно, что слева от шапки «VOLCHAY / OBS» висел маленький оранжевый кружок (`AccentBadge`, 16×16) — он не нёс смысла, дублировал акцент темы и выглядел как случайное пятно. Логичнее ставить туда фактическую иконку приложения (`resources/icons/logo.svg`), та же, что используется в `setWindowIcon()`.

**Что сделано:**

- `src/MainWindow.cpp`: убран `m_badge = new AccentBadge(m_theme)`. Вместо него — локальный `QLabel brandIcon` 36×36 с `setPixmap(QIcon(":/resources/icons/logo.svg").pixmap(36, 36))`. Размер подобран так, чтобы лого занимал высоту обоих строк «VOLCHAY/OBS» и читался как полноценный знак, а не как точка-индикатор.
- `src/MainWindow.h`: удалены forward-declaration `class AccentBadge;` и поле `AccentBadge* m_badge` — теперь это локальная переменная, член класса не нужен (нигде больше не используется).
- `src/AccentBadge.{h,cpp}` — удалены, класс полностью неиспользуемый.
- `CMakeLists.txt`: убраны `src/AccentBadge.cpp/.h` из `VOLCHAY_SOURCES`.
- `src/ThemeManager.cpp`: подправлен комментарий — убрана ссылка на удалённый AccentBadge.
- `README.md`: в архитектурной схеме удалена строка про `AccentBadge.{h,cpp}`.

**Эффект:** в шапке сайдбара теперь видно нормальный broadcast-логотип (тёмный квадрат с радио-волнами и красной LIVE-точкой) — тот же знак, что в иконке окна и `logo.ico`. Идентичность брэнда стала консистентной по всему приложению.

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

**2d. Sidebar nav-иконки `stream/settings/info/log.svg` — амбер вместо `currentColor`.**

- Контекст: на Blackout пункты Settings и About показывали почти невидимые иконки (Screenshot_8). Та же причина что у source-иконок в 2b → `QIcon` при рендере SVG в `QPushButton[role="nav"]` не наследует `currentColor` через QSS. Активный пункт (Stream) случайно отрендерился оранжевым только потому, что Qt пересчитал иконку после смены checked-state и подхватил `color: {{ACCENT}}` из активного селектора.
- Решение: захардкодить `stroke="#FF9900"` во всех четырёх sidebar/log иконках для гарантированной видимости. У nav-кнопок активность всё равно отчётливо показывается через `border-left: 4px solid ACCENT` и оранжевый текст — единого амбера у иконки достаточно.
- `play.svg`/`stop.svg` намеренно оставил с `currentColor`: они на primary-кнопке `Go live`/`Stop` с ярким оранжевым фоном, где `currentColor` фолбэкается в `#0A0A0A` (color из QSS для primary), что даёт читаемый тёмный треугольник на оранжевом — никакого фикса не требует.

**2c. Фикс «белых полосок» в audio mixer (`AudioMixerPanel.cpp`, `AudioLevelMeter.cpp`).**

- Контекст: на Blackout и RGB темах слайдер громкости показывал синюю заливку (Windows-системный `palette(highlight)`) + светло-серую остальную часть трэка (`palette(mid)`), которая выглядела как яркая «белая полоска». Параллельно у VU-метра рейл рендерился через `palette(WindowText)` с alpha 70 — на тёмной теме это даёт почти-белый цвет (Screenshot_7).
- Решение: вместо платформенной палитры — фиксированные значения, согласованные с бренд-стилем:
  - QSlider: `sub-page` и `handle` → `#FF9900` (амбер), `groove`/`add-page` → `rgba(128,128,128,60)` — нейтральный приглушённый серый, читается на любой теме.
  - AudioLevelMeter: track-цвет → `rgba(128,128,128,60)` (тот же серый, единый язык с слайдером). Peak-маркер → `#FF9900` (был белый из palette, теперь акцент — видно на любой части gradient'а).
- Эффект: на Blackout слайдер и метр выглядят как тонкие приглушённые серые рейлы с яркой амбер-заливкой — никаких больше «белых полосок». На Light тёмно-серый rgba даёт спокойный нейтральный фон.

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
