const form = document.getElementById('form');
const mensaje = document.getElementById('mensaje');

/* Al tocar Aceptar: manda los campos como formulario (ssid=..&pass=..&puerto_int=..)
 * sin recargar la pagina, y muestra la respuesta del ESP32. */
form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const boton = form.querySelector('button');
    boton.disabled = true;

    try {
        const r = await fetch('/api/config', {
            method: 'POST',
            body: new URLSearchParams(new FormData(form)),
        });
        const texto = await r.text();
        mensaje.textContent = r.ok ? 'Configuracion enviada' : 'Error: ' + texto;
        mensaje.className = r.ok ? 'ok' : 'error';
    } catch (err) {
        mensaje.textContent = 'Sin conexion con el ESP32';
        mensaje.className = 'error';
    }

    boton.disabled = false;
});
