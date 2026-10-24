/* Ladrillos: un bloque de la grilla por variable del ESP32.
 * Espejo de qt/ladrillos.py: mismas clases y mismos metodos.
 *
 * Cada tipo define solo:
 *   crear(def)        arma sus elementos a partir de la definicion
 *   actualizar(valor) muestra el valor que confirmo el ESP32
 * y para cambiar el valor llama a this.set(valor).
 *
 * Tipo nuevo: escribir la clase y sumarla a TIPOS.
 * Mientras un tipo no tenga clase, se muestra con Desconocido. */

class Ladrillo {
    constructor(def, set) {
        this.def = def;
        this.set = (valor) => set(def.id, valor);
        this.elemento = document.createElement('div');
        this.elemento.className = 'ladrillo';
        const titulo = document.createElement('h3');
        titulo.textContent = def.etq;
        this.elemento.appendChild(titulo);
        this.crear(def);
    }

    /* Crea un elemento HTML y lo agrega al ladrillo */
    agregar(etiqueta) {
        const e = document.createElement(etiqueta);
        this.elemento.appendChild(e);
        return e;
    }

    crear(def) {}

    actualizar(valor) {}
}

/* Tipo que este cliente todavia no conoce: muestra el valor tal cual */
class Desconocido extends Ladrillo {
    crear(def) {
        this.texto = this.agregar('p');
    }

    actualizar(valor) {
        this.texto.textContent = this.def.tipo + ': ' + valor;
    }
}

/* tipo (como lo manda el ESP32) -> clase. Ej: dout: SalidaDigital, */
const TIPOS = {
};

function crearLadrillo(def, set) {
    const Clase = TIPOS[def.tipo] || Desconocido;
    return new Clase(def, set);
}
