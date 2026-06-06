#!/usr/bin/env node
// Native HTTP service wrapping the Node renderer stage.
'use strict';
const http = require('http');
const { execFileSync } = require('child_process');

const port = parseInt(process.argv[2] || '7030', 10);

http.createServer((req, res) => {
  if (req.method === 'GET' && req.url === '/health') {
    res.end('ok');
    return;
  }
  let body = '';
  req.on('data', (c) => { body += c; });
  req.on('end', () => {
    try {
      const out = execFileSync('node', ['pipeline/09_renderer.js', body.trim()]);
      res.end(out);
    } catch (e) {
      res.statusCode = 500;
      res.end('render failed');
    }
  });
}).listen(port, '127.0.0.1');
