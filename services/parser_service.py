#!/usr/bin/env python3
# Native HTTP service wrapping the Python parser stage.
import sys
import subprocess
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 7002
STAGE = ["python3", "pipeline/02_parser.py"]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, data, code=200):
        self.send_response(code)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/health":
            self._send(b"ok")
        else:
            self._send(b"", 404)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n)
        try:
            res = subprocess.run(STAGE, input=body, capture_output=True)
            if res.returncode != 0 and not res.stdout:
                self._send(res.stderr or b"stage failed", 500)
            else:
                self._send(res.stdout)
        except Exception as e:  # noqa
            self._send(str(e).encode(), 500)


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
