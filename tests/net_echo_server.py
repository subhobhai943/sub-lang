#!/usr/bin/env python3
"""Echo server for tests/run_tests.py's networking check: answers any
request with 'ECHO:' + the request body. Takes the port as argv[1]."""
import http.server, sys

class Handler(http.server.BaseHTTPRequestHandler):
    def _reply(self):
        n = int(self.headers.get('Content-Length', 0) or 0)
        data = self.rfile.read(n) if n else b''
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'ECHO:' + data)
    do_GET = do_POST = do_PUT = _reply
    def log_message(self, *args): pass

http.server.HTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
