# Инкремент 7: производительность и portable Release

Дата проверки: 2026-08-28

Среда: Windows 11 x64, 1920 × 1080, Release `/MT`.

Результаты:

- idle private bytes: 1,88 МБ;
- idle CPU за 10 секунд: 0 мс;
- area hotkey → overlay: 116 мс;
- monitor hotkey → editor: 142 мс;
- два параллельно открытых редактора независимы, Copy оставляет второй открытым;
- 100 capture/open/close циклов: 25,3 с, в среднем 253,3 мс;
- handles: 148 cold, 203 после 100 циклов, 202 после 200 циклов;
- private bytes: 1,86 МБ cold, 14,82 МБ после 100 циклов, 17,83 МБ после 200;
- idle после прогрева остаётся ниже бюджета 20 МБ;
- приложение штатно завершилось после каждого smoke/stress-прогона;
- 28/28 CTest прошли в Debug и Release.

Собственные editor windows используют `WDA_EXCLUDEFROMCAPTURE`. Смена дисплея и
suspend отменяют активный selection overlay; Explorer restart восстанавливает tray
icon через `TaskbarCreated`.

Не закрытые выпускные проверки:

- Windows 10 1903 и реальная конфигурация mixed-DPI/multi-monitor/HDR;
- High Contrast и полный keyboard-only accessibility audit;
- реальные Share targets и принтеры;
- code signing и Microsoft Defender submission.
