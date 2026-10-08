import { parentPort } from 'node:worker_threads';
globalThis.self=globalThis;
globalThis.postMessage=message=>parentPort.postMessage(message);
await import('../src/meta-worker.js');
parentPort.on('message',data=>self.onmessage({data}));
