"""Ladrillos: un bloque de la grilla por variable del ESP32.
Espejo de www/ladrillos.js: mismas clases y mismos metodos.

Cada tipo define solo:
    crear(d)            arma sus widgets a partir de la definicion
    actualizar(valor)   muestra el valor que confirmo el ESP32
y para cambiar el valor llama a self.set(valor).

Tipo nuevo: escribir la clase y sumarla a TIPOS.
"""

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QGroupBox, QHBoxLayout, QLabel, QLineEdit,
                               QProgressBar, QPushButton, QSlider, QVBoxLayout)


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


def con_unidad(valor, unidad=""):
    """42.5 + "%" -> "42.5 %" """
    return f"{valor} {unidad}" if unidad else str(valor)


class SalidaDigital(Ladrillo):
    """dout: boton on/off"""

    def crear(self, d):
        self.boton = self.agregar(QPushButton())
        self.boton.setCheckable(True)   # apretado = prendido
        self.boton.setMinimumHeight(40)
        self.boton.clicked.connect(lambda: self.set("toggle"))
        self.actualizar(0)

    def actualizar(self, valor):
        self.boton.setText("ON" if valor else "OFF")
        self.boton.setChecked(bool(valor))


class EntradaDigital(Ladrillo):
    """din: indicador (solo lectura)"""

    def crear(self, d):
        self.indicador = self.agregar(QLabel())
        self.indicador.setAlignment(Qt.AlignCenter)
        self.indicador.setMinimumHeight(40)
        self.actualizar(0)

    def actualizar(self, valor):
        self.indicador.setText("ACTIVO" if valor else "inactivo")
        color = "background: #1e8e3e; color: white;" if valor else "background: #e3e6ec;"
        self.indicador.setStyleSheet(color + " border-radius: 6px;")


class EntradaAnalogica(Ladrillo):
    """ain: barra + valor (solo lectura)"""

    def crear(self, d):
        self.barra = self.agregar(QProgressBar())
        self.barra.setRange(0, 1000)        # fraccion de min..max, en milesimas
        self.barra.setTextVisible(False)
        self.texto = self.agregar(QLabel())
        self.texto.setAlignment(Qt.AlignCenter)

    def actualizar(self, valor):
        fraccion = (valor - self.d["min"]) / (self.d["max"] - self.d["min"])
        self.barra.setValue(round(fraccion * 1000))
        self.texto.setText(con_unidad(valor, self.d["unidad"]))


class Pwm(Ladrillo):
    """pwm: deslizador; manda el valor al soltarlo"""

    def crear(self, d):
        self.barra = self.agregar(QSlider(Qt.Horizontal))
        self.barra.setRange(int(d["min"]), int(d["max"]))
        self.barra.setTracking(False)       # valueChanged recien al soltar
        self.texto = self.agregar(QLabel())
        self.texto.setAlignment(Qt.AlignCenter)
        self.barra.sliderMoved.connect(lambda v: self.texto.setText(con_unidad(v, d["unidad"])))
        self.barra.valueChanged.connect(self.set)
        self.actualizar(0)

    def actualizar(self, valor):
        if self.barra.isSliderDown():       # mientras se arrastra, el polling no lo pisa
            return
        self.barra.blockSignals(True)       # que este cambio no se mande al ESP32
        self.barra.setValue(round(valor))
        self.barra.blockSignals(False)
        self.texto.setText(con_unidad(valor, self.d["unidad"]))


class Editable(Ladrillo):
    """Base de num y texto: valor actual + campo + boton Enviar (o Enter)"""

    def crear(self, d):
        self.texto = self.agregar(QLabel())
        self.texto.setAlignment(Qt.AlignCenter)
        fila = QHBoxLayout()
        self.entrada = QLineEdit()
        self.boton = QPushButton("Enviar")
        fila.addWidget(self.entrada, 1)
        fila.addWidget(self.boton)
        self.capa.addLayout(fila)
        self.boton.clicked.connect(self.enviar)
        self.entrada.returnPressed.connect(self.enviar)

    def enviar(self):
        self.set(self.entrada.text())
        self.entrada.clear()

    def actualizar(self, valor):
        self.texto.setText(con_unidad(valor, self.d.get("unidad", "")))


class Numero(Editable):
    """num: numero entre min y max"""

    def crear(self, d):
        super().crear(d)
        self.entrada.setPlaceholderText(f"{d['min']} a {d['max']}")


class Texto(Editable):
    """texto: hasta 31 caracteres"""

    def crear(self, d):
        super().crear(d)
        self.entrada.setMaxLength(31)
        self.entrada.setPlaceholderText("Nuevo texto")


class Desconocido(Ladrillo):
    """Tipo que este cliente todavia no conoce: muestra el valor tal cual"""

    def crear(self, d):
        self.texto = self.agregar(QLabel())

    def actualizar(self, valor):
        self.texto.setText(f"{self.d['tipo']}: {valor}")


TIPOS = {
    "dout": SalidaDigital,
    "din": EntradaDigital,
    "ain": EntradaAnalogica,
    "pwm": Pwm,
    "num": Numero,
    "texto": Texto,
}


def crear_ladrillo(d: dict, set):
    clase = TIPOS.get(d["tipo"], Desconocido)
    return clase(d, set)
