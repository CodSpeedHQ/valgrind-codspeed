// callerA only calls calleeX and callerB only calls calleeY, both through the
// same V8 call builtins. With --separate-callers=all those builtins are
// separated per JS caller, so no builtin context reaches both callees.

function calleeX(v) {
  return v + 1;
}

function calleeY(v) {
  return v + 2;
}

function callerA(n) {
  let sum = 0;
  for (let i = 0; i < n; i++) sum += calleeX(i);
  return sum;
}

function callerB(n) {
  let sum = 0;
  for (let i = 0; i < n; i++) sum += calleeY(i);
  return sum;
}

let total = 0;
for (let k = 0; k < 200; k++) total += callerA(20) + callerB(20);
console.log(total);
