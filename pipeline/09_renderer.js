#!/usr/bin/env node
// ===========================================================================
// STAGE 6 of the cathedral: THE RENDERER  (language: Node.js)
// ---------------------------------------------------------------------------
// Takes the consensus integer and renders it redundantly: decimal, hex, 64-bit
// two's-complement binary, Roman numerals, and English words. It then PARSES the
// Roman and English forms back to integers and asserts they round-trip to the
// original value. If any cross-check fails it refuses to emit (HARD_RULES #1).
// The decimal field is the headline answer; the rest is glorious ceremony.
// ===========================================================================
'use strict';

const value = BigInt(process.argv[2]);

// ---- two's-complement views ----
const TWO64 = 1n << 64n;
const u = ((value % TWO64) + TWO64) % TWO64;
const hex = '0x' + u.toString(16);
const binary = u.toString(2).padStart(64, '0');

// ---- Roman numerals (classical range 1..3999) ----
function toRoman(n) {
  if (n <= 0n || n >= 4000n) return null;
  let x = Number(n);
  const table = [
    [1000, 'M'], [900, 'CM'], [500, 'D'], [400, 'CD'],
    [100, 'C'], [90, 'XC'], [50, 'L'], [40, 'XL'],
    [10, 'X'], [9, 'IX'], [5, 'V'], [4, 'IV'], [1, 'I'],
  ];
  let s = '';
  for (const [val, sym] of table) {
    while (x >= val) { s += sym; x -= val; }
  }
  return s;
}
function fromRoman(s) {
  const m = { I: 1, V: 5, X: 10, L: 50, C: 100, D: 500, M: 1000 };
  let total = 0;
  for (let i = 0; i < s.length; i++) {
    const cur = m[s[i]];
    const nxt = m[s[i + 1]] || 0;
    total += cur < nxt ? -cur : cur;
  }
  return BigInt(total);
}

// ---- English words ----
const ONES = ['zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven',
  'eight', 'nine', 'ten', 'eleven', 'twelve', 'thirteen', 'fourteen',
  'fifteen', 'sixteen', 'seventeen', 'eighteen', 'nineteen'];
const TENS = ['', '', 'twenty', 'thirty', 'forty', 'fifty', 'sixty', 'seventy',
  'eighty', 'ninety'];
const SCALES = ['', 'thousand', 'million', 'billion', 'trillion',
  'quadrillion', 'quintillion'];

function threeWords(n) {
  const parts = [];
  if (n >= 100) { parts.push(ONES[Math.floor(n / 100)], 'hundred'); n %= 100; }
  if (n >= 20) {
    parts.push(TENS[Math.floor(n / 10)]);
    if (n % 10) parts.push(ONES[n % 10]);
  } else if (n > 0) {
    parts.push(ONES[n]);
  }
  return parts.join(' ');
}
function toWords(n) {
  if (n === 0n) return 'zero';
  const neg = n < 0n;
  let x = neg ? -n : n;
  const groups = [];
  while (x > 0n) { groups.push(Number(x % 1000n)); x /= 1000n; }
  const parts = [];
  for (let i = groups.length - 1; i >= 0; i--) {
    if (groups[i] === 0) continue;
    let w = threeWords(groups[i]);
    if (SCALES[i]) w += ' ' + SCALES[i];
    parts.push(w);
  }
  return (neg ? 'negative ' : '') + parts.join(' ');
}
function fromWords(str) {
  if (str === 'zero') return 0n;
  let toks = str.split(/\s+/);
  let neg = false;
  if (toks[0] === 'negative') { neg = true; toks = toks.slice(1); }
  const onesMap = {}; ONES.forEach((w, i) => { onesMap[w] = i; });
  const tensMap = {}; TENS.forEach((w, i) => { if (w) tensMap[w] = i * 10; });
  const scaleMap = {}; SCALES.forEach((w, i) => { if (w) scaleMap[w] = i; });
  let total = 0n, current = 0n;
  for (const t of toks) {
    if (t in onesMap) current += BigInt(onesMap[t]);
    else if (t in tensMap) current += BigInt(tensMap[t]);
    else if (t === 'hundred') current *= 100n;
    else if (t in scaleMap) { total += current * (1000n ** BigInt(scaleMap[t])); current = 0n; }
    else throw new Error('unparseable word: ' + t);
  }
  return neg ? -(total + current) : total + current;
}

// ---- render + cross-verify ----
const words = toWords(value);
const roman = toRoman(value);

let verified = true;
if (fromWords(words) !== value) verified = false;
if (roman !== null && fromRoman(roman) !== value) verified = false;

if (!verified) {
  console.error('RENDER VERIFICATION FAILED for value ' + value.toString());
  process.exit(7);
}

console.log(JSON.stringify({
  decimal: value.toString(),
  hex,
  binary,
  roman,
  words,
  verified,
}));
