"""Emulador de la valvula remota (Actividad 3) que habla protocolo UNER por TCP.

Es el SERVIDOR: el ESP32 se conecta como cliente.
  python emulador_valvula.py [puerto]          (por defecto 5000)
En la pagina wifi.html del ESP poner como destino la IP de esta PC y el puerto.

Trama UNER: 'U' 'N' 'E' 'R' LEN ':' CARGA CHK   (LEN = len(CARGA) + 1, CHK = XOR de todo lo anterior)
Comandos:
  F0          -> F0 0D (controlable)
  D0          -> D0 pos
  D1 pos      -> D1 pos 0D   y despues se mueve de a 1 % cada 50 ms; al llegar manda D2 pos
  A0..AF ...  -> la misma trama (eco): para los ejemplos de tipos de implementacionTcpUner.c
Opciones (tecla + Enter en esta consola):
  m  mudo: deja de contestar (para probar el latido de 3 s en el ESP)
  q  salir
"""
import socket
import sys
import threading
import time

PUERTO = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
mudo = False


def armar(carga):
    trama = bytes(b'UNER') + bytes([len(carga) + 1]) + b':' + bytes(carga)
    chk = 0
    for b in trama:
        chk ^= b
    return trama + bytes([chk])


class Receptor:
    """La misma maquina de estados que uner.c, en Python."""

    def __init__(self):
        self.estado = 0
        self.carga = []

    def byte(self, b):
        e = self.estado
        if e == 1 and b == ord('N') or e == 2 and b == ord('E') or e == 3 and b == ord('R'):
            self.chk ^= b
            self.estado += 1
            return None
        if e == 4 and 1 <= b <= 65:
            self.chk ^= b
            self.esperados = b - 1
            self.estado = 5
            return None
        if e == 5 and b == ord(':'):
            self.chk ^= b
            self.carga = []
            self.estado = 6 if self.esperados > 0 else 7
            return None
        if e == 6:
            self.chk ^= b
            self.carga.append(b)
            if len(self.carga) == self.esperados:
                self.estado = 7
            return None
        if e == 7:
            self.estado = 0
            return self.carga if b == self.chk else None
        if b == ord('U'):
            self.chk = b
            self.estado = 1
        else:
            self.estado = 0
        return None


def atender(conn, dir_cliente):
    print(f'ESP conectado desde {dir_cliente}')
    conn.settimeout(0.05)
    rx = Receptor()
    pos = 0
    objetivo = 0
    ultimo_paso = time.time()
    pedidos_d0 = 0
    try:
        while True:
            try:
                datos = conn.recv(256)
                if not datos:
                    break
            except socket.timeout:
                datos = b''

            for b in datos:
                carga = rx.byte(b)
                if carga is None or not carga:
                    continue
                cmd = carga[0]
                if mudo:
                    continue
                if cmd == 0xF0:
                    print('<- F0 (controlable?)  -> F0 0D')
                    conn.sendall(armar([0xF0, 0x0D]))
                elif cmd == 0xD0:
                    pedidos_d0 += 1
                    conn.sendall(armar([0xD0, pos]))
                elif cmd == 0xD1 and len(carga) >= 2:
                    ok = carga[1] <= 100
                    print(f'<- D1 {carga[1]}  -> D1 {carga[1]} {"0D" if ok else "FF"}')
                    conn.sendall(armar([0xD1, carga[1], 0x0D if ok else 0xFF]))
                    if ok:
                        objetivo = carga[1]
                elif 0xA0 <= cmd <= 0xAF:
                    print(f'<- {cmd:02X} {bytes(carga[1:]).hex(" ")}  -> eco')
                    conn.sendall(armar(carga))
                else:
                    print(f'<- comando desconocido {carga}')

            # Mover la valvula de a 1 % cada 50 ms
            if time.time() - ultimo_paso >= 0.05 and pos != objetivo:
                ultimo_paso = time.time()
                pos += 1 if objetivo > pos else -1
                if pos == objetivo and not mudo:
                    print(f'llego a {pos}  -> D2 {pos}')
                    conn.sendall(armar([0xD2, pos]))
    except (ConnectionResetError, BrokenPipeError, OSError) as e:
        print('conexion cortada:', e)
    conn.close()
    print(f'ESP desconectado (atendio {pedidos_d0} pedidos D0)')


def consola():
    global mudo
    for linea in sys.stdin:
        linea = linea.strip()
        if linea == 'm':
            mudo = not mudo
            print('MUDO' if mudo else 'contestando')
        elif linea == 'q':
            import os
            os._exit(0)


if __name__ == '__main__':
    threading.Thread(target=consola, daemon=True).start()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('0.0.0.0', PUERTO))
    srv.listen(1)
    print(f'Valvula escuchando en el puerto {PUERTO} (m = mudo, q = salir)')
    while True:
        conn, dir_cliente = srv.accept()
        atender(conn, dir_cliente)
