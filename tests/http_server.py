"""Loopback-only PUT endpoint for http_tests.cpp; never publishes real games."""
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_PUT(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if not all(key in body for key in ('host', 'port', 'profile_name', 'message')):
            self.send_error(400)
            return
        self.send_response(200)
        self.send_header('Content-Length', '0')
        self.end_headers()


ThreadingHTTPServer(('127.0.0.1', 18349), Handler).serve_forever()
