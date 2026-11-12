# ADR0029: Restore Color panel in final workspace hierarchy

Date: 2026-10-24. Status: source fixed; corrected test APK published, phone acceptance pending.

User phone screenshot shows Color selected, but only generic status in its context pane.
Root cause: buildWorkspace creates Color panel in the temporary hierarchy, calls
root.removeAllViews, then reattaches other context tools but omits Color. Workspace state
changes visibility of a detached panel; no grading UI can be reached.

Reattach Color to final contextBody. Its two actions use ordinary labelled, theme-aware
buttons instead of StudioButton (which deliberately renders only icons). Put actions
before the lengthy still-preview explanation so they are discoverable in the fixed-height
scroll pane. Preserve icon-only navigation, native grade policy, timing, export guards
and dialog/session behavior. No dependency or new grading math.

Extend MinimalLayoutTest to assert both labelled Color actions are attached/shown after
switching to Color across all3themes and hidden on Edit. This instrumentation source must
compile but is not executed by default; user owns phone testing. Resource checks pass301
strings/3themes/17labels, whitespace check passes. New fresh-install candidate uses existing
explicit consent route; temporary signer still may require uninstall after backup.

## Publication evidence

Source d454c0b: Android40097805457 and Portable40097805462 pass. Release
fresh-install-40097805457 contains APK/checksum/metadata. APK8,004,471bytes; SHA256
 db6ca7507a6688bc7633f362b569a93650c0b7203aef728734651ec895164fec.
Regression instrumentation source compiles, not executed; screenshot fix awaits user
phone confirmation. No new grading algorithm or export enablement claimed.
