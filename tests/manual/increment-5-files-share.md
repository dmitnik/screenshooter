# Инкремент 5: файлы и Windows Share

Дата проверки: 2026-08-28

Реализовано:

- Save As через системный диалог для PNG и JPEG;
- WIC decode PNG/JPEG/BMP и условный WebP через установленный WIC codec;
- открытие файла из tray menu;
- drag-and-drop изображения в текущий документ;
- вставка `CF_DIBV5` из clipboard как image annotation;
- Windows Share через `IDataTransferManagerInterop` и PNG stream;
- очистка временных share PNG старше 24 часов при следующем запуске;
- ошибки и отмена системных диалогов не закрывают документ.

Автоматические проверки:

- PNG round-trip сохраняет размер и контрольный пиксель;
- JPEG round-trip создаёт читаемый непрозрачный файл ожидаемого размера;
- все 26 CTest-тестов Debug прошли.

Windows Share interop ранее отдельно подтверждён техническим spike. Перед выпуском
нужно повторить интерактивную проверку с реальными получателями Share и принтерами
на Windows 10 и Windows 11.
