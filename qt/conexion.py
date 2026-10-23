"""Conexion TCP con la consola del ESP32 (microrl sobre Telnet).

Espejo de www/conexion.js: las mismas tres operaciones, con el mismo JSON.
    pedir_def()        -> senal def_recibida({"vars":[...]})
    pedir_valores()    -> senal valores_recibidos({"led1":1,...})
    set(id, valor)     -> cambia y pide los valores actualizados
En la web cada operacion devuelve su respuesta (fetch); aca la respuesta
llega despues por una senal, porque el socket es un flujo continuo.

La consola fue hecha para personas: el ESP32 manda negociacion Telnet, colores,
eco de lo que se escribe y el prompt. Esta clase limpia todo eso y se queda
con las lineas JSON (las que empiezan con '{') y los errores (las que empiezan
con 'ERROR: ' -> senal error_recibido, como el texto de error de la API web).

Manejo de la conexion:
- Estados: DESCONECTADO -> CONECTANDO -> CONECTADO.
- Si no conecta en TIMEOUT_CONECTAR_MS, se aborta.
- Latido: si no llega nada en LATIDO_S, se manda un Enter vacio (el ESP32
  responde con el prompt). Si sigue sin llegar nada en PERDIDA_S, se da la
  conexion por perdida (se cayo el WiFi, se reinicio el ESP32, etc.).
"""

import json
import re
import time

from PySide6.QtCore import QObject, QTimer, Signal
from PySide6.QtNetwork import QAbstractSocket, QTcpSocket

TIMEOUT_CONECTAR_MS = 3000
LATIDO_S = 3
PERDIDA_S = 6

DESCONECTADO = "desconectado"
CONECTANDO = "conectando"
CONECTADO = "conectado"

# Telnet (ver telnet.c del ESP32)
IAC, SB, SE = 255, 250, 240
WILL, DONT = 251, 254

# Secuencias de escape ANSI: colores, mover cursor, borrar linea ("\x1b[32m", "\x1b[K")
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

PREFIJO_ERROR = "ERROR: "      # asi empiezan los errores de la consola (main.c)

ERRORES = {
    QAbstractSocket.SocketError.ConnectionRefusedError: "Conexion rechazada (¿puerto correcto?)",
    QAbstractSocket.SocketError.HostNotFoundError: "IP no encontrada",
    QAbstractSocket.SocketError.RemoteHostClosedError: "El ESP32 cerro la conexion",
    QAbstractSocket.SocketError.SocketTimeoutError: "Tiempo de espera agotado",
    QAbstractSocket.SocketError.NetworkError: "Error de red (¿WiFi conectado?)",
}


