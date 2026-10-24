"""Conexion con el ESP32 por HTTP.
Espejo de www/conexion.js: las mismas tres operaciones, con el mismo JSON.
    definicion()     -> {"vars":[{"id":"led1","tipo":"dout","etq":"LED 1","fila":0,"col":0},...]}
    valores()        -> {"led1":1,"led2":0,...}
    set(id, valor)   -> valores actualizados
En la web cada operacion devuelve una promesa (await); aca recibe dos funciones:
    listo(datos)     si salio bien, con el JSON ya convertido
    fallo(mensaje)   si fallo: "Sin conexion con el ESP32" o el motivo que respondio el ESP32
"""

import json

from PySide6.QtCore import QObject, QUrl
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

TIMEOUT_MS = 3000


class Conexion(QObject):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._red = QNetworkAccessManager(self)
        self.ip = ""

    def definicion(self, listo, fallo):
        self._pedir_json("/api/def", None, listo, fallo)

    def valores(self, listo, fallo):
        self._pedir_json("/api/valores", None, listo, fallo)

    def set(self, id: str, valor, listo, fallo):
        self._pedir_json("/api/set", {"id": id, "valor": str(valor)}, listo, fallo)

    def _pedir_json(self, ruta: str, cuerpo, listo, fallo):
        """GET si cuerpo es None; si no, POST con el cuerpo en JSON"""
        pedido = QNetworkRequest(QUrl(f"http://{self.ip}{ruta}"))
        pedido.setTransferTimeout(TIMEOUT_MS)
        if cuerpo is None:
            respuesta = self._red.get(pedido)
        else:
            pedido.setHeader(QNetworkRequest.KnownHeaders.ContentTypeHeader, "application/json")
            respuesta = self._red.post(pedido, json.dumps(cuerpo).encode())
        respuesta.finished.connect(lambda: self._al_terminar(respuesta, listo, fallo))

    def _al_terminar(self, respuesta, listo, fallo):
        respuesta.deleteLater()
        texto = bytes(respuesta.readAll()).decode(errors="replace")
        if respuesta.error() == QNetworkReply.NetworkError.NoError:
            listo(json.loads(texto))
        elif respuesta.attribute(QNetworkRequest.Attribute.HttpStatusCodeAttribute):
            fallo(texto)        # el ESP32 respondio con error (400, 403, 404): su motivo
        else:
            fallo("Sin conexion con el ESP32")
