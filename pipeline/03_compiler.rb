#!/usr/bin/env ruby
# frozen_string_literal: true
# ============================================================================
# STAGE 3 of the cathedral: THE COMPILER  (language: Ruby)
# ----------------------------------------------------------------------------
# Reads the AST as JSON from STDIN, walks it post-order (a Visitor in spirit),
# and LOWERS it to bytecode for a stack machine of our own invention. Numeric
# literals (integers, decimals, scientific notation) are converted EXACTLY to a
# reduced rational num/den so the engines only ever parse "PUSH p/q". The result
# is base64-encoded for "transport", because shipping plaintext between stages
# would simply not be convoluted enough.
# ============================================================================
require 'json'
require 'base64'

# Exact decimal/scientific literal -> [num, den] (both Integer, reduced).
def literal_to_rational(s)
  m = s.downcase
  exp = 0
  if m.include?('e')
    base, e = m.split('e', 2)
    exp = e.to_i
    m = base
  end
  if m.include?('.')
    intp, frac = m.split('.', 2)
    frac ||= ''
  else
    intp = m
    frac = ''
  end
  intp = '0' if intp.empty?
  num = (intp + frac).to_i          # bignum
  den = 10**frac.length
  if exp >= 0
    num *= 10**exp
  else
    den *= 10**(-exp)
  end
  g = num.gcd(den)
  g = 1 if g.zero?
  [num / g, den / g]
end

def emit(node, out)
  if node.key?('num')
    n, d = literal_to_rational(node['num'])
    out << "PUSH #{n}/#{d}"
  elsif node.key?('const')
    out << "CONST #{node['const']}"
  elsif node.key?('func')
    emit(node['x'], out)
    out << "FUNC #{node['func']}"
  elsif node['op'] == 'neg'
    emit(node['x'], out)
    out << 'NEG'
  else
    emit(node['l'], out)
    emit(node['r'], out)
    out << {
      'add' => 'ADD', 'sub' => 'SUB', 'mul' => 'MUL', 'div' => 'DIV',
      'idiv' => 'IDIV', 'mod' => 'MOD', 'pow' => 'POW'
    }.fetch(node['op'])
  end
end

ast = JSON.parse($stdin.read)
program = []
emit(ast, program)
bytecode = program.join("\n") + "\n"

print Base64.strict_encode64(bytecode)
