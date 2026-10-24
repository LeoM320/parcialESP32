"""Ladrillos: un bloque de la grilla por variable del ESP32.
Espejo de www/ladrillos.js: mismas clases y mismos metodos.

Cada tipo define solo:
    crear(d)            arma sus widgets a partir de la definicion
    actualizar(valor)   muestra el valor que confirmo el ESP32
y para cambiar el valor llama a self.set(valor).

Tipo nuevo: escribir la clase y sumarla a TIPOS.
Mientras un tipo no tenga clase, se muestra con Desconocido.
"""

from PySide6.QtWidgets import QGroupBox, QLabel, QVBoxLayout


class Ladrillo(QGroupBox):
    def __init__(self, d: dict, set):
        super().__init__(d["etq"])
        self.d = d
        self.set = lambda valor: set(d["id"], valor)
        self.capa = QVBoxLayout(self)
        self.crear(d)

    def agregar(self, widget):
        """Agrega un widget al ladrillo y lo devuelve"""
        self.capa.addWidget(widget)
        return widget

    def crear(self, d: dict):
        pass

    def actualizar(self, valor):
        pass


class Desconocido(Ladrillo):
    """Tipo que este cliente todavia no conoce: muestra el valor tal cual"""

    def crear(self, d):
        self.texto = self.agregar(QLabel())

    def actualizar(self, valor):
        self.texto.setText(f"{self.d['tipo']}: {valor}")


# tipo (como lo manda el ESP32) -> clase. Ej: "dout": SalidaDigital,
TIPOS = {
}


def crear_ladrillo(d: dict, set):
    clase = TIPOS.get(d["tipo"], Desconocido)
    return clase(d, set)
