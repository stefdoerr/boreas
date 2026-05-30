/*
 * MyPlugin — MOD modgui controller.
 *
 * MOD-UI calls this function for each lifecycle event of the pedal
 * (start, change, end, …). For a simple plugin where every parameter
 * is a knob bound via mod-role="input-control-port", you don't need
 * any custom JS at all — MOD-UI handles knob ↔ port binding from the
 * HTML attributes. This file is the place to add behaviour like:
 *
 *   - LV2 enum parameters rendered as <select> dropdowns. (MOD-UI's
 *     default film-strip widget can't render enums well, so make a
 *     custom <select>, and in the 'change' branch translate the
 *     dropdown's string value to the enum integer index and call
 *     funcs.set_port_value(symbol, index). In the 'change' event for
 *     external port writes, do the inverse to keep the <select> in
 *     sync with preset recall / automation.)
 *   - Custom buttons that trigger one-shot actions on the plugin
 *     (e.g. an audition / test button — write 1 then 0 to a boolean
 *     LV2 port).
 *
 * See INSTRUCTIONS.md ("LV2 enum parameters → custom dropdowns") for the
 * pattern for adding enum dropdowns and one-shot trigger buttons.
 */

function (event, funcs) {

    if (event.type === 'start') {
        // Per-instance setup. event.icon is the jQuery-wrapped pedal DOM.
        // No bindings needed for a single-knob plugin — MOD-UI wires up
        // input-control-port elements automatically from the HTML.
        return;
    }

    if (event.type === 'change') {
        // event.symbol is the LV2 port symbol; event.value its current
        // value. Triggered for external writes (preset recall, MIDI
        // automation, settings popup). Only needed if you're rendering
        // something custom that needs syncing back from the port.
        return;
    }
}
