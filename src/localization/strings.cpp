#include "localization/strings.h"
#include <atomic>
#include <iterator>

namespace screenshooter::i18n {
namespace {
std::atomic<Language> current{Language::english};
struct Translation { const wchar_t* english; const wchar_t* russian; };
constexpr Translation catalog[]{
    {L"Capture area", L"Снимок области"}, // capture_area
    {L"Window under cursor", L"Окно под курсором"}, // capture_window
    {L"Monitor under cursor", L"Монитор под курсором"}, // capture_monitor
    {L"All monitors", L"Все мониторы"}, // capture_all
    {L"Open image…", L"Открыть изображение…"}, // open_image
    {L"Open save folder", L"Открыть папку сохранения"}, // open_folder
    {L"Settings…", L"Настройки…"}, // settings_menu
    {L"Exit", L"Выход"}, // exit
    {L"Hotkeys", L"Горячие клавиши"}, // hotkeys
    {L"Enable global hotkeys", L"Включить глобальные горячие клавиши"}, // enable_hotkeys
    {L"Capture area:", L"Снимок области:"}, // area_label
    {L"Monitor under cursor:", L"Монитор под курсором:"}, // monitor_label
    {L"Window under cursor:", L"Окно под курсором:"}, // window_label
    {L"Save folder", L"Папка сохранения"}, // save_folder
    {L"Browse…", L"Обзор…"}, // browse
    {L"Start Screenshooter when I sign in to Windows", L"Запускать Screenshooter при входе в Windows"}, // autostart
    {L"Save", L"Сохранить"}, // save
    {L"Cancel", L"Отмена"}, // cancel
    {L"Set all three shortcuts.", L"Задайте все три сочетания."}, // missing_hotkeys
    {L"Shortcuts must be different.", L"Сочетания не должны повторяться."}, // duplicate_hotkeys
    {L"Screenshooter settings", L"Настройки Screenshooter"}, // settings_title
    {L"Could not start the application.", L"Не удалось запустить приложение."}, // startup_error
    {L"Could not create the notification area icon.", L"Не удалось создать значок в области уведомлений."}, // tray_error
    {L"No available window under the cursor.", L"Под курсором нет доступного окна."}, // no_window
    {L"Unknown capture error.", L"Неизвестная ошибка захвата."}, // capture_unknown
    {L"Could not open area selection.", L"Не удалось открыть выбор области."}, // selection_error
    {L"Could not open the screenshot.", L"Не удалось открыть снимок."}, // editor_error
    {L"Images", L"Изображения"}, // images
    {L"All files", L"Все файлы"}, // all_files
    {L"This image format is not supported.", L"Формат изображения не поддерживается."}, // unsupported_image
    {L"Could not open the save folder.", L"Не удалось открыть папку сохранения."}, // folder_error
    {L"A selected shortcut is already in use. Previous settings were kept.", L"Одно из выбранных сочетаний уже занято. Старые настройки сохранены."}, // hotkey_busy
    {L"Could not change startup settings. Previous settings were kept.", L"Не удалось изменить автозапуск. Старые настройки сохранены."}, // autostart_error
    {L"Could not save settings. Previous values were restored.", L"Не удалось сохранить настройки. Старые значения восстановлены."}, // settings_error
    {L"Could not capture the desktop.", L"Не удалось получить изображение рабочего стола."}, // capture_error
    {L"Close", L"Закрыть"}, // close
    {L"About Screenshooter", L"Информация о Screenshooter"}, // info_title
    {L"Apply", L"Применить"}, // apply
    {L"Settings", L"Настройки"}, // settings
    {L"Fit", L"Вписать"}, // fit
    {L"Rectangle", L"Прямоугольник"}, // rectangle
    {L"Ellipse", L"Эллипс"}, // ellipse
    {L"Line", L"Линия"}, // line
    {L"Arrow", L"Стрелка"}, // arrow
    {L"Marker", L"Маркер"}, // marker
    {L"Text", L"Текст"}, // text
    {L"Blur", L"Размыть"}, // blur
    {L"Crop", L"Обрезать"}, // crop
    {L"Select", L"Выбор"}, // select
    {L"Information", L"Информация"}, // info
    {L"Zoom: 100%", L"Масштаб: 100%"}, // zoom_initial
    {L"Red", L"Красный"}, // red
    {L"Green", L"Зелёный"}, // green
    {L"Blue", L"Синий"}, // blue
    {L"Black", L"Чёрный"}, // black
    {L"Bold", L"Полужирный"}, // bold
    {L"Italic", L"Курсив"}, // italic
    {L"Underline", L"Подчёркнутый"}, // underline
    {L"Copy to clipboard and close", L"Копировать в буфер обмена и закрыть"}, // copy_accessible
    {L"Copy the screenshot to the clipboard and close the editor", L"Копировать снимок в буфер обмена и закрыть редактор"}, // copy_tip
    {L"Actual size: one image pixel per screen pixel", L"Исходный размер: один пиксель изображения на пиксель экрана"}, // actual_tip
    {L"Fit screenshot to window (Ctrl+0)", L"Вписать снимок в окно (Ctrl+0)"}, // fit_tip
    {L"Zoom: ", L"Масштаб: "}, // zoom_prefix
    {L"Could not apply the crop.", L"Не удалось применить обрезку."}, // crop_error
    {L"Could not copy the image.", L"Не удалось скопировать изображение."}, // copy_error
    {L"Could not open the save dialog.", L"Не удалось открыть диалог сохранения."}, // save_dialog_error
    {L"PNG image", L"PNG изображение"}, // png
    {L"JPEG image", L"JPEG изображение"}, // jpeg
    {L"Could not select a save location.", L"Не удалось выбрать путь сохранения."}, // save_path_error
    {L"Could not save the image.", L"Не удалось сохранить изображение."}, // save_error
    {L"Could not determine the default save folder.", L"Не удалось определить папку сохранения по умолчанию."}, // default_folder_error
    {L"Could not save the image to the default folder.", L"Не удалось сохранить изображение в папку по умолчанию."}, // default_save_error
    {L"Could not open the image.", L"Не удалось открыть изображение."}, // open_error
    {L"Save changes before closing?", L"Сохранить изменения перед закрытием?"}, // unsaved
    {L"Select area — Screenshooter", L"Выбор области — Screenshooter"}, // selection_title
    {L"Screenshooter screenshot", L"Снимок Screenshooter"}, // share_title
    {L"Language / Язык", L"Язык / Language"}, // language
    {L"Copy to clipboard\nand close", L"Копировать в буфер\nи закрыть"}, // copy
    {L"One or more shortcuts are in use by another application.\nYou can capture from the Screenshooter tray menu.", L"Одно или несколько сочетаний заняты другим приложением.\nДоступные режимы можно запустить через значок Screenshooter в трее."}, // hotkeys_busy
    {L"QUICK START\r\nOpen the Screenshooter tray menu, choose a capture mode, edit your screenshot, then click Copy or Save.\r\n\r\nCAPTURE MODES\r\nCapture an area, the window under the cursor, the monitor under the cursor, or all monitors. Configured global shortcuts can also start captures.\r\n\r\nEDITOR TOOLS\r\nRectangle, ellipse, line, arrow, marker, text, blur, crop and select. Shapes and text support red, green, blue and black. Text also supports font sizes, bold, italic and underline. Double-click existing text to edit it again.\r\n\r\nSHIFT\r\nHold Shift to draw squares and circles, constrain lines and arrows to 45-degree steps, straighten the marker, or preserve an inserted image's proportions. You can press or release Shift while resizing.\r\n\r\nSELECTION AND HISTORY\r\nMove selected objects, resize them using handles, or delete them with Delete, Backspace or Esc. Ctrl+Z undoes an action; Ctrl+Y redoes it.\r\n\r\nCLIPBOARD AND FILES\r\nCtrl+C copies the selected object. With no selection, it copies the finished screenshot and closes the window. Ctrl+X cuts an object. Ctrl+V pastes an object or an image from the clipboard. You can also drag an image into the editor.\r\n\r\nSAVING\r\nSave opens the system PNG/JPEG dialog. Configure the default folder, hotkeys, startup and language using the Settings button or Settings in the tray menu.", L"БЫСТРЫЙ СТАРТ\r\nОткройте меню значка Screenshooter в трее, выберите режим захвата, отредактируйте снимок и нажмите «Копировать» или «Сохранить».\r\n\r\nРЕЖИМЫ ЗАХВАТА\r\nДоступны снимок области, окна под курсором, монитора под курсором и всех мониторов. Те же действия можно запускать настроенными глобальными горячими клавишами.\r\n\r\nИНСТРУМЕНТЫ РЕДАКТОРА\r\nПрямоугольник, эллипс, линия, стрелка, маркер, текст, размытие, обрезка и выбор. Для фигур и текста доступны красный, зелёный, синий и чёрный цвета. Для текста также можно выбрать размер, полужирное, курсивное и подчёркнутое начертание. Двойной щелчок по существующему тексту возобновляет его редактирование.\r\n\r\nSHIFT\r\nShift превращает прямоугольник в квадрат и эллипс в круг, фиксирует линию и стрелку с шагом 45°, выпрямляет маркер и сохраняет пропорции вставленного изображения. Shift можно нажать или отпустить прямо во время изменения размера.\r\n\r\nВЫДЕЛЕНИЕ И ИСТОРИЯ\r\nВыделенный объект можно перемещать, изменять за управляющие точки и удалять клавишами Delete, Backspace или Esc. Ctrl+Z отменяет действие, Ctrl+Y повторяет его.\r\n\r\nБУФЕР ОБМЕНА И ФАЙЛЫ\r\nCtrl+C копирует выделенный объект, а без выделения копирует готовый снимок и закрывает окно. Ctrl+X вырезает объект, Ctrl+V вставляет объект или изображение из системного буфера. Изображение также можно перетащить в редактор.\r\n\r\nСОХРАНЕНИЕ\r\n«Сохранить» открывает системный диалог PNG/JPEG. Папка по умолчанию, горячие клавиши автозапуск и язык настраиваются кнопкой с шестерёнкой или пунктом «Настройки…» в трее."}, // instructions
    {L"Could not open the repository in your browser.\nhttps://github.com/dmitnik/screenshooter", L"Не удалось открыть репозиторий в браузере.\nhttps://github.com/dmitnik/screenshooter"}, // repository_open_error
};
static_assert(std::size(catalog) == static_cast<std::size_t>(Id::count));
}
Language CurrentLanguage() noexcept { return current.load(); }
const wchar_t* Text(Id id, Language language) noexcept {
    const auto index = static_cast<std::size_t>(id);
    if (index >= std::size(catalog)) return L"";
    return language == Language::russian ? catalog[index].russian : catalog[index].english;
}
const wchar_t* Text(Id id) noexcept { return Text(id, CurrentLanguage()); }
void SetLanguage(Language language) {
    if (current.exchange(language) == language) return;
    ::EnumThreadWindows(::GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
        ::SendMessageW(window, kLanguageChanged, 0, 0);
        return TRUE;
    }, 0);
}
}  // namespace screenshooter::i18n
