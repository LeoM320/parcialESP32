/* Panel: arma la grilla con la definicion del ESP32 y refresca los valores.
 * Espejo de qt/app.py: mismo algoritmo.
 *   1. Pedir def y crear un ladrillo por variable en su fila/col.
 *   2. Polling de valores cada REFRESCO_MS.
 *   3. Al tocar un ladrillo: set y mostrar los valores que devuelve. */

const REFRESCO_MS = 1000;
const REINTENTO_MS = 2000;
const ERROR_MS = 4000;      // cuanto se muestra un error (igual que en Qt)

const grilla = document.getElementById('grilla');
const mensaje = document.getElementById('mensaje');
const ladrillos = {};       // id -> ladrillo
let finError;               // timer que borra el error

/* Error en rojo un rato. Si se repite (ej. sin conexion, en cada polling),
 * el timer vuelve a empezar: queda a la vista mientras siga fallando */
function mostrarError(err) {
    mensaje.textContent = err.message;
    mensaje.className = 'error';
    clearTimeout(finError);
    finError = setTimeout(() => {
        mensaje.textContent = '';
        mensaje.className = '';
    }, ERROR_MS);
}

/* Cada ladrillo muestra su valor; los ids desconocidos se ignoran */
function mostrarValores(valores) {
    for (const id in valores) {
        if (ladrillos[id]) {
            ladrillos[id].actualizar(valores[id]);
        }
    }
}

async function set(id, valor) {
    try {
        mostrarValores(await Conexion.set(id, valor));
    } catch (err) {
        mostrarError(err);
    }
}

function armarGrilla(def) {
    grilla.innerHTML = '';
    for (const d of def.vars) {
        const ladrillo = crearLadrillo(d, set);
        ladrillo.elemento.style.gridRow = d.fila + 1;       /* CSS cuenta desde 1 */
        ladrillo.elemento.style.gridColumn = d.col + 1;
        grilla.appendChild(ladrillo.elemento);
        ladrillos[d.id] = ladrillo;
    }
}

/* Polling encadenado: el siguiente pedido se programa recien cuando termina
 * el anterior, asi no se acumulan si el ESP32 tarda */
async function refrescar() {
    try {
        mostrarValores(await Conexion.valores());
    } catch (err) {
        mostrarError(err);
    }
    setTimeout(refrescar, REFRESCO_MS);
}

async function iniciar() {
    try {
        armarGrilla(await Conexion.def());
    } catch (err) {
        mostrarError(err);
        setTimeout(iniciar, REINTENTO_MS);  /* sin definicion no hay grilla: reintentar */
        return;
    }
    refrescar();
}

iniciar();
