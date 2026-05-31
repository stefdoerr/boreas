function (event, funcs) {

    var MODE_VALUES   = ['moment', 'latch'];   // index matches the C++ enum order
    var METHOD_VALUES = ['sinusoidal', 'loop'];

    if (event.type === 'start') {
        var icon   = event.icon;

        // Mode dropdown -> integer enum port. (Mode select is the one WITHOUT
        // the method class, so exclude it.)
        icon.find('.boreas-mode-select').not('.boreas-method-select').on('change', function () {
            if (icon.data('suppress-emit')) return;
            funcs.set_port_value('mode', MODE_VALUES.indexOf(this.value));
        });

        // Method dropdown -> integer enum port.
        icon.find('.boreas-method-select').on('change', function () {
            if (icon.data('suppress-emit')) return;
            funcs.set_port_value('method', METHOD_VALUES.indexOf(this.value));
        });

        // Momentary buttons: port = 1 while pressed, 0 on release. MOD reads
        // control ports once per audio block, so a fast tap can collapse to just
        // "0" and the rising edge is missed -- so enforce a minimum 90 ms hold.
        // Clear's press DURATION matters (plugin: tap = remove last layer, hold
        // >= 0.4s = wipe all), so we follow the real release, never auto-release.
        var HOLD = 90;  // ms minimum
        function momentary(sel, sym) {
            var el = icon.find(sel), downAt = 0;
            el.on('mousedown touchstart', function (e) {
                e.preventDefault(); downAt = Date.now();
                funcs.set_port_value(sym, 1);
            });
            el.on('mouseup mouseleave touchend', function () {
                var held = Date.now() - downAt;
                if (held < HOLD) setTimeout(function () { funcs.set_port_value(sym, 0); }, HOLD - held);
                else funcs.set_port_value(sym, 0);
            });
        }
        momentary('.boreas-freeze', 'footswitch');
        momentary('.boreas-clear', 'clear');
        return;
    }

    if (event.type === 'change') {
        // Keep the Mode dropdown in sync with preset recall / automation.
        var icon2 = event.icon;
        if (event.symbol === 'mode') {
            icon2.data('suppress-emit', true);
            icon2.find('.boreas-mode-select').not('.boreas-method-select').val(MODE_VALUES[Math.round(event.value)]);
            icon2.data('suppress-emit', false);
        } else if (event.symbol === 'method') {
            icon2.data('suppress-emit', true);
            icon2.find('.boreas-method-select').val(METHOD_VALUES[Math.round(event.value)]);
            icon2.data('suppress-emit', false);
        }
        return;
    }
}
