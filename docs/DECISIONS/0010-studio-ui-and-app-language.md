# ADR 0010 — Studio UI, semantic icons and explicit app language

Accepted implementation direction, 2026-10-22. User reports the project-workspace
APK works well on their phone and requests a modern UI, icon editing actions, Bengali
and English with mandatory first-run choice and Home Settings changes.

Use original Canvas stroke icons, platform Views, a shared slate/blue visual palette,
rounded project cards/tool surfaces and refined timeline ruler/lane/clip presentation.
No icon font, external artwork or UI dependency added. Editing/tool actions display
icons rather than text. Their localized Button text remains semantic state only:
content descriptions, 52dp minimum targets, focus/selected outline and native tooltip.
Do not remove confirmation dialog text, project names, explanatory copy or language
names: destructive-consent and first-run choices must remain understandable.

Application-owned preference (`en` or `bn`) creates localized Configuration contexts
on API 26+. First launch shows both language names and bilingual explanation before
library I/O. Home Settings changes the preference and recreates only Home, never a
live editor project. Default resources are English; values-bn is Bengali. Android
system-wide/app-language Settings integration is not advertised in this first version;
OS pickers/provider/codec diagnostics may follow system language. Project names,
original media paths, technical timecodes and native IDs are not translated or edited.
No project-format, timeline math, preview or render-preset change.

Resources include statuses, confirmations, recovery, export and app-authored errors,
not just primary menus. Locale-independent test assertions replace language-specific
assumptions without weakening storage or cleanup checks. Current Flow stays read-only.

Official guidance inspected:
- https://developer.android.com/guide/topics/resources/app-languages
- https://developer.android.com/guide/topics/ui/accessibility/apps

Platform Configuration localization is used without introducing AppCompat/Compose.
The per-app-language overview informed the lifecycle boundary; it does not imply
LocaleManager/system Settings integration is implemented. Resource/key/placeholder
parity and 4.5:1 palette checks run as a dependency-free Python gate. New Android tests
exercise first-run language choice, retention/recreation, real Settings switch and
both-language accessible icon editor controls. Host tests cannot validate Android
rendered layout; full regression CI and phone ergonomics remain separate gates.

Executed: final `1e701d4`, Android `39764813788` and portable `39764813603`
passed. APK/checksum published as `phone-test-39764813788`. Physical phone visual
review remains pending; see BUILD_AND_TEST for exact gate/optional artifact limits.
