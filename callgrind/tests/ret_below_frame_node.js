// Run under --always-sparkplug: every JS function starts by calling
// Builtins_BaselineOutOfLinePrologue, which returns with SP below the SP it
// was called with.

function leaf(x) {
  return (x * 31 + 7) % 1000003;
}

function mid(x) {
  let sum = 0;
  for (let i = 0; i < 5; i++) sum += leaf(x + i);
  return sum;
}

function outer(n) {
  let sum = 0;
  for (let i = 0; i < n; i++) sum += mid(i);
  return sum;
}

let total = 0;
for (let k = 0; k < 200; k++) total += outer(20);
console.log(total);
