# Инкременты 3–4: инструменты, Gaussian Blur и Crop

Дата проверки: 2026-08-28

Реализованы в общей объектной модели и общем preview/export renderer:

- прямоугольник, линия, эллипс и звезда;
- прямая стрелка и пунктирная изогнутая стрелка;
- полупрозрачный marker stroke как один объект;
- Unicode-текст через нативное поле ввода (`Ctrl+Enter`, `Esc`);
- вставка скопированных объектов и `CF_DIBV5`-изображений;
- Gaussian Blur по нижележащему raster input;
- неразрушающий Crop с Undo/Redo;
- move, нижняя правая scale handle, Delete/Backspace и keyboard nudge.

Автоматически подтверждено:

- 24/24 теста Debug;
- blur меняет нижележащие пиксели, но следующая annotation остаётся резкой;
- crop не меняет исходный bitmap и задаёт размер экспортного изображения;
- marker остаётся единым hit-testable объектом;
- arrow, ellipse и star проходят общий raster renderer.

Перед выпуском остаётся ручная матрица Unicode/font presets, 4K с 100 объектами,
смешанный DPI, crop handles на границах и визуальное сравнение Gaussian Blur с
Direct2D reference output.
