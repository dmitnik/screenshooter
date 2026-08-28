# План реализации Screenshooter

**Статус:** Portable 0.1.0 implemented; release limitations documented

**Дата утверждения:** 2026-08-28

**Название продукта:** Screenshooter (`screenshooter.exe`)

## Результат выполнения

Инкременты 0–7 реализованы локальными коммитами и проверены Debug/Release
сборками. Portable artifact находится в `packages/` рабочего дерева; фактические
метрики и инструкция запуска опубликованы в `README.md`, протоколы — в
`tests/manual/`.

После утверждения плана пользователь дополнительно запросил выбор папки сохранения
по умолчанию. Он реализован через системный folder picker и значение `SaveFolder`
в `HKCU\Software\Screenshooter`.

Ограничения, которые нельзя закрыть одной локальной Windows 11 машиной или без
внешних полномочий, оставлены явно: Windows 10/mixed-DPI/HDR test matrix, реальные
Share targets и принтеры, code-signing certificate и Defender submission. Window
capture в portable 0.1.0 использует DWM bounds с visible-region fallback.

## 1. Назначение документа

Этот документ переводит продуктовые требования из
[`PLAN_WINDOWS_SCREENSHOT_APP.md`](./PLAN_WINDOWS_SCREENSHOT_APP.md) в порядок
инженерной реализации. Исходное ТЗ остаётся главным источником требований к
поведению приложения; этот план определяет архитектуру, зависимости работ,
контракты модулей, проверки и критерии завершения.

План рассчитан на разработку с нуля: на момент его составления в репозитории нет
исходного кода приложения, сборочной системы и тестов.

## 2. Зафиксированные решения

### 2.1. Платформа и сборка

- целевая платформа первой версии: Windows 10 версии 1903 (build 18362) и новее,
  Windows 11, только x64;
- язык: C++20 без исключений из выбранного в ТЗ стека;
- сборка: CMake и генератор Visual Studio 2022;
- Windows SDK: актуальная установленная версия, но код компилируется с
  `_WIN32_WINNT=0x0A00` и проверяет наличие более новых API во время выполнения;
- приложение: один GUI-процесс и один EXE, без службы Windows и без фонового
  вспомогательного процесса;
- один процесс может одновременно держать несколько независимых окон редактора;
  каждый завершённый захват создаёт новое окно со своей сессией документа;
- конфигурация Release использует статически подключённую C/C++ runtime
  (`/MT`), чтобы пользователю не требовалась отдельная установка VC Runtime;
- основной дистрибутив сначала создаётся как unpackaged portable EXE. Выбор между
  MSIX и классическим установщиком принимается только после проверки Windows
  Share, автозапуска, подписи и обновления поверх предыдущей версии.

Минимальная версия Windows 10 1903 нужна для захвата отдельного окна через
`IGraphicsCaptureItemInterop::CreateForWindow`. Захват мониторов через Desktop
Duplication доступен на более старых системах, но поддерживать две разные нижние
границы первой версии нецелесообразно.

### 2.2. Графический стек

- Direct3D 11 владеет GPU-устройством и текстурами захвата;
- DXGI Desktop Duplication захватывает мониторы для сценариев «область»,
  «активный монитор» и «все мониторы»;
- Windows.Graphics.Capture захватывает выбранное окно без содержимого
  перекрывающих его окон, когда API и окно это позволяют;
- GDI `BitBlt` используется только как резервный путь для захвата видимого
  рабочего стола при ожидаемых отказах DXGI;
- Direct2D 1.1 рисует canvas, векторные объекты и эффекты;
- DirectWrite выполняет компоновку и отрисовку текста;
- WIC декодирует входные изображения и кодирует PNG/JPEG;
- C++/WinRT используется только как заголовочный мост к системным WinRT API для
  захвата окна и Windows Share. Windows App SDK и WinUI не добавляются.

