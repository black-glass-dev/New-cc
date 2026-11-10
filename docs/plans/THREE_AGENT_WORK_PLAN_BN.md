# তিন এজেন্ট — বর্তমান দায়িত্ব (ব্যবহারকারীর সর্বশেষ সংশোধন)

পুরোনো native-curves/QA বণ্টন superseded।

- A/coordinator: color editor, saved-grade main viewer, renderer/codec/export, shared
  MainActivity/build/JNI/schema integration ও final review।
- B: timeline-specific view/controller/geometry/tests; exact file allowlist ঘোষণা করবে।
- C: project-library/recovery-specific UI/storage adapter/tests; exact file allowlist ঘোষণা করবে।

Shared MainActivity, build/CI, resources, JNI ও persistence schema-এ B/C সরাসরি simultaneous
edit নয়; প্রয়োজনীয় patch/API proposal coordinator-কে দেবে। Same checkout/index-এ concurrent
work নয়। আলাদা Arena session/workspace ও নিজস্ব assigned branch; force-push/switch/rename নয়।
সর্বশেষ baseline যাচাই; PR/commit/patch handoff, tests/risks/UNRUN আলাদা। Coordinator নিজের
branch-এ review/integrate করবে। অন্য agent-এর work overwrite নয়।

B প্রথমে scrub/playhead/zoom/selection এবং split/trim/delete/reorder/history reliability।
C প্রথমে library create/open/rename/delete, relink, save/reopen/autosave/recovery এবং failed
load-এ data preservation। No new dependency/GPL ports/destructive migration without review।
Host tests phone proof নয়। APK কেবল ব্যবহারকারী চাইলে coordinator দেবেন।

A-এর ADR0042 checkpoint: saved-grade main viewer, exclusive reference/live layers, correct
Input eligibility এবং reachable export Details। Device acceptance pending। Worker sessions
বা handoffs এই চ্যাট থেকে স্বয়ংক্রিয়ভাবে দৃশ্যমান নয়; PR/report প্রয়োজন।25%/75%।
