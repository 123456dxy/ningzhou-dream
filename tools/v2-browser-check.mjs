// Optional real-browser acceptance test. npm install --no-save playwright, or use a
// runtime with Playwright already available. No dependency is shipped to players.
import {spawn} from 'node:child_process';
import {createServer} from 'node:net';
import {mkdtemp,mkdir,rm,writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {chromium}=require('playwright');
const repo=resolve(fileURLToPath(new URL('..',import.meta.url)));
const data=await mkdtemp(join(tmpdir(),'ningzhou-v2-browser-'));
const out=resolve(process.env.V2_QA_OUT || join(repo,'..','ningzhou-v2-validation','browser'));
await mkdir(out,{recursive:true});
const reservation=createServer();
await new Promise((resolve,reject)=>{reservation.once('error',reject);reservation.listen(Number(process.env.V2_QA_PORT||0),'127.0.0.1',resolve);});
const port=reservation.address().port;
await new Promise(resolve=>reservation.close(resolve));
const base=`http://127.0.0.1:${port}`;
const server=spawn(process.env.NZ_WEB_BINARY||join(repo,'build-v2','ningzhou_web'),['--host','127.0.0.1','--port',String(port),'--web',join(repo,'web'),'--data',data,'--no-open'],{stdio:['ignore','pipe','pipe']});
let serverLog='';server.stdout.on('data',d=>serverLog+=d);server.stderr.on('data',d=>serverLog+=d);
let browser;
const results=[],errors=[];
const check=(ok,label)=>{assert.ok(ok,label);results.push(label);};
try{
  let ready=false;
  for(let i=0;i<100;i++){if(server.exitCode!==null||server.signalCode)throw new Error('Disposable server exited before startup');try{await fetch(base+'/api/whoami');ready=true;break;}catch{await new Promise(r=>setTimeout(r,50));}}
  assert.ok(ready,'disposable server must start before browser QA');
  browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
  const context=await browser.newContext({viewport:{width:390,height:844}}),page=await context.newPage();
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'/v2.html');await page.locator('#login-form').waitFor();
  await page.locator('[name=nickname]').fill('v2browser');await page.locator('[name=password]').fill('test-only-v2');await page.locator('#login-form button').click();
  await page.getByRole('button',{name:'开辟这一世',exact:true}).waitFor();check(true,'新道号登录注册');
  await page.getByRole('button',{name:'开辟这一世',exact:true}).click();await page.locator('#life-name').waitFor();await page.locator('#life-name').fill('掌中山河');
  await page.getByRole('button',{name:'开始这一世',exact:true}).click();await page.getByRole('heading',{name:'仙途日常'}).waitFor();check(true,'建号与服务端天赋校验');
  await page.screenshot({path:join(out,'390-home.png'),fullPage:true});
  const readState=()=>page.evaluate(async()=>await(await fetch('/api/state')).json());
  const before=await readState();
  await page.getByRole('button',{name:'推进 1 天',exact:true}).click();await page.waitForFunction(()=>document.body.getAttribute('aria-busy')==='false');
  const after=await readState();check(after.state.clock.day!==before.state.clock.day||!!after.state.pending||!!after.state.battle,'真实日常推进改变服务端状态');
  if(after.state.pending){await page.locator('#sheet [data-command="choose"]:not([disabled])').first().click();await page.waitForFunction(()=>document.body.getAttribute('aria-busy')==='false');}
  await page.getByRole('button',{name:'山河',exact:true}).click();await page.locator('#map-detail').waitFor();
  await page.getByRole('button',{name:'选择目的地',exact:true}).click();
  await page.locator('#sheet [data-ui="map-focus"]').filter({hasText:'天星城'}).click();await page.screenshot({path:join(out,'390-map.png'),fullPage:true});
  const travel=page.locator('[data-command="travel"]:not([disabled])').first();check(await travel.count()>0,'地图显示可用引擎报价');await travel.click();await page.waitForFunction(()=>document.body.getAttribute('aria-busy')==='false');
  const traveling=await readState();check(traveling.state.job?.type==='travel'||traveling.state.player.location==='tianxing'||traveling.state.pending||traveling.state.battle,'启程产生真实行程/中断状态');
  // Complete in-progress job through UI controls, resolving events with visible choices.
  for(let i=0;i<15;i++){const s=(await readState()).state;if(s.pending){await page.locator('#sheet [data-command="choose"]:not([disabled])').first().click();}else if(s.battle){await page.locator('[data-command="battle/turn"][data-body*=escape]').click();}else if(s.job){await page.locator('[data-command="advance"]').first().click();}else break;await page.waitForFunction(()=>document.body.getAttribute('aria-busy')==='false');}
  for(const [route,sub] of [['inventory','potions'],['inventory','gear'],['inventory','market'],['inventory','forge'],['arts','manuals'],['arts','spells'],['arts','daos'],['arts','cave'],['social','friends'],['social','sects'],['social','tianji']]){
    await page.goto(base+`/v2.html#/${route}/${sub}`);await page.locator('.bottom-nav').waitFor();await page.waitForTimeout(100);
    check(!(await page.locator('body').innerText()).includes('undefined'),`${route}/${sub}真实状态渲染`);
  }
  await page.screenshot({path:join(out,'390-tianji.png'),fullPage:true});
  // Back/forward preserves section routes.
  await page.getByRole('button',{name:'行囊',exact:true}).click();await page.getByRole('button',{name:'道法',exact:true}).click();await page.goBack();check(page.url().includes('/inventory'),'浏览器返回恢复主目的地');await page.goForward();check(page.url().includes('/arts'),'浏览器前进恢复主目的地');
  for(const width of [360,390,1280]){
    await page.setViewportSize({width,height:width===1280?900:844});
    for(const route of ['home','map','inventory/gear','inventory/market','arts/spells','social/friends','social/tianji']){
      await page.goto(base+'/v2.html#/'+route);await page.locator('.bottom-nav').waitFor();await page.waitForTimeout(100);
      const dims=await page.evaluate(()=>({scroll:document.documentElement.scrollWidth,client:document.documentElement.clientWidth}));check(dims.scroll<=dims.client,`${width}px ${route}无横向溢出`);
    }
    await page.goto(base+'/v2.html#/home');await page.locator('.bottom-nav').waitFor();await page.screenshot({path:join(out,`${width}-home.png`),fullPage:true});
  }
  await page.locator('[data-ui="settings"]').first().click();await page.getByRole('heading',{name:'存档与设置'}).waitFor();check(await page.locator('#sheet [data-command="save"]').count()===3,'三手动存档位可操作，自动位不可覆盖');
  await page.getByRole('button',{name:'关闭',exact:true}).click();check(!(await page.locator('#sheet').evaluate(e=>e.open)),'设置弹层关闭');
  await page.reload();await page.getByRole('heading',{name:'仙途日常'}).waitFor();check(true,'会话与人生刷新恢复');
  check(errors.length===0,'浏览器无未捕获异常');
  console.log(`v2 browser: ${results.length} checks passed`);await writeFile(join(out,'results.json'),JSON.stringify({checks:results,errors},null,2));
}catch(e){if(browser){const pages=browser.contexts().flatMap(c=>c.pages());if(pages[0]){await pages[0].screenshot({path:join(out,'failure.png'),fullPage:true}).catch(()=>{});await writeFile(join(out,'failure.html'),await pages[0].content()).catch(()=>{});}}console.error(e);console.error('Page errors:',errors);process.exitCode=1;}
finally{await browser?.close();server.kill('SIGTERM');await writeFile(join(out,'server.log'),serverLog);await rm(data,{recursive:true,force:true});}
