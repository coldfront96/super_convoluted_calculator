#!/usr/bin/env ruby
# frozen_string_literal: true
# ============================================================================
# STAGE 3 of the cathedral: THE COMPILER  (language: Ruby)
# ----------------------------------------------------------------------------
# Reads the AST as JSON from STDIN, walks it post-order (a Visitor in spirit),
# and LOWERS it to bytecode for a stack machine of our own invention. The
# resulting assembly is then base64-encoded for "transport", because shipping
# plaintext between stages would simply not be convoluted enough.
# ============================================================================
require 'json'
require 'base64'

def emit(node, out)
  if node.key?('int')
    out << "PUSH #{node['int']}"
  elsif node['op'] == 'neg'
    emit(node['x'], out)
    out << 'NEG'
  else
    emit(node['l'], out)
    emit(node['r'], out)
    out << {
      'add' => 'ADD', 'sub' => 'SUB', 'mul' => 'MUL',
      'div' => 'DIV', 'mod' => 'MOD'
    }.fetch(node['op'])
  end
end

ast = JSON.parse($stdin.read)
program = []
emit(ast, program)
bytecode = program.join("\n") + "\n"

# Strict base64 (no embedded newlines) so the bash transport layer can pipe it
# straight into `base64 -d` without ceremony-breaking surprises.
print Base64.strict_encode64(bytecode)
