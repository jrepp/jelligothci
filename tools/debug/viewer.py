# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""A loopback-only readable event companion; one owner of the game debug wire."""
import argparse
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs
import serial
from jelli_debug import Client, SocketWire, DebugError


CHEATS = {name: 100 for name in ('fullness', 'energy', 'clean', 'fun', 'connection', 'bond')}
CHEATS.update(food=20, gifts=20)


def cheat_command(body):
    if not isinstance(body, dict):
        raise ValueError('Expected an object')
    name, value = body.get('name'), body.get('value')
    if name == 'heal' and value is None:
        return 'cheat heal'
    if not isinstance(name, str) or name not in CHEATS or type(value) is not int:
        raise ValueError('Unknown cheat or non-integer value')
    if not 0 <= value <= CHEATS[name]:
        raise ValueError(f'{name}: use 0–{CHEATS[name]}')
    return f'cheat {name} {value}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument('--socket')
    transport.add_argument('--port')
    parser.add_argument('--http-port', type=int, default=8765)
    args = parser.parse_args()
    if args.port:
        wire = serial.Serial(port=None, baudrate=115200, timeout=.05, write_timeout=3)
        wire.dtr = wire.rts = True
        wire.port = args.port
        wire.open()
    else:
        wire = SocketWire(args.socket, 3)
    client = Client(wire)
    origin = f'http://127.0.0.1:{args.http_port}'

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def respond(self, value, status=200):
            data = json.dumps(value).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            url = urlparse(self.path)
            if url.path == '/':
                data = Path(__file__).with_name('viewer.html').read_bytes()
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
            if url.path != '/api/poll':
                self.respond({'error': 'Not found'}, 404)
                return
            try:
                cursor = int(parse_qs(url.query).get('after', ['0'])[0])
                if not 0 <= cursor <= 0xffffffff:
                    raise ValueError('Invalid cursor')
                reply = client.request(f'events {cursor}')
                # Two bounded batches max. No frame data or animation telemetry.
                if reply['more']:
                    extra = client.request(f"events {reply['next']}")
                    reply['events'].extend(extra['events'])
                    reply['next'] = extra['next']
                    reply['more'] = extra['more']
                reply['state'] = client.state()
                self.respond(reply)
            except (ValueError, DebugError, OSError) as exc:
                self.respond({'error': str(exc)}, 503)

        def do_POST(self):
            # Only controls from our own loopback page; no arbitrary wire command endpoint.
            if self.headers.get('Origin') != origin or self.path not in ('/api/press', '/api/cheat'):
                self.respond({'error': 'Origin or path rejected'}, 403)
                return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= 128:
                    raise ValueError('Invalid body size')
                body = json.loads(self.rfile.read(size))
                if self.path == '/api/cheat':
                    self.respond(client.request(cheat_command(body)))
                    return
                if not isinstance(body, dict):
                    raise ValueError('Expected an object')
                button = body.get('button')
                if not isinstance(button, str) or len(button) > 32:
                    raise ValueError('Invalid button')
                self.respond(client.press(button))
            except (ValueError, DebugError, OSError) as exc:
                self.respond({'error': str(exc)}, 400)

    server = HTTPServer(('127.0.0.1', args.http_port), Handler)
    print(f'Event companion: {origin}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        wire.close()


if __name__ == '__main__':
    main()
