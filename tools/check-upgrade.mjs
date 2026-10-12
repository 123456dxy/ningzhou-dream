// 隔离验证真实旧存档的升级；不连接正式游戏端口。
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
const root=process.cwd(), port=18109;
const snapshot=JSON.parse(fs.readFileSync(process.argv[2] || 'build/saves/before-npc-map-upgrade.json','utf8').replace(/^\uFEFF/,''));
const before=JSON.parse(snapshot.game);
const cwd=path.join(root,'build','upgrade-check'); fs.mkdirSync(cwd,{recursive:true});
const child=spawn(path.join(root,'build','ningzhou_web.exe'),['--port',String(port),'--no-open','--web',path.join(root,'web')],{cwd,windowsHide:true,stdio:'ignore'});
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function request(route,body) {
  const r=await fetch(`http://127.0.0.1:${port}/api/${route}`,body ? {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body),signal:AbortSignal.timeout(10000)} : {signal:AbortSignal.timeout(10000)});
  const data=await r.json(); assert.equal(r.status,200,`${route}: ${data.error}`); return data;
}
try {
  for(let i=0;i<30;i++){try{await request('state');break;}catch{await delay(100);}}
  await request('import',{game:snapshot.game});
  const after=JSON.parse((await request('export')).game);
  for(const key of ['name','gender','age','lifespan','stage','xp','health','stones','herbs','pills','cores','location','sect']) assert.deepEqual(after.player[key],before.player[key],key);
  assert.deepEqual(after.clock,before.clock); assert.equal(after.rng,before.rng); assert.equal(after.npcs.length,before.npcs.length);
  for(const key of ['herbs','materials','potions','equipped']) assert.deepEqual(after.player.arts[key],before.player.arts[key],`arts.${key}`);
  assert.equal(after.combatRulesVersion,3); assert.ok(after.player.arts.knownSpells.length>=before.player.arts.spells.length);
  const state=(await request('state')).state; assert.equal(state.locations.length,28); assert.ok(state.player.escapeSpeed>0);
  const stable=JSON.stringify(after);
  await request('import',{game:JSON.stringify(after)});
  assert.equal(JSON.stringify(JSON.parse((await request('export')).game)),stable,'新版重复导入不重复发放迁移收益');
  for(const image of [state.player.portrait,'/assets/portraits/beast-4.webp','/assets/map/locations/tianmo.webp']) {
    const r=await fetch(`http://127.0.0.1:${port}${image}`,{signal:AbortSignal.timeout(10000)}); assert.equal(r.status,200); assert.ok((await r.arrayBuffer()).byteLength>10000);
  }
  console.log(`✓ 真实旧存档升级保留 ${before.player.name}／第${before.clock.days}天／${before.player.stones}灵石，人口、库存、日历与随机序列不变`);
  console.log('✓ 新版再次导入稳定；28地点、遁速与人物/妖兽/地点图片实际可用');
} finally { child.kill(); }
