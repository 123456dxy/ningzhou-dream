// 明确模拟API的DOM验收服务器；仅验证页面点击、布局与权威字段消费，不替代C++接口测试。
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { join, extname, resolve } from 'node:path';
import vm from 'node:vm';
const args = process.argv.slice(2), root = resolve(args[args.indexOf('--root') + 1]);
const port = Number(args[args.indexOf('--port') + 1]);
if (!args.includes('--root') || !port || [8106,8107,25001].includes(port)) throw new Error('Fixture仅允许自有隔离端口');
const checkSource = readFileSync(join(root, 'tools/check-web.mjs'), 'utf8');
const literal = checkSource.slice(checkSource.indexOf('const state = {'), checkSource.indexOf('\nconst cityMap ='));
const holder = {}; vm.runInNewContext(literal + '\nglobalThis.fixture=state;', holder);
const initial = holder.fixture;
Object.assign(initial.player, { name: '桌面验收', apt: 100, insight: 80, luck: 40, body: 20, alchemy: 5, rank: '散修',xp:0,xpNeed:30,escapeSpeed:30,renown:0,
  talents: [{ id: 'poor', name: '家境清寒', desc: '出身家境清寒，获得额外构筑点数。' }], traits: ['poor'], combatReference: { hp: 120, hpMax: 150, mana: 3, manaRegen: 3, manaCap: 8 } });
Object.assign(initial.clock, { year: 0, days: 0, label: '入道第1年1月1日' });
Object.assign(initial.world, { npcCount: 0, weather: '晴', mineOwner: 'wuling', marketCash: 10000 });
Object.assign(initial.arts, { mods: { cult:1, attack:1, hp:1, mana:0 }, materials:{}, spells:[], spellList:[], knownSpells:[], manualList:[], potions:{}, used:{}, poison:0, heart:0, mandatorySpells:[] });
Object.assign(initial, { plan:'cultivate', plans:[['cultivate','闭关修炼'],['explore','游历寻缘'],['work','城中差事'],['rest','休养生息'],['gather','采集药材']].map(([id,name])=>({id,name,desc:'在当前地点安排'+name,preview:'当前收益由服务端状态提供'})),
  breakthrough:{ready:false,cost:100,chance:.6,next:'炼气中期'},bonds:{stories:{},stipendBalance:0}, adventure:{events:0,since:0,total:0,wins:0}, logs:[],
  stories:[{id:'letter',name:'未拆的家书',location:'wuling',days:3,available:true,done:false,desc:'客栈掌柜替你收着家书',opening:'纸上写着家中近况。',opt1Name:'回信报平安',opt1Effect:'心境+4，福缘+2',opt2Name:'收入旧卷',opt2Effect:'悟性+2'}],
  wildTasks:[{id:'wolf',name:'山林猎妖',desc:'野外斗法',days:3,fee:20,enabled:true}], renownInfo:{value:0,tier:'初入宁州',tierDesc:'声望尚在积累。',toNext:20,buyDiscount:0,sellBonus:0,sellLimit:8,marketCashCap:400,firstImpression:0,libraryDiscount:0,stipendBonus:0,masterRequirement:20,tiers:[],sources:[]}, tianji:null, ended:false,
  pillCatalog:[{id:'5001',name:'化淤丹',grade:1,effect:'回复气血20点',baseEffect:'回复气血20点',catalogEffect:'回复气血20点',usage:'用于恢复气血',enabled:true,basePoison:3,poisonAfter:3,poisonDelta:3},
    {id:'5002',name:'二阶补气丹',grade:2,effect:'恢复灵力2点',baseEffect:'恢复灵力2点',catalogEffect:'恢复灵力2点',enabled:false,reason:'尚未持有',basePoison:4,poisonAfter:4,poisonDelta:4}],
  herbCatalog:[{id:'silver',name:'银月花',grade:1},{id:'flow',name:'流莹草',grade:1},{id:'heart',name:'恒心草',grade:1}],
  materialCatalog:[{id:'iron',name:'锻剑铁',grade:1},{id:'jade',name:'岩玉',grade:1},{id:'wood',name:'桃木',grade:1}],
  dungeons:{sites:[],specials:[],intel:[
    {id:'leaf-3',kind:'herb',name:'灵叶草',grade:3,desc:'三阶灵药',stageName:'金丹初期',cost:30,enabled:true},
    {id:'ore-3',kind:'material',name:'三阶玄铁',grade:3,desc:'三阶炼器材料',stageName:'金丹初期',cost:30,enabled:true},
    {id:'flower-4',kind:'herb',name:'仙露草',grade:4,desc:'四阶灵药',stageName:'元婴初期',cost:40,enabled:true},
    {id:'crystal-4',kind:'material',name:'寒晶',grade:4,desc:'四阶矿晶',stageName:'元婴初期',cost:40,enabled:false,reason:'境界不足'},
  ]},
  brewOptions:[{id:'5001',name:'化淤丹',grade:1,desc:'炼制气血丹药',chance:.7,output:2,stoneCost:14,days:4,enabled:true,durabilityCost:1},
    ...Array.from({length:10},(_,n)=>({id:'fixture-recipe-'+n,name:'一阶丹方'+(n+1),grade:1,desc:'用于密集目录布局验收',chance:.7,output:2,stoneCost:14,days:4,enabled:true,durabilityCost:1})),
    {id:'5002',name:'二阶补气丹',grade:2,desc:'二阶丹方',chance:.7,output:1,stoneCost:20,days:5,enabled:false,reason:'境界不足',durabilityCost:1}],
  furnaces:[{id:'basic',name:'黄铜丹炉',grade:1,current:true,capacity:1,price:100}], gears:[] });
