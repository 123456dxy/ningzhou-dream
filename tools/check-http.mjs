import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import { spawn } from 'node:child_process';
const port = 18108, root = process.cwd(), testDir = path.join(root, 'build', 'http-check');
fs.mkdirSync(testDir, { recursive: true });
const exe = path.join(root, 'build', 'ningzhou_web.exe');
const server = spawn(exe, ['--port', String(port), '--no-open', '--web', path.join(root, 'web')], {cwd:testDir, windowsHide:true, stdio:'ignore'});
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function state() { const r=await fetch('http://127.0.0.1:'+port+'/api/state',{signal:AbortSignal.timeout(6000)}); return await r.text(); }
async function raw(chunks) {
  return await new Promise((resolve, reject) => {
    const sock = net.createConnection({host:'127.0.0.1',port}); let out='';
    const timeout=setTimeout(()=>{sock.destroy();reject(new Error('socket timeout'));},7000);
    sock.on('data',d=>{out+=d;}); sock.on('error',reject); sock.on('close',()=>{clearTimeout(timeout);resolve(out);});
    sock.on('connect',async()=>{ for(const [text,pause=0] of chunks) { if(pause) await delay(pause); if(!sock.destroyed) sock.write(text); } });
  });
}
try {
  for(let i=0;i<30;i++) { try { await state(); break; } catch { await delay(100); } }
  const before = await state();
  const idle = net.createConnection({host:'127.0.0.1',port}); idle.on('error',()=>{});
  await new Promise(resolve=>idle.once('connect',resolve)); const start=Date.now();
  const idleState=await state(); idle.destroy();
  assert.equal(idleState,before); assert.ok(Date.now()-start<4000,'idle preconnect must release server');
  console.log('✓ 空闲预连接限时关闭，正常游戏请求继续响应');
  const body=JSON.stringify({name:'短正文不得执行',seed:12345});
  const short=await raw([['POST /api/new HTTP/1.1\r\nHost: localhost\r\nContent-Length: '+(body.length+40)+'\r\n\r\n'+body]]);
  assert.match(short,/400 Bad Request/); assert.equal(await state(),before);
  console.log('✓ 未收到完整正文的请求不执行新建角色');
  const invalid=await raw([['POST /api/new HTTP/1.1\r\nContent-Length: 2junk\r\n\r\n{}']]);
  assert.match(invalid,/400 Bad Request/); assert.equal(await state(),before);
  console.log('✓ 非法正文长度不执行动作');
  const valid=await raw([['POST /api/arts/nodes HTTP/1.1\r\nContent-Length: 2\r\n\r\n{'],['}',200]]);
  assert.match(valid,/200 OK/); assert.equal(await state(),before);
  console.log('✓ 正常分段正文完整收到后处理');
} finally { server.kill(); }