Desktop Duplication создаётся отдельно для каждого активного output. Кадры
приводятся к единой системе физических координат виртуального рабочего стола,
включая отрицательные координаты и поворот монитора. Для одноразового снимка не
нужно постоянно держать поток кадров в трее: ресурсы захвата создаются лениво и
освобождаются после получения согласованного кадра.

### 2.3. Разрешение противоречий исходного ТЗ

- инструмент скрытия данных реализуется как Gaussian Blur; этап 4 исходного ТЗ
  приведён к этой согласованной формулировке;
- размытие не считается безопасным редактированием. В справке приложения нужно
  явно указать, что для гарантированного сокрытия данных следует вырезать область
  или не публиковать изображение;
- окно скриншотера скрывается до снимка. Начиная с Windows 10 2004 его top-level
  окна дополнительно помечаются `WDA_EXCLUDEFROMCAPTURE`; на 1903 это не является
  гарантией, поэтому порядок «скрыть → дождаться композиции → захватить» остаётся
  обязательным;
- резервный захват выбранного окна возвращает фактически видимую область из
  frozen desktop. `PrintWindow` не является обязательным путём, потому что его
  результат зависит от реализации чужого окна;
- каждый завершённый захват создаёт отдельные `EditorWindow` и `DocumentSession`;
  ранее открытые редакторы не закрываются и не заменяются;
- повторный хоткей разрешён при открытых редакторах, но пока активен capture или
  selection overlay, второй захват не запускается;
- кнопка `Копировать` и эквивалентная команда `Ctrl+C`, когда объект не выбран,
  копируют итоговое изображение и после успешной записи в clipboard закрывают
  только текущее окно редактора. При ошибке clipboard окно и документ остаются
  открытыми;
- обычное закрытие изменённого окна без Copy или Save показывает варианты
  «Сохранить», «Закрыть без сохранения», «Отмена».

### 2.4. Потоки и состояния

Главный поток работает в STA и владеет Win32 message loop, tray icon, всеми
окнами и объектами Direct2D, связанными с UI. Захват и экспорт выполняются в
отдельных MTA-задачах. Результат возвращается в UI через `PostMessage`; фоновые
задачи не обращаются к `HWND` и объектам редактора напрямую.

Глобальный `CaptureCoordinator` имеет состояния:

`Idle → Capturing → Selecting → Idle`.

После успешного выбора он передаёт кадр в новую `DocumentSession`, регистрирует
новое окно и сразу возвращается в `Idle`. Поэтому существующие редакторы не
блокируют следующий хоткей. Одновременно активен не более чем один overlay.

Каждая `DocumentSession` имеет собственное состояние:

`Editing ↔ Exporting → Closed`.

Отмена захвата возвращает только `CaptureCoordinator` в `Idle`. Закрытие,
сохранение или копирование одного документа не влияет на другие окна. Процесс
остаётся в трее после закрытия последнего редактора и завершается только командой
«Выход» или завершением пользовательской сессии Windows.

## 3. Структура репозитория

```text
/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE                         # после выбора лицензии
├── cmake/
│   ├── CompilerWarnings.cmake
│   └── Sanitizers.cmake            # только поддерживаемые dev-проверки
├── resources/
│   ├── app.manifest
│   ├── app.rc
│   ├── resource.h
│   ├── icons/
│   └── strings.ru-RU.rc
├── src/
│   ├── app/                        # WinMain, lifecycle, state machine
│   ├── platform/                   # COM, Win32, DPI, errors, RAII handles
│   ├── tray/                       # tray icon, menu, hotkeys
│   ├── capture/                    # DXGI, WGC, GDI, monitor topology
│   ├── overlay/                    # area/window selection
│   ├── document/                   # pure model, annotations, commands
│   ├── editor/                     # sessions, windows, tools, input, viewport
│   ├── render/                     # D2D renderer and offscreen renderer
│   ├── export/                     # WIC, clipboard, file, share, print
│   ├── settings/                   # schema, registry storage, migration
│   └── diagnostics/                # timings and debug-only logging
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── fixtures/
│   └── manual/
└── packaging/
    ├── msix/
    └── installer/
```