initial.player.arts = initial.arts;
initial.market = [
  ...initial.herbCatalog.map(item=>({...item,kind:'herb',price:4,owned:0})),
  ...initial.materialCatalog.map(item=>({...item,kind:'material',price:4,owned:0})),
  ...initial.pillCatalog.map(item=>({...item,kind:'potion',price:20,desc:'当前回复气血0点',owned:0})),
];
let state = structuredClone(initial);
function sync() {
  state.arts = state.player.arts;
  const count = state.arts.potions['5001'] || 0;
  Object.assign(state.pillCatalog[0], {owned:count,enabled:count>0,reason:count>0?'':'没有这枚丹药'});
  state.pillCatalog[0].effect = state.player.health === 100 ? '当前回复气血0点' : '回复气血20点';
  state.brewOptions[0].ingredients = state.herbCatalog.map(item=>({...item,required:1,owned:state.arts.herbs[item.name]||0}));
  state.brewOptions[0].enabled=state.brewOptions[0].ingredients.every(item=>item.owned>=1);
  state.brewOptions[0].reason=state.brewOptions[0].enabled?'':'指定药材不足';
  state.clock.month=Math.floor((state.clock.days%360)/30)+1; state.clock.day=state.clock.days%30+1;
  state.stories[0].done=!!state.bonds.stories.letter;
  return state;
}
function quote(body) {
  const defs=body.kind==='brew'?state.herbCatalog.map(item=>({...item,kind:'herb',required:body.batch||1,owned:state.arts.herbs[item.name]||0}))
    :[['main',4],['aux',3],['core',3]].map(([role,required])=>({...state.materialCatalog.find(item=>item.id===body[role]),kind:'material',required,owned:state.arts.materials[body[role]]||0}));
  const materials=defs.map(item=>({...item,missing:Math.max(0,item.required-item.owned),unitPrice:4,cost:Math.max(0,item.required-item.owned)*4,buyable:true,reason:''}));
  return {valid:true,enabled:materials.some(item=>item.missing),batch:body.batch||1,reason:materials.some(item=>item.missing)?'':'材料已备齐，无须补料',totalCost:materials.reduce((sum,item)=>sum+item.cost,0),materials};
}
const server=createServer(async(req,res)=>{
  try {
    if (!req.url.startsWith('/api/')) {
      const path=join(root,'web',req.url.split('?')[0]==='/'?'index.html':decodeURIComponent(req.url.split('?')[0]));
      if(!resolve(path).startsWith(resolve(root,'web'))) {res.writeHead(403);res.end();return;}
      res.setHeader('Content-Type',{'.html':'text/html; charset=utf-8','.mjs':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.webp':'image/webp'}[extname(path)]||'application/octet-stream');res.end(readFileSync(path));return;
    }
    let text='';for await(const chunk of req)text+=chunk;const body=text?JSON.parse(text):{};
    const path=req.url.slice(5).split('?')[0];let result={ok:true};
    if(path==='whoami'||path==='login')result={ok:true,nickname:'DesktopUI121012',hasGame:true};
    else if(path==='new'){state=structuredClone(initial);state.player.name=body.name;result={ok:true,state:sync()};}
    else if(path==='export')result={ok:true,game:JSON.stringify(state)};
    else if(path==='import'){state=JSON.parse(body.game);result={ok:true,state:sync()};}
    else if(path==='state')result={ok:true,hasGame:true,state:sync()};
    else if(path==='plan'){state.plan=body.id;result={ok:true,state:sync()};}
    else if(path==='story'){state.clock.days+=3;state.bonds.stories.letter=1;result={ok:true,state:sync(),text:'家书已落笔'};}
    else if(path==='use-pill'){state.arts.potions[body.id]--;result={ok:true,state:sync(),text:'已服用化淤丹'};}
    else if(path==='eco-options')result={ok:true,options:[{id:'city-work',name:'城中差事',type:'task',enabled:true,days:3,pay:10,desc:'城中日常'}]};
    else if(path==='sects')result={ok:true,sects:[],mentors:[]};
    else if(path==='achievements')result={ok:true,achievements:[{name:'丹术初成',done:true,target:5,progress:500}]};
    else if(path==='forge-options')result={ok:true,materials:state.materialCatalog.map(item=>({...item,owned:state.arts.materials[item.id]||0,element:'剑',polarity:'天阳'})),shapes:[{id:'sword',name:'剑',slot:'weapon'}]};
    else if(path==='forge-preview')result={ok:true,info:{valid:true,enabled:true,grade:1,chance:.6,cost:33,days:8,shapeName:'剑',mainName:'锻剑铁',mainElement:'剑',mainPolarity:'天阳',passive:{text:'攻击倍率+10个百分点'},ability:{id:'artifact:fixture',name:'锻剑铁剑·催动',dao:'剑',kind:'attack',cost:2,cooldown:1,description:'攻击目标',preview:{valid:true,text:'造成57点伤害',context:'模拟服务端字段，仅用于DOM验证'}}}};
    else if(path==='craft/materials/quote')result={ok:true,quote:quote(body)};
    else if(path==='craft/materials/buy'){const q=quote(body);state.player.stones-=q.totalCost;for(const item of q.materials){const stock=item.kind==='herb'?state.arts.herbs:state.arts.materials;stock[item.kind==='herb'?item.name:item.id]=(stock[item.kind==='herb'?item.name:item.id]||0)+item.missing;}result={ok:true,state:sync(),text:'补料完成',quote:q};}
    else if(path==='gear/salvage'){const gear=state.gears.find(item=>item.id===body.id);state.player.stones-=gear.salvage.costStones;for(const item of gear.salvage.materials)state.arts.materials[item.id]=(state.arts.materials[item.id]||0)+item.count;state.gears=state.gears.filter(item=>item.id!==body.id);result={ok:true,state:sync(),text:'材料已回收'};}
    else if(path==='tianji/status')result={ok:true,hasEncounter:false};
    res.setHeader('Content-Type','application/json; charset=utf-8');res.end(JSON.stringify(result));
  }catch(error){res.statusCode=500;res.end(JSON.stringify({ok:false,error:error.message}));}
});
server.listen(port,'127.0.0.1');
