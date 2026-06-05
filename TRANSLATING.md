# Translating Community Shaders

Community Shaders supports multiple languages through a JSON-based translation system.
This document explains how to contribute translations.

## For Translators (No Coding Required)

### Option A: Via Weblate (Recommended)

The easiest way to contribute translations is through our hosted Weblate instance:

1. Visit: **[hosted.weblate.org/projects/community-shaders](https://hosted.weblate.org/projects/community-shaders/)** _(link will be active once configured)_
2. Create an account or log in with GitHub
3. Select your language
4. Translate strings in the web interface
5. Your translations are automatically submitted as PRs

Weblate provides:

-   Translation memory and suggestions
-   Consistency checks
-   Placeholder validation (`{name}` must be preserved)
-   Progress tracking per language

### Option B: Direct PR on GitHub

1. Fork the repository
2. Copy `package/SKSE/Plugins/CommunityShaders/Translations/en.json` to a new file named with your locale code (e.g., `zh_CN.json`, `ja.json`, `de.json`)
3. Translate the string values (NOT the keys)
4. Submit a Pull Request

## Translation File Format

```json
{
    "_meta": {
        "language": "简体中文",
        "locale": "zh_CN",
        "version": "1.0.0",
        "authors": ["Your Name"]
    },
    "menu.home.welcome": "欢迎使用 Community Shaders {version}",
    "menu.faq.q1": "什么是 Community Shaders？",
    ...
}
```

### Rules

| Rule                              | Example                                              |
| --------------------------------- | ---------------------------------------------------- |
| **Translate values, not keys**    | `"menu.faq.q1": "翻译这里"` — key 左边不改           |
| **Preserve placeholders**         | `{version}`, `{count}`, `{key}` 必须保留，位置可调整 |
| **Preserve format specifiers**    | `%s`, `%d`, `%.1f` 必须保留                          |
| **`\n` = line break**             | 可以调整分行位置                                     |
| **`_meta.language`**              | 用该语言自身书写（如 "日本語" 而非 "Japanese"）      |
| **Don't translate `##` suffixes** | 如果值中包含 `##xxx`，不翻译 `##` 后面的部分         |
| **Partial translations OK**       | 缺失的 key 会自动 fallback 到英文                    |

### Locale Codes

Use standard BCP 47-style codes:

| Code    | Language           |
| ------- | ------------------ |
| `zh_CN` | 简体中文           |
| `zh_TW` | 繁體中文           |
| `ja`    | 日本語             |
| `ko`    | 한국어             |
| `de`    | Deutsch            |
| `fr`    | Français           |
| `es`    | Español            |
| `pt_BR` | Português (Brasil) |
| `ru`    | Русский            |
| `it`    | Italiano           |
| `pl`    | Polski             |

## For Developers

### Adding New Translatable Strings

```cpp
// 1. Use T() with inline default in source code
ImGui::Text("%s", T("menu.faq.q10", "My new FAQ question?"));

// 2. For Feature files, use TKEY macro for shorter keys
#define I18N_KEY_PREFIX "feature.my_feature."
ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &settings.enabled);
#undef I18N_KEY_PREFIX

// 3. Regenerate en.json
//    Run: python tools/extract-i18n.py --write
```

### CI Validation

The `pr-i18n.yaml` workflow checks:

-   `en.json` is in sync with source code (`--check`)
-   No orphaned keys exist (`--orphans`)
-   Translation file key order matches `en.json` (`sort-i18n.py --check`)
-   Translation files have valid JSON format
-   Placeholders `{name}` are consistent across languages

### Translation Key Ordering

All non-English translation files must have their keys ordered to match `en.json`. This ensures consistency and makes diffs easier to review.

```bash
# Check if translation files are correctly ordered
python tools/sort-i18n.py --check

# Automatically reorder translation files to match en.json
python tools/sort-i18n.py --write
```

Ordering rules:

1. `_meta` always comes first
2. Keys present in `en.json` follow `en.json`'s order
3. Any extra keys not in `en.json` are appended alphabetically at the end

### Key Naming Convention

```
menu.<page>.<item>              — Menu UI labels
menu.<page>.<item>_tooltip      — Tooltip text
feature.<short_name>.<setting>  — Feature settings
overlay.<type>                  — Overlay messages
common.<term>                   — Shared/reused text
ui.<component>                  — Utility UI
weather_editor.<item>           — Weather editor
```

## CJK Font Support

CJK languages (Chinese, Japanese, Korean) require fonts with appropriate glyph coverage.
Community Shaders uses system CJK fonts by default.