В `src/document` не допускаются зависимости от `HWND`, Direct2D и COM. Это
позволяет тестировать геометрию, hit testing, команды и Undo/Redo обычными
детерминированными unit-тестами.

## 4. Контракты основных модулей

### 4.1. Базовые типы

- `PixelPoint`, `PixelSize`, `PixelRect` — целочисленные физические координаты;
- `DocumentPoint`, `DocumentRect` — `float`-координаты исходного изображения;
- `DipPoint`, `DipSize` — координаты интерфейса в DIPs;
- `MonitorInfo` — `HMONITOR`, physical bounds, work area, rotation, DPI, adapter
  и output index;
- `PixelBuffer` — BGRA8 premultiplied, размер, stride и явное владение памятью;
- `Result<T>`/`AppError` — категория, `HRESULT`/Win32 code и безопасное сообщение
  для пользователя.

Преобразования между системами координат находятся только в `platform/dpi` и
`editor/viewport`. Неявное смешивание `RECT`, DIPs и document pixels запрещено.

### 4.2. CaptureEngine

```cpp
struct CaptureRequest {
    CaptureMode mode;
    std::optional<HWND> window;
    std::optional<HMONITOR> monitor;
    bool includeCursor;
};

struct CaptureFrame {
    PixelBuffer pixels;
    PixelRect virtualDesktopBounds;
    std::vector<MonitorSlice> monitors;
    CaptureBackend backend;
    CaptureWarnings warnings;
};

class ICaptureBackend {
public:
    virtual Result<CaptureFrame> Capture(const CaptureRequest&) = 0;
    virtual ~ICaptureBackend() = default;
};
```

`CaptureEngine` выбирает backend по capability check, а не по типу исключения.
Ожидаемые ошибки `DXGI_ERROR_ACCESS_LOST`, смена режима, отключение монитора и
лимит Desktop Duplication приводят к одному пересозданию ресурсов либо к GDI
fallback. Неожиданные ошибки возвращаются пользователю и не зацикливаются.

### 4.3. SelectionOverlay

Overlay состоит из одного borderless topmost окна на каждый монитор и общего
`SelectionController`. Каждое окно рисует свой slice frozen frame, а controller
хранит единственный прямоугольник в физических координатах виртуального рабочего
стола. Это позволяет протянуть выделение через границы мониторов с разным DPI.

Для выбора окна отдельный `WindowHitTester`:

- получает кандидата через `WindowFromPoint` и поднимается к root owner;
- фильтрует невидимые, cloaked, tool/system и собственные окна;
- получает extended frame bounds через DWM;
- подсвечивает границы на frozen frame;
- после клика передаёт `HWND` в Windows.Graphics.Capture, а при отказе вырезает
  видимый прямоугольник из frozen frame и возвращает предупреждение.

### 4.4. DocumentModel и история

Каждая `DocumentSession` владеет ровно одним `Document`, своим `EditorWindow`,
viewport, selection/tool state и историей команд. Реестр сессий в `app/` хранит
их по стабильному `SessionId` и удаляет сессию после уничтожения соответствующего
окна.

`Document` содержит неизменяемое исходное изображение, crop rectangle,
упорядоченный список `Annotation` и метаданные исходного файла. `Annotation` —
`std::variant` из `Arrow`, `Text`, `Shape`, `MarkerStroke`, `BlurRegion` и
`ImageObject`. Каждый тип реализует bounds, hit test и управляющие точки через
отдельные чистые функции, а не виртуальную иерархию с UI-состоянием.

Изменения выполняются командами:

- `AddAnnotationCommand`;
- `DeleteAnnotationCommand`;
- `TransformAnnotationCommand`;
- `ChangeStyleCommand`;
- `EditTextCommand`;
- `SetCropCommand`;
- `PasteImageCommand`.

Команда создаётся при начале жеста, обновляет preview во время движения и один
раз попадает в историю при завершении. История хранит не больше 100 команд и
64 МБ оценочного payload; превышение любого лимита удаляет самые старые команды.

### 4.5. Renderer

Один `SceneRenderer` используется для экранного preview и полного экспорта.
Различается только target и матрица `Document → Target`. Порядок рисования:

1. очистка target;
2. исходное изображение;
3. растровые вставки и blur regions в порядке слоёв;
4. остальные annotation objects в порядке слоёв;
5. clip по crop rectangle;
6. только для UI: selection bounds, handles, crop dimming и guides.

Blur строится через `CLSID_D2D1GaussianBlur` над объединённым нижележащим
растровым слоем и композитится внутри прямоугольной маски. UI overlays никогда не
входят в экспорт. Device loss (`D2DERR_RECREATE_TARGET`, DXGI device removed)
обрабатывается централизованным пересозданием device-dependent ресурсов; модель
документа остаётся живой.

### 4.6. EditorWindow и инструменты

Каждая `EditorWindow` привязана к своей `DocumentSession` и маршрутизирует Win32
input только в её активный `IToolController`. Инструмент может менять только
draft state; подтверждённое изменение выполняется командой DocumentModel.
Нейтральный `SelectTool` отвечает за hit testing, перемещение, resize handles и
клавиатурные команды.

Viewport хранит zoom и pan независимо от документа. Все pointer coordinates
преобразуются `client px → DIP → document px` один раз на входе. Zoom ограничен
диапазоном 10–800%, выполняется относительно курсора и не влияет на экспорт.

Для ввода текста создаётся дочерний Win32 edit control поверх canvas. Его bounds
синхронизируются с viewport; после подтверждения текст и layout parameters
переходят в `Text` annotation, а native control уничтожается.

Нижняя панель повторяет подтверждённый сценарий референса: справа расположены
`Копировать`, `Сохранить`, `Поделиться`. Успешный `Копировать` закрывает только
текущую `EditorWindow`; `Сохранить` и `Поделиться` сами по себе окно не закрывают,
если отдельная настройка сохранения не задаёт другое поведение.

### 4.7. ExportService

Полноразмерный кадр формируется тем же `SceneRenderer` в offscreen D3D11 texture.
После завершения GPU-копирования staging texture преобразуется в WIC bitmap.

- PNG: lossless BGRA/RGBA через WIC;
- JPEG: фон предварительно сводится с непрозрачным белым цветом, качество по
  умолчанию 0.92;
- Clipboard: сначала кодируется PNG, затем атомарно публикуются `CF_DIBV5` и
  зарегистрированный формат `PNG`; при ошибке clipboard закрывается и память не
  передаётся системе;
- Save: `IFileSaveDialog`, запись во временный файл в выбранной папке и затем
  атомарная замена целевого файла, где файловая система это поддерживает;
- Share: временный PNG в `%LocalAppData%\Screenshooter\Share`,
  `DataTransferManager` получается через `IDataTransferManagerInterop` для HWND,
  а файл передаётся как `StorageItems`; очистка удаляет файлы старше 24 часов при
  старте и после завершения операции;
- Print: `PrintDlgExW`, рендер в bitmap требуемого разрешения и вывод в printer DC
  с сохранением пропорций.

Экспорт сериализуется на одном общем worker: одновременные Copy/Save/Share из
разных окон не должны создавать несколько полноразмерных рендеров и пик памяти.
Задача экспорта получает immutable snapshot документа и `SessionId`; успешный
Copy отправляет в UI команду закрытия только соответствующей сессии. При ошибке
сессия возвращается в `Editing`.

### 4.8. SettingsStore

