"""Panel del ESP32 por HTTP (la misma API que usa la pagina web).
Espejo de www/app.js: mismo algoritmo.
    1. Al conectar, pedir def y crear un ladrillo por variable en su fila/col.
    2. Polling de valores cada REFRESCO_MS.
    3. Al tocar un ladrillo: set y mostrar los valores que devuelve.
Arriba: IP y Conectar.

Ejecutar:  python app.py   (o doble clic en abrir.bat)
"""

import sys

from PySide6.QtCore import QRegularExpression, QSettings, QTimer
from PySide6.QtGui import QRegularExpressionValidator
from PySide6.QtWidgets import (QApplication, QGridLayout, QHBoxLayout, QLabel,
                               QLineEdit, QMainWindow, QPushButton, QVBoxLayout,
                               QWidget)

from conexion import Conexion
from ladrillos import crear_ladrillo

REFRESCO_MS = 1000
ERROR_MS = 4000         # cuanto se muestra un error (igual que en la web)

IP_VALIDA = r"^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$"

DESCONECTADO, CONECTANDO, CONECTADO = "desconectado", "conectando", "conectado"
COLORES = {DESCONECTADO: "#c5221f", CONECTANDO: "#f9ab00", CONECTADO: "#1e8e3e"}


class Ventana(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ESP32 - Panel")
        self._config = QSettings("ParcialESP32", "Control")   # recuerda la IP
        self._conexion = Conexion(self)
        self._estado = DESCONECTADO

        # ---- barra superior ----
        self._ip = QLineEdit(self._config.value("ip", "192.168.4.1"))
        self._ip.setPlaceholderText("IP del ESP32")
        self._ip.setValidator(QRegularExpressionValidator(QRegularExpression(IP_VALIDA)))
        self._ip.returnPressed.connect(self._al_tocar_boton)

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
        barra.addWidget(self._boton)
        barra.addWidget(self._led)

        # ---- grilla de ladrillos (se arma al recibir la definicion) ----
        self._panel = QWidget()
        self._grilla = QGridLayout(self._panel)
        self._ladrillos = {}        # id -> ladrillo

        # Polling encadenado: el siguiente pedido se programa recien cuando
        # termina el anterior (singleShot), asi no se acumulan si el ESP32 tarda
        self._refresco = QTimer(self, singleShot=True, interval=REFRESCO_MS)
        self._refresco.timeout.connect(self._refrescar)

        central = QWidget()
        columna = QVBoxLayout(central)
        columna.addLayout(barra)
        columna.addWidget(self._mensaje)
        columna.addWidget(self._panel)
        columna.addStretch(1)
        self.setCentralWidget(central)
        self.resize(720, 460)

        self._cambiar(DESCONECTADO, "Ingresa la IP del ESP32")

    # ---------- conexion ----------

    def _al_tocar_boton(self):
        if self._estado != DESCONECTADO:
            self._cambiar(DESCONECTADO, "Desconectado")
            return
        if not self._ip.hasAcceptableInput():
            self._mostrar_error("IP invalida")
            return
        self._config.setValue("ip", self._ip.text())
        self._conexion.ip = self._ip.text()
        self._cambiar(CONECTANDO, f"Conectando a {self._ip.text()}...")
        self._conexion.definicion(self._al_conectar, self._al_no_conectar)

    def _al_conectar(self, definicion: dict):
        if self._estado != CONECTANDO:      # tocaron Desconectar mientras esperaba
            return
        self._armar_grilla(definicion)
        self._cambiar(CONECTADO, f"Conectado a {self._conexion.ip}")
        self._refrescar()

    def _al_no_conectar(self, mensaje: str):
        if self._estado == CONECTANDO:
            self._cambiar(DESCONECTADO, mensaje)

    def _cambiar(self, estado: str, mensaje: str):
        self._estado = estado
        desconectado = estado == DESCONECTADO
        self._boton.setText("Conectar" if desconectado else "Desconectar")
        self._ip.setEnabled(desconectado)
        self._panel.setEnabled(estado == CONECTADO)
        self._led.setStyleSheet(f"color: {COLORES[estado]}; font-size: 18px;")
        self._estado_txt = mensaje
        self._mostrar_estado()
        if desconectado:
            self._refresco.stop()

    # ---------- polling y set ----------

    def _refrescar(self):
        self._conexion.valores(self._al_refrescar, self._al_fallar_refresco)

    def _al_refrescar(self, valores: dict):
        self._mostrar_valores(valores)
        self._seguir()

    def _al_fallar_refresco(self, mensaje: str):
        self._mostrar_error(mensaje)
        self._seguir()

    def _seguir(self):
        if self._estado == CONECTADO:
            self._refresco.start()

    def _set(self, id: str, valor):
        self._conexion.set(id, valor, self._mostrar_valores, self._mostrar_error)

    # ---------- mensajes ----------

    def _mostrar_estado(self):
        self._fin_error.stop()
        self._mensaje.setStyleSheet("")
        self._mensaje.setText(self._estado_txt)

    def _mostrar_error(self, texto: str):
        """Error en rojo un rato. Si se repite (ej. sin conexion, en cada polling),
        el timer vuelve a empezar: queda a la vista mientras siga fallando"""
        self._mensaje.setStyleSheet("color: #c5221f;")
        self._mensaje.setText(texto)
        self._fin_error.start()

    # ---------- grilla ----------

    def _armar_grilla(self, definicion: dict):
        for ladrillo in self._ladrillos.values():
            ladrillo.deleteLater()
        self._ladrillos.clear()
        for d in definicion["vars"]:
            ladrillo = crear_ladrillo(d, self._set)
            self._grilla.addWidget(ladrillo, d["fila"], d["col"])
            self._ladrillos[d["id"]] = ladrillo

    def _mostrar_valores(self, valores: dict):
        """Cada ladrillo muestra su valor; los ids desconocidos se ignoran"""
        for id, valor in valores.items():
            if id in self._ladrillos:
                self._ladrillos[id].actualizar(valor)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    ventana = Ventana()
    ventana.show()
    sys.exit(app.exec())