class Conexion(QObject):
    estado_cambiado = Signal(str, str)  # (estado, mensaje para mostrar)
    def_recibida = Signal(dict)         # {"vars":[...]}
    valores_recibidos = Signal(dict)    # {"led1":1,...}
    error_recibido = Signal(str)        # "valor invalido para consigna: 60"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.estado = DESCONECTADO
        self._sock = QTcpSocket(self)
        self._sock.connected.connect(self._al_conectar)
        self._sock.disconnected.connect(self._al_desconectar)
        self._sock.errorOccurred.connect(self._al_error)
        self._sock.readyRead.connect(self._al_recibir)

        self._timeout = QTimer(self, singleShot=True, interval=TIMEOUT_CONECTAR_MS)
        self._timeout.timeout.connect(self._al_timeout)
        self._latido = QTimer(self, interval=1000)
        self._latido.timeout.connect(self._revisar_latido)

        self._motivo = ""       # por que se corto, para mostrarlo al final
        self._telnet = 0        # estado del filtro Telnet
        self._texto = ""        # texto recibido que todavia no forma una linea
        self._ultimo_rx = 0.0

    # ---------- API ----------

    def conectar(self, ip: str, puerto: int):
        if self.estado != DESCONECTADO:
            return
        self._motivo = ""
        self._telnet = 0
        self._texto = ""
        self._cambiar(CONECTANDO, f"Conectando a {ip}:{puerto}...")
        self._timeout.start()
        self._sock.connectToHost(ip, puerto)

    def desconectar(self, motivo: str = "Desconectado"):
        if self.estado == DESCONECTADO:
            return
        self._motivo = motivo
        self._sock.abort()          # corta ya, sin esperar al ESP32
        self._terminar()

    def pedir_def(self):
        self._enviar("def")

    def pedir_valores(self):
        self._enviar("valores")

    def set(self, id: str, valor):
        self._enviar(f"{id} {valor}")
        self.pedir_valores()            # como /api/set: devolver los valores actualizados

    def _enviar(self, comando: str):
        """Manda un comando de consola (sin el Enter)."""
        if self.estado == CONECTADO:
            self._sock.write((comando + "\r\n").encode())

    # ---------- eventos del socket ----------

    def _al_conectar(self):
        self._timeout.stop()
        self._ultimo_rx = time.monotonic()
        self._latido.start()
        self._cambiar(CONECTADO, f"Conectado a {self._sock.peerName()}:{self._sock.peerPort()}")

    def _al_desconectar(self):
        if not self._motivo:
            self._motivo = "El ESP32 cerro la conexion"
        self._terminar()

    def _al_error(self, error):
        if not self._motivo:
            self._motivo = ERRORES.get(error, self._sock.errorString())
        if self.estado == CONECTANDO:   # en este estado no llega 'disconnected'
            self._terminar()

    def _al_timeout(self):
        self.desconectar("El ESP32 no responde (¿IP correcta? ¿misma red?)")

    def _revisar_latido(self):
        quieto = time.monotonic() - self._ultimo_rx
        if quieto > PERDIDA_S:
            self.desconectar("Se perdio la conexion con el ESP32")
        elif quieto > LATIDO_S:
            self._sock.write(b"\r\n")   # Enter vacio: el ESP32 contesta con el prompt

    def _al_recibir(self):
        self._ultimo_rx = time.monotonic()
        texto = self._filtrar_telnet(bytes(self._sock.readAll()))
        self._texto += ANSI.sub("", texto.decode(errors="replace"))

        *lineas, self._texto = self._texto.split("\n")   # lo ultimo queda incompleto
        for linea in lineas:
            linea = linea.strip("\r ")
            if "Consola llena" in linea:
                self.desconectar("Consola llena: demasiados clientes conectados al ESP32")
                return
            if linea.startswith(PREFIJO_ERROR):
                self.error_recibido.emit(linea[len(PREFIJO_ERROR):])
                continue
            if not linea.startswith("{"):
                continue                # eco, prompt, mensajes para personas
            try:
                datos = json.loads(linea)
            except ValueError:
                continue
            if "vars" in datos:
                self.def_recibida.emit(datos)
            else:
                self.valores_recibidos.emit(datos)

    # ---------- auxiliares ----------

    def _filtrar_telnet(self, datos: bytes) -> bytes:
        """Saca los comandos Telnet (IAC ...). Misma maquina de estados que telnet.c."""
        DATOS, COMANDO, OPCION, SUB, SUB_IAC = range(5)
        salida = bytearray()
        for c in datos:
            e = self._telnet
            if e == DATOS:
                if c == IAC:
                    self._telnet = COMANDO
                else:
                    salida.append(c)
            elif e == COMANDO:
                if c == IAC:
                    salida.append(c)
                    self._telnet = DATOS
                elif WILL <= c <= DONT:
                    self._telnet = OPCION
                elif c == SB:
                    self._telnet = SUB
                else:
                    self._telnet = DATOS
            elif e == OPCION:
                self._telnet = DATOS
            elif e == SUB:
                if c == IAC:
                    self._telnet = SUB_IAC
            else:  # SUB_IAC
                self._telnet = DATOS if c == SE else SUB
        return bytes(salida)

    def _terminar(self):
        self._timeout.stop()
        self._latido.stop()
        if self.estado != DESCONECTADO:
            self._cambiar(DESCONECTADO, self._motivo or "Desconectado")

    def _cambiar(self, estado: str, mensaje: str):
        self.estado = estado
        self.estado_cambiado.emit(estado, mensaje)
