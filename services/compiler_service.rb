#!/usr/bin/env ruby
# Native HTTP service wrapping the Ruby compiler stage (raw TCPServer; single
# connection at a time, which is plenty for a sequential orchestrator).
require 'socket'

port = (ARGV[0] || 7003).to_i
server = TCPServer.new('127.0.0.1', port)

loop do
  client = server.accept
  begin
    request = client.gets
    clen = 0
    while (line = client.gets)
      break if line == "\r\n"
      clen = $1.to_i if line =~ /Content-Length:\s*(\d+)/i
    end

    if request && request.start_with?('GET /health')
      client.print "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok"
      next
    end

    body = clen > 0 ? client.read(clen) : ''
    out = IO.popen(['ruby', 'pipeline/03_compiler.rb'], 'r+') do |io|
      io.write(body)
      io.close_write
      io.read
    end
    client.print "HTTP/1.1 200 OK\r\nContent-Length: #{out.bytesize}\r\n" \
                 "Connection: close\r\n\r\n#{out}"
  rescue => e
    msg = "compiler error: #{e.message}"
    client.print "HTTP/1.1 500 Internal Server Error\r\nContent-Length: #{msg.bytesize}\r\n" \
                 "Connection: close\r\n\r\n#{msg}"
  ensure
    client.close
  end
end
