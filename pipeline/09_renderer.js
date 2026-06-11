#!/usr/bin/env node
// ===========================================================================
// STAGE 6 of the cathedral: THE RENDERER  (language: Node.js)
// ---------------------------------------------------------------------------
// Takes the consensus value (an exact rational in canonical form: an integer, a
// terminating decimal, or a reduced fraction p/q) and renders it: the canonical
// value, the exact fraction, a 50-place decimal approximation, and — for whole
// numbers in range — Roman numerals and English words. It re-derives the
// canonical form from the parsed fraction and refuses to emit if they disagree.
// ===========================================================================
'use strict';

const verdict = process.argv[2];

function gcd(a, b) { a = a < 0n ? -a : a; b = b < 0n ? -b : b; while (b) { [a, b] = [b, a % b]; } return a; }

// parse canonical string -> [num, den] (reduced, den > 0)
function parseVal(s) {
  if (s.includes('/')) {
    const [p, q] = s.split('/');
    return reduce(BigInt(p), BigInt(q));
  }
  if (s.includes('.')) {
    let neg = s.startsWith('-'); let t = neg ? s.slice(1) : s;
    const [ip, frac] = t.split('.');
    let num = BigInt((ip || '0') + frac);
    const den = 10n ** BigInt(frac.length);
    if (neg) num = -num;
    return reduce(num, den);
  }
  return [BigInt(s), 1n];
}
function reduce(num, den) {
  if (den < 0n) { num = -num; den = -den; }
  if (num === 0n) return [0n, 1n];
  const g = gcd(num, den);
  return [num / g, den / g];
}

function canon(num, den) {
  if (num === 0n) return '0';
  if (den < 0n) { num = -num; den = -den; }
  if (den === 1n) return num.toString();
  let q = den, a = 0n, b = 0n;
  while (q % 2n === 0n) { q /= 2n; a++; }
  while (q % 5n === 0n) { q /= 5n; b++; }
  if (q === 1n) {
    const k = a > b ? a : b;
    const scale = (10n ** k) / den;
    const N = num * scale;
    const neg = N < 0n;
    const s = (N < 0n ? -N : N).toString();
    const kk = Number(k);
    let intp, frac;
    if (s.length <= kk) { intp = '0'; frac = '0'.repeat(kk - s.length) + s; }
    else { intp = s.slice(0, s.length - kk); frac = s.slice(s.length - kk); }
    frac = frac.replace(/0+$/, '');
    return (neg ? '-' : '') + intp + (frac ? '.' + frac : '');
  }
  return num.toString() + '/' + den.toString();
}

// decimal approximation, up to `places` fractional digits (truncated)
function approxDecimal(num, den, places) {
  const neg = (num < 0n) !== (den < 0n);
  let n = num < 0n ? -num : num;
  const d = den < 0n ? -den : den;
  const ip = n / d;
  let rem = n % d;
  if (rem === 0n) return (neg && ip !== 0n ? '-' : '') + ip.toString();
  let digits = '';
  let truncated = false;
  for (let i = 0; i < places; i++) {
    rem *= 10n;
    digits += (rem / d).toString();
    rem %= d;
    if (rem === 0n) break;
  }
  if (rem !== 0n) truncated = true;
  return (neg ? '-' : '') + ip.toString() + '.' + digits + (truncated ? '...' : '');
}

// ---- Roman numerals (classical range 1..3999) ----
function toRoman(n) {
  if (n <= 0n || n >= 4000n) return null;
  let x = Number(n);
  const table = [[1000, 'M'], [900, 'CM'], [500, 'D'], [400, 'CD'], [100, 'C'],
    [90, 'XC'], [50, 'L'], [40, 'XL'], [10, 'X'], [9, 'IX'], [5, 'V'], [4, 'IV'], [1, 'I']];
  let s = '';
  for (const [val, sym] of table) while (x >= val) { s += sym; x -= val; }
  return s;
}

// ---- English words ----
const ONES = ['zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight',
  'nine', 'ten', 'eleven', 'twelve', 'thirteen', 'fourteen', 'fifteen', 'sixteen',
  'seventeen', 'eighteen', 'nineteen'];
const TENS = ['', '', 'twenty', 'thirty', 'forty', 'fifty', 'sixty', 'seventy', 'eighty', 'ninety'];
const SCALES = ['', 'thousand', 'million', 'billion', 'trillion', 'quadrillion', 'quintillion'];
function threeWords(n) {
  const p = [];
  if (n >= 100) { p.push(ONES[Math.floor(n / 100)], 'hundred'); n %= 100; }
  if (n >= 20) { p.push(TENS[Math.floor(n / 10)]); if (n % 10) p.push(ONES[n % 10]); }
  else if (n > 0) p.push(ONES[n]);
  return p.join(' ');
}
function toWords(n) {
  if (n === 0n) return 'zero';
  const neg = n < 0n; let x = neg ? -n : n;
  const groups = [];
  while (x > 0n) { groups.push(Number(x % 1000n)); x /= 1000n; }
  if (groups.length > SCALES.length) return null;
  const parts = [];
  for (let i = groups.length - 1; i >= 0; i--) {
    if (groups[i] === 0) continue;
    let w = threeWords(groups[i]);
    if (SCALES[i]) w += ' ' + SCALES[i];
    parts.push(w);
  }
  return (neg ? 'negative ' : '') + parts.join(' ');
}

const [num, den] = parseVal(verdict);

if (canon(num, den) !== verdict) {
  console.error(`RENDER VERIFICATION FAILED: ${verdict} != ${canon(num, den)}`);
  process.exit(7);
}

const isInt = den === 1n;
const roman = isInt ? toRoman(num) : null;
const words = isInt ? toWords(num) : null;

console.log(JSON.stringify({
  value: verdict,
  exact: den === 1n ? num.toString() : `${num}/${den}`,
  approx: approxDecimal(num, den, 50),
  roman,
  words,
  verified: true,
}));
