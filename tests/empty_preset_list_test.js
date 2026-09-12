const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync(`${__dirname}/../resources/web/index.html`, 'utf8');
const start = html.indexOf('    function OnMessage(');
const end = html.indexOf('\n    }', start) + '\n    }'.length;
let received;
const context = {
  window: { atob }, Uint8Array, TextDecoder,
  UpdatePresetList: names => { received = names; }
};
vm.createContext(context);
vm.runInContext(html.slice(start, end), context);
// iPlug2's empty vector is formatted as '(null)' on macOS. A zero-length
// payload has no bytes to decode, regardless of the bridge's placeholder.
context.OnMessage(13, 0, '(null)');
assert.equal(JSON.stringify(received), '[]');
context.OnMessage(13, 4, btoa('Pad'));
assert.equal(JSON.stringify(received), '["Pad"]');
console.log('Empty and populated preset lists: PASS');