Первая версия хранит настройки в
`HKCU\Software\Screenshooter`, поскольку это не требует JSON-зависимости и
работает в portable-сценарии. Каждая запись имеет значение по умолчанию, проверку
диапазона и номер версии схемы. Повреждённое значение сбрасывается отдельно, без
сброса остальных настроек.

Автозапуск unpackaged-версии реализуется только по явному действию пользователя
через `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. Для MSIX этот путь
заменяется поддерживаемым механизмом пакета.

## 5. План поставки по инкрементам

Каждый инкремент должен завершаться рабочей сборкой, проверенным diff и отдельным
локальным commit. Следующий инкремент не начинается, пока не выполнены его exit
criteria.

### Инкремент 0. Каркас и проверка рисков

Работы:

1. Создать CMake targets `screenshooter`, `unit_tests` и `integration_tests`.
2. Добавить manifest Per-Monitor V2, Common Controls v6 и resources.
3. Настроить единый набор warning flags (`/W4`, `/permissive-`, `/Zc:__cplusplus`)
   и запретить новые warnings в CI.
4. Добавить RAII wrappers для Win32 handles и COM initialization.
5. Сделать четыре удаляемых spike-прототипа:
   - один кадр Desktop Duplication с двух outputs;
   - `CreateForWindow` для обычного HWND;
   - `IDataTransferManagerInterop::ShowShareUIForWindow` из чистого Win32 HWND;
   - Gaussian Blur в offscreen D3D11 texture с последующим WIC PNG.
6. Зафиксировать измеренные ограничения и удалить spike UI после переноса
   минимального рабочего кода в модули.

Exit criteria:

- Debug и Release x64 собираются с нуля через CMake preset;
- приложение показывает пустое DPI-aware окно и корректно переживает перенос
  между мониторами;
- все четыре spike подтверждены на поддерживаемой Windows 10/11 либо для
  неподтверждённого сценария выбран и описан fallback;
- smoke test запускается через CTest.

### Инкремент 1. Первый сквозной сценарий

Работы:

1. Реализовать TrayHost, восстановление icon после `TaskbarCreated` и один хоткей
   «Снимок области».
2. Реализовать топологию мониторов, DXGI capture и GDI fallback.
3. Реализовать multi-window frozen overlay, выделение области, размеры и отмену.
4. Открывать для каждого завершённого захвата новую `DocumentSession` и
   минимальную `EditorWindow` с pan/zoom и исходным bitmap.
5. Реализовать offscreen render и Copy в clipboard с закрытием текущего окна
   только после успешного копирования.
6. Ввести `CaptureCoordinator`, реестр сессий и блокировку только параллельного
   захвата, а не новых захватов при открытых редакторах.

Exit criteria:

- путь `hotkey → selection → editor → Ctrl+C → close current editor` работает
  без временного файла;
- три последовательных захвата создают три независимых окна, а закрытие или Copy
  в одном из них не меняет два остальных;
- область совпадает с физическими пикселями на конфигурации 100% + 150%, включая
  монитор слева от основного;
- `Esc` не оставляет overlay windows и не меняет clipboard;
- процесс в Idle не опрашивает состояние таймером и не удерживает capture device.

### Инкремент 2. Модель документа и базовое редактирование

Работы:

1. Реализовать DocumentModel, annotation IDs, crop bounds и dirty state.
2. Реализовать command history с объединением drag в одну операцию.
3. Добавить SelectTool, hit testing, handles, move/resize/delete и keyboard nudge.
4. Добавить прямоугольник и прямую линию как первые end-to-end annotations.
5. Обеспечить одинаковый render preview/export.
6. Добавить close confirmation отдельно для каждой сессии; новый хоткей не
   проверяет dirty state уже открытых окон.

Exit criteria:

- unit-тесты покрывают геометрию, z-order, hit testing и границы истории;
- 100 последовательных drag events создают одну undo command;
- undo/redo восстанавливает точное состояние и dirty flag;
- PNG после экспорта визуально совпадает с canvas при zoom 50%, 100% и 200%.

### Инкремент 3. Основные инструменты

Порядок реализации снижает зависимость между сложными инструментами:

1. ellipse и star на общей shape infrastructure;
2. straight filled/outlined arrow;
3. curved dashed arrow с control point;
4. marker stroke со сглаживанием и alpha compositing;
5. text object, native edit overlay, DirectWrite wrapping и три preset styles;
6. image paste из bitmap, file drop и scale handles;
7. object copy/cut/paste и выбор clipboard behavior по текущему контексту;
8. tool/context toolbar, palette, thickness и режим единичного/серийного создания.

Exit criteria:

- каждый объект создаётся, выбирается, перемещается, масштабируется и удаляется;
- русский, Unicode, перенос строк и отмена редактирования текста проверены;
- marker overlap усиливает цвет, но отдельный stroke остаётся одним объектом;
- экспорт 100 объектов на 4K не меняет порядок слоёв и не блокирует UI на время
  кодирования.

### Инкремент 4. Blur и crop

Работы:

1. Реализовать сборку нижележащего raster input для blur region.
2. Добавить Gaussian Blur preview и export с одинаковыми параметрами.
3. Реализовать crop draft, handles, dimming, Enter/Esc и `SetCropCommand`.
4. Проверить clip частично видимых annotations и сохранение объектов вне crop.
5. Добавить предупреждение о том, что blur не гарантирует необратимое сокрытие.

Exit criteria:

- blur не размывает более высокие векторные слои;
- crop undo возвращает прежнюю область и скрытые объекты;
- pixel comparison preview/export проходит с заданным допуском;
- при device loss документ восстанавливается без потери модели.

### Инкремент 5. Файлы и системный вывод

Работы:

1. Save/Save As для PNG/JPEG и безопасная обработка перезаписи.
2. Open и drag-and-drop PNG/JPEG/BMP; WebP показывается только при наличии WIC
   decoder capability.
3. Windows Share с lifecycle временного файла и понятным empty-target состоянием.
4. Печать, ориентация, fit-to-page и отсутствие принтера.
5. Уведомления Copy/Save, закрытие только успешно скопированной сессии и обработка
   ошибок clipboard/диска/доступа.

Exit criteria:

- round-trip PNG сохраняет размер и ожидаемый alpha;
- JPEG всегда непрозрачен и использует выбранное качество;
- отмена системного диалога ничего не создаёт и не меняет dirty state;
- временные share-файлы очищаются после сбоя и следующего запуска;
- системные вызовы не выполняются из background thread без требуемой apartment
  initialization.

### Инкремент 6. Остальные режимы захвата и настройки

Работы:

1. Захват окна: overlay hit testing, WGC backend и frozen-frame fallback.
2. Захват монитора под курсором и всего virtual desktop.
3. Полный набор переназначаемых hotkeys и атомарная перерегистрация.
4. Settings UI, сохранение schema и восстановление defaults.
5. Автозапуск, open-file command line и «Открыть папку сохранения».
6. Согласовать поведение `PrintScreen` с системным Snipping Tool и сообщать
   конфликт без молчаливого изменения настройки.

Exit criteria:

- каждый режим доступен из tray menu и hotkey;
- конфликт hotkey виден пользователю, старое рабочее сочетание не теряется при
  неудачной перерегистрации;
- собственные окна не появляются в результате захвата;
- settings переживают обновление schema и повреждение одного значения.

### Инкремент 7. Производительность, стабильность и выпуск

Работы:

1. Добавить `QueryPerformanceCounter`-метрики для capture, overlay first paint,
   editor first paint, render и encode; метрики остаются локальными.
2. Проверить private working set, CPU idle, GDI/USER handles, COM и GPU leaks.
3. Прогнать матрицу DPI, multi-monitor, rotation, HDR/SDR и negative coordinates.
4. Обработать Explorer restart, session lock/unlock, display change, device loss,
   shutdown и два быстрых hotkey events.
5. Проверить accessibility names, keyboard-only flow и High Contrast.
6. Выбрать упаковку, подготовить signing pipeline и Release artifacts.
7. Зафиксировать фактические показатели рядом с целями исходного ТЗ.

Exit criteria:

- пройдена обязательная матрица тестов из исходного ТЗ;
- нет роста handles/working set после 100 циклов capture/open/close;
- Release соответствует согласованным performance budgets либо отклонения
  измерены и явно приняты;
- пакет устанавливается, обновляется и удаляется без потери пользовательских
  файлов;
- symbols и диагностические артефакты не входят в пользовательский пакет.

## 6. Стратегия тестирования

### 6.1. Автоматические unit-тесты

- coordinate transforms при разных DPI и отрицательных origins;
- rectangle normalization, clipping и cross-monitor selection;
- hit testing для всех annotations и handles;
- shape/arrow geometry и Shift constraints;
- marker simplification/smoothing;
- text bounds и Unicode state transitions;
- command apply/revert, merge и memory limit;
- settings defaults, validation и schema migration;
- filename template и очистка share-файлов по возрасту.

### 6.2. Интеграционные тесты

- WIC decode/encode на fixtures и сравнение размеров/pixels;
- offscreen Direct2D render в WARP-режиме для воспроизводимости;
- clipboard round-trip в отдельном test process;
- реестр нескольких `DocumentSession`, независимое закрытие и маршрутизация
  завершения экспорта по `SessionId`;
- capture capability checks и контролируемый GDI fallback;
- регистрация/освобождение тестового hotkey;
- создание/пересоздание graphics resources после искусственного device loss.

GPU-, shell-, printer- и multi-monitor-сценарии нельзя считать надёжными в
headless CI. Они выполняются на выделенной Windows-машине и документируются в
`tests/manual/` короткими сценариями с ожидаемым результатом и местом для
фактических измерений.

### 6.3. Проверка производительности

Каждый бюджет измеряется отдельно после холодного и тёплого запуска. Для каждого
сценария сохраняются median и p95 не менее чем по 30 прогонам:

- hotkey → frozen overlay first paint;
- mouse release → editor first paint;
- Copy click → clipboard ready для 1080p и 4K;
- Save click → PNG/JPEG ready;
- Idle private working set и CPU за 5 минут;
- пик памяти при 4K screenshot и 100 annotations.

## 7. Риски и точки раннего решения

| Риск | Последствие | Проверка/снижение |
|---|---|---|
| Windows Share ведёт себя по-разному в unpackaged/MSIX | блокировка кнопки «Поделиться» или упаковки | spike в инкременте 0 на Windows 10 и 11; fallback — открыть системный выбор приложения/папку файла с ясным сообщением |
| Несинхронные кадры нескольких outputs | граница изображения содержит разрыв во времени | параллельный acquire с timestamp; допустимость фиксируется тестом, строгая синхронизация API не обещается |
| DXGI access lost/device removed | чёрный снимок или зависание | одно пересоздание, затем GDI fallback, без бесконечных retry |
| HDR → BGRA8 даёт washed-out цвета | неверный экспорт | spike HDR tone mapping; первая версия экспортирует SDR и документирует ограничение, если корректный путь не укладывается в бюджет |
| WGC не захватывает конкретное окно | нарушено ожидание «без перекрытий» | capability check и frozen visible-region fallback с уведомлением |
| Gaussian Blur оставляет данные распознаваемыми | ложное чувство безопасности | явное предупреждение и тест читаемости; отдельный redaction tool только после изменения scope |
| Full-resolution blur и много объектов дают пик памяти | выход за целевые 20 МБ/общую отзывчивость | 20 МБ применяется только к Idle; export buffer создаётся один раз, операции сериализуются |
| Несколько открытых 4K-документов увеличивают память | высокое потребление при привычном многократном захвате | каждый документ владеет одним исходным bitmap; закрытие окна немедленно освобождает его ресурсы; общий export worker ограничивает дополнительные буферы |
| `PrintScreen` занят системой | hotkey не регистрируется | показать конфликт, не заменять сочетание молча |
| Требование WebP зависит от WIC codec | формат не открывается на части машин | runtime capability check и условное отображение поддержки |

## 8. Контроль объёма работ

До завершения первой версии не добавляются OCR, запись видео, scrolling capture,
облако, история снимков, автообновление, панель слоёв, rotation и плагины.
Архитектура не должна заранее вводить абстракции для этих функций.

Любое новое требование оценивается по трём вопросам:

1. Меняет ли оно DocumentModel или порядок растеризации?
2. Добавляет ли оно новый системный permission/runtime/процесс?
3. Нарушает ли оно цели Idle memory, startup time или отсутствие сети?

Если хотя бы один ответ положительный, изменение оформляется как отдельное
решение и не включается незаметно в текущий инкремент.

## 9. Утверждённые продуктовые решения

Решения подтверждены пользователем 2026-08-28:

1. минимальная ОС — Windows 10 1903, x64;
2. display name — `Screenshooter`, имя файла — `screenshooter.exe`;
3. каждый захват создаёт отдельное окно и независимую `DocumentSession` в одном
   процессе; количество открытых редакторов не ограничивается искусственно;
4. инструмент размытия — Gaussian Blur без гарантии безопасного редактирования;
5. успешная команда `Копировать` помещает готовое изображение в clipboard и
   закрывает только текущее окно; при ошибке окно остаётся открытым;
6. настройки первой portable-версии хранятся в
   `HKCU\Software\Screenshooter`, не в JSON;
7. основной язык интерфейса первой версии — русский, строки сразу выносятся в
   Win32 resources для будущей локализации;
8. первый дистрибутив — portable Release EXE; установщик выбирается после
   технических spikes;
9. курсор не включается в снимок;
10. автоматические тесты используют CTest; тестовый framework подключается как
    pinned build-only dependency и не попадает в приложение.

## 10. Definition of Done для версии 1.0

Версия готова, когда одновременно выполнены условия:

- все пользовательские сценарии разделов 6–17 исходного ТЗ реализованы либо
  отклонение явно принято и записано;
- обязательные тесты раздела 21 пройдены на Windows 10 и Windows 11;
- приложение не выполняет сетевые запросы и не требует администратора;
- Idle использует один процесс, event-driven message loop и не держит открытым
  редактор или capture session;
- preview и экспорт используют общий renderer и дают одинаковый результат;
- ошибки захвата, clipboard, файла, share и печати не приводят к потере документа;
- последовательные хоткеи создают независимые окна, а Copy/Save/Close одного
  документа не изменяет другие сессии;
- Release artifact воспроизводимо собирается из чистого checkout;
- фактические performance metrics опубликованы в README рядом с целями;
- установочный/portable artifact проверен Microsoft Defender и подписан для
  публичного распространения;
- исходники, тесты, manifest, лицензия и инструкция сборки находятся в Git.

## 11. Официальные технические опоры

- [Desktop Duplication API](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
- [`IDXGIOutput1::DuplicateOutput`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput)
- [`IGraphicsCaptureItemInterop::CreateForWindow`](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
- [Per-Monitor V2 DPI awareness](https://learn.microsoft.com/en-us/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process)
- [Direct2D built-in effects](https://learn.microsoft.com/en-us/windows/win32/direct2d/built-in-effects)
- [WinRT UI interop for desktop HWND, including Windows Share](https://learn.microsoft.com/en-us/windows/apps/develop/ui/display-ui-objects)
- [`SetWindowDisplayAffinity`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)
