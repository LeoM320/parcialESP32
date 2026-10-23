/* Ladrillos: un bloque de la grilla por variable del ESP32.
 * Espejo de qt/ladrillos.py: mismas clases y mismos metodos.
 *
 * Cada tipo define solo:
 *   crear(def)        arma sus elementos a partir de la definicion
 *   actualizar(valor) muestra el valor que confirmo el ESP32
 * y para cambiar el valor llama a this.set(valor).
 *
 * Tipo nuevo: escribir la clase y sumarla a TIPOS. */

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

/* "42.5" + "%" -> "42.5 %" */
function conUnidad(valor, unidad) {
    return unidad ? valor + ' ' + unidad : String(valor);
}

/* dout: boton on/off */
class SalidaDigital extends Ladrillo {
    crear(def) {
        this.boton = this.agregar('button');
        this.boton.onclick = () => this.set('toggle');
        this.actualizar(0);
    }

    actualizar(valor) {
        this.boton.textContent = valor ? 'ON' : 'OFF';
        this.boton.className = valor ? 'encendido' : '';
    }
}

/* din: indicador (solo lectura) */
class EntradaDigital extends Ladrillo {
    crear(def) {
        this.indicador = this.agregar('p');
        this.actualizar(0);
    }

    actualizar(valor) {
        this.indicador.textContent = valor ? 'ACTIVO' : 'inactivo';
        this.indicador.className = valor ? 'indicador encendido' : 'indicador';
    }
}

/* ain: barra + valor (solo lectura) */
class EntradaAnalogica extends Ladrillo {
    crear(def) {
        this.barra = this.agregar('progress');
        this.barra.max = 1;
        this.texto = this.agregar('p');
    }

    actualizar(valor) {
        this.barra.value = (valor - this.def.min) / (this.def.max - this.def.min);
        this.texto.textContent = conUnidad(valor, this.def.unidad);
    }
}

/* pwm: deslizador; manda el valor al soltarlo */
class Pwm extends Ladrillo {
    crear(def) {
        this.barra = this.agregar('input');
        this.barra.type = 'range';
        this.barra.min = def.min;
        this.barra.max = def.max;
        this.texto = this.agregar('p');
        this.moviendo = false;      /* mientras se arrastra, el polling no lo pisa */
        this.barra.oninput = () => {
            this.moviendo = true;
            this.texto.textContent = conUnidad(this.barra.value, def.unidad);
        };
        this.barra.onchange = () => {
            this.moviendo = false;
            this.set(this.barra.value);
        };
        this.actualizar(0);
    }

    actualizar(valor) {
        if (this.moviendo) {
            return;
        }
        this.barra.value = valor;
        this.texto.textContent = conUnidad(valor, this.def.unidad);
    }
}

/* Base de num y texto: valor actual + campo + boton Enviar (o Enter) */
class Editable extends Ladrillo {
    crear(def) {
        this.texto = this.agregar('p');
        const fila = this.agregar('div');
        fila.className = 'editar';
        this.entrada = document.createElement('input');
        this.boton = document.createElement('button');
        this.boton.textContent = 'Enviar';
        fila.append(this.entrada, this.boton);
        this.boton.onclick = () => this.enviar();
        this.entrada.onkeydown = (e) => {
            if (e.key === 'Enter') {
                this.enviar();
            }
        };
    }

    enviar() {
        this.set(this.entrada.value);
        this.entrada.value = '';
    }

    actualizar(valor) {
        this.texto.textContent = conUnidad(valor, this.def.unidad);
    }
}

/* num: numero entre min y max */
class Numero extends Editable {
    crear(def) {
        super.crear(def);
        this.entrada.type = 'number';
        this.entrada.step = 'any';
        this.entrada.placeholder = def.min + ' a ' + def.max;
    }
}

/* texto: hasta 31 caracteres */
class Texto extends Editable {
    crear(def) {
        super.crear(def);
        this.entrada.maxLength = 31;
        this.entrada.placeholder = 'Nuevo texto';
    }
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

const TIPOS = {
    dout: SalidaDigital,
    din: EntradaDigital,
    ain: EntradaAnalogica,
    pwm: Pwm,
    num: Numero,
    texto: Texto,
};

function crearLadrillo(def, set) {
    const Clase = TIPOS[def.tipo] || Desconocido;
    return new Clase(def, set);
}
