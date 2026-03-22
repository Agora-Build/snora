import { createServer } from 'node:net';
import { encodeMessage, MessageDecoder } from '../../src/shared/ipc-protocol.js';
import type { IpcMessage } from '../../src/shared/types.js';

const socketPath = process.argv.find((arg, i) => process.argv[i - 1] === '--socket') ?? '/tmp/snora-test.sock';

const server = createServer((conn) => {
  const decoder = new MessageDecoder();
  conn.on('data', (data) => decoder.feed(data));

  decoder.on('message', (msg: IpcMessage) => {
    // Always ack
    conn.write(encodeMessage({ type: 'ack' }));

    if (msg.type === 'init') {
      conn.write(encodeMessage({ type: 'status', data: { reason: 'running' } }));
      process.stderr.write(JSON.stringify({ level: 'info', msg: 'Engine initialized' }) + '\n');
    } else if (msg.type === 'shutdown') {
      process.stderr.write(JSON.stringify({ level: 'info', msg: 'Engine shutting down' }) + '\n');
      setTimeout(() => {
        server.close();
        process.exit(0);
      }, 100);
    }
  });
});

server.listen(socketPath, () => {
  process.stderr.write(JSON.stringify({ level: 'info', msg: `Mock engine listening on ${socketPath}` }) + '\n');
});

process.on('SIGTERM', () => {
  server.close();
  process.exit(0);
});
