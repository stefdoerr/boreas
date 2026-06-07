function (event, funcs) {

    if (event.type === 'start') {
        var icon = event.icon;

        // Momentary footswitch buttons: port = 1 while pressed, 0 on release. MOD
        // reads control ports once per audio block, so a fast tap can collapse to
        // just "0" and the rising edge is missed -- enforce a 90 ms minimum hold.
        // Press DURATION matters (Hold sustains while held; Clear tap = remove last
        // layer, hold >= 0.4s = wipe all), so we follow the real release.
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
        momentary('.boreas-hold',   'hold');
        momentary('.boreas-clear',  'clear');
        return;
    }
}
