/*
 * M5GFX-based status display for the trigger4p AtomS3.
 * Reads from trigger_state and paints a 128x128 layout showing:
 *   - link state           (BOOT / SCANNING / CONNECTING / LINKED / LOST)
 *   - channel ON/OFF/BLINK (4 small status pills)
 *   - last commanded dim   (or "—" if never set)
 */

#ifndef TRIGGER_UI_H
#define TRIGGER_UI_H

void trigger_ui_init(void);

/* Call from a periodic task or main loop; the renderer diffs against its
 * cache and only redraws regions whose value actually changed. Safe to call
 * every 100-200 ms. */
void trigger_ui_tick(void);

#endif /* TRIGGER_UI_H */
