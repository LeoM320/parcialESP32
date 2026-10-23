"""Panel del ESP32 por TCP.
Espejo de www/app.js: mismo algoritmo.
    1. Al conectar, pedir def y crear un ladrillo por variable en su fila/col.
    2. Polling de valores cada REFRESCO_MS.
    3. Al tocar un ladrillo: set y mostrar los valores que devuelve.
Arriba: IP, puerto y Conectar.

Ejecutar:  python app.py   (o doble clic en abrir.bat)
"""

import sys

from PySide6.QtCore import QRegularExpression, QSettings, QTimer
from PySide6.QtGui import QRegularExpressionValidator
from PySide6.QtWidgets import (QApplication, QGridLayout, QHBoxLayout, QLabel,
                               QLineEdit, QMainWindow, QPushButton, QSpinBox,
                               QVBoxLayout, QWidget)

import conexion as cx
from ladrillos import crear_ladrillo

REFRESCO_MS = 1000
ERROR_MS = 4000         # cuanto se muestra un error del ESP32

IP_VALIDA = r"^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$"

COLORES = {cx.DESCONECTADO: "#c5221f", cx.CONECTANDO: "#f9ab00", cx.CONECTADO: "#1e8e3e"}


class Ventana(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ESP32 - Panel")
        self._config = QSettings("ParcialESP32", "Control")   # recuerda IP y puerto

        self._conexion = cx.Conexion(self)
        self._conexion.estado_cambiado.connect(self._al_cambiar_estado)
        self._conexion.def_recibida.connect(self._armar_grilla)
        self._conexion.valores_recibidos.connect(self._mostrar_valores)
        self._conexion.error_recibido.connect(self._mostrar_error)

        # ---- barra superior ----
        self._ip = QLineEdit(self._config.value("ip", "192.168.4.1"))
        self._ip.setPlaceholderText("IP del ESP32")
        self._ip.setValidator(QRegularExpressionValidator(QRegularExpression(IP_VALIDA)))
        self._ip.returnPressed.connect(self._al_tocar_boton)

        self._puerto = QSpinBox()
        self._puerto.setRange(1, 65535)
        self._puerto.setValue(int(self._config.value("puerto", 1234)))

        self._boton = QPushButton("Conectar")
        self._boton.clicked.connect(self._al_tocar_boton)

        self._led = QLabel("●")
        self._mensaje = QLabel()
        self._estado_txt = ""       # mensaje del estado, se vuelve a el despues de un error
        self._fin_error = QTimer(self, singleShot=True, interval=ERROR_MS)
        self._fin_error.timeout.connect(self._mostrar_estado)

        barra = QHBoxLayout()
        barra.addWidget(QLabel("IP:"))
        barra.addWidget(self._ip, 1)
        barra.addWidget(QLabel("Puerto:"))
        barra.addWidget(self._puerto)
        barra.addWidget(self._boton)
        barra.addWidget(self._led)

        # ---- grilla de ladrillos (se arma al recibir la definicion) ----
        self._panel = QWidget()
        self._grilla = QGridLayout(self._panel)
        self._ladrillos = {}        # id -> ladrillo

        self._refresco = QTimer(self, interval=REFRESCO_MS)
        self._refresco.timeout.connect(self._conexion.pedir_valores)

        central = QWidget()
        columna = QVBoxLayout(central)
        columna.addLayout(barra)
        columna.addWidget(self._mensaje)
        columna.addWidget(self._panel)
        columna.addStretch(1)
        self.setCentralWidget(central)
        self.resize(720, 460)

        self._al_cambiar_estado(cx.DESCONECTADO, "Ingresa la IP y el puerto del ESP32")

    # ---------- conexion ----------

    def _al_tocar_boton(self):
        if self._conexion.estado == cx.DESCONECTADO:
            if not self._ip.hasAcceptableInput():
                self._mensaje.setText("IP invalida")
                return
            self._config.setValue("ip", self._ip.text())
            self._config.setValue("puerto", self._puerto.value())
            self._conexion.conectar(self._ip.text(), self._puerto.value())
        else:
            self._conexion.desconectar()

    def _al_cambiar_estado(self, estado: str, mensaje: str):
        desconectado = estado == cx.DESCONECTADO
        self._boton.setText("Conectar" if desconectado else "Desconectar")
        self._ip.setEnabled(desconectado)
        self._puerto.setEnabled(desconectado)
        self._panel.setEnabled(estado == cx.CONECTADO)
        self._led.setStyleSheet(f"color: {COLORES[estado]}; font-size: 18px;")
        self._estado_txt = mensaje
        self._mostrar_estado()

        if estado == cx.CONECTADO:
            self._conexion.pedir_def()      # la grilla se arma cuando llega
            self._refresco.start()
        else:
            self._refresco.stop()

    def _mostrar_estado(self):
        self._fin_error.stop()
        self._mensaje.setStyleSheet("")
        self._mensaje.setText(self._estado_txt)

    def _mostrar_error(self, texto: str):
        """Error que respondio el ESP32 (ej. valor fuera de rango): en rojo un rato"""
        self._mensaje.setStyleSheet("color: #c5221f;")
        self._mensaje.setText(texto)
        self._fin_error.start()

    # ---------- grilla ----------

    def _armar_grilla(self, definicion: dict):
        for ladrillo in self._ladrillos.values():
            ladrillo.deleteLater()
        self._ladrillos.clear()
        for d in definicion["vars"]:
            ladrillo = crear_ladrillo(d, self._conexion.set)
            self._grilla.addWidget(ladrillo, d["fila"], d["col"])
            self._ladrillos[d["id"]] = ladrillo

    def _mostrar_valores(self, valores: dict):
        """Cada ladrillo muestra su valor; los ids desconocidos se ignoran"""
        for id, valor in valores.items():
            if id in self._ladrillos:
                self._ladrillos[id].actualizar(valor)

    def closeEvent(self, evento):
        self._conexion.desconectar()
        super().closeEvent(evento)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    ventana = Ventana()
    ventana.show()
    sys.exit(app.exec())
