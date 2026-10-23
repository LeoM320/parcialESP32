/* Conexion con el ESP32 por HTTP.
 * Espejo de qt/conexion.py: las mismas tres operaciones, con el mismo JSON.
 *   def()            -> {"vars":[{"id":"led1","tipo":"dout","etq":"LED 1","fila":0,"col":0},...]}
 *   valores()        -> {"led1":1,"led2":0,...}
 *   set(id, valor)   -> valores actualizados
 * Si falla tira un Error: 'Sin conexion con el ESP32' o el motivo que respondio el ESP32. */

async function pedirJson(url, opciones) {
    let r;
    try {
        r = await fetch(url, opciones);
    } catch (err) {
        throw new Error('Sin conexion con el ESP32');
    }
    if (!r.ok) {
        throw new Error(await r.text());
    }
    return r.json();
}

const Conexion = {
    def() {
        return pedirJson('/api/def');
    },
    valores() {
        return pedirJson('/api/valores');
    },
    set(id, valor) {
        return pedirJson('/api/set', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id, valor }),
        });
    },
};
