# 国际化与翻译 (i18n)

本目录用于存放 Linernotes 的翻译文件（`.ts` / `.qm`）。

## 更新与编辑翻译

1. **更新 `.ts` 提取文件**：
   在构建目录下运行 `update_translations` 目标，自动从 C++ 源码与 QML 文件中提取/更新可翻译字符串：
   ```bash
   cmake --build <build-dir> --target update_translations
   ```
   例如：
   ```bash
   cmake --build build/debug --target update_translations
   ```

2. **编辑翻译**：
   使用 [Qt Linguist](https://doc.qt.io/qt-6/qtlinguist-index.html) 或直接使用文本/XML 编辑器打开 `i18n/linernotes_zh_CN.ts` 填写翻译内容。

3. **编译翻译**：
   项目构建时会通过 CMake `qt_add_translations` 自动调用 `lrelease` 将 `.ts` 编译为 `.qm` 并嵌入二进制资源 `:/i18n/linernotes_zh_CN.qm`。

## 注意事项

- **不要手写或手动创建 `.ts` 文件**：应由 `update_translations` (lupdate) 自动生成，保持上下文和行号同步。
- **英文作为源语言**：应用默认源语言为英文，通常不需要 `linernotes_en.ts`。英文复数形式（如 `%n track(s)`）依赖 Qt 的源语言复数规则；若特定 Qt 版本需要英文复数映射文件才能正确显示单复数，可在 `CMakeLists.txt` 的 `I18N_TRANSLATED_LANGUAGES` 中添加 `en` 生成 `linernotes_en.ts`。
