// Exposed global state guards mirror api.cpp's documented route exceptions.
// Item-specific flags/previews remain server-owned; no numeric game formulas.
export function mutationBlocker(state,path,{tianjiActive=false}={}) {
  if(path.startsWith('tianji/'))return '';
  if(tianjiActive||state?.tianji?.hasEncounter)return '天机留影斗法尚未结束，请先完成或认输本场。';
  if(['new','load','import'].includes(path))return '';
  if(!state)return '请先建立人物或读取存档。';
  if(['save','legacy/settle','checkpoint/restore'].includes(path))return '';
  if(state.zhuji?.status==='active'&&path!=='zhuji')return '正在筑基，请先完成运转或散功离关。';
  if(state.zhuji?.status==='preparing'&&!['zhuji','advance','choose','battle/turn'].includes(path))return '筑基准备中，请先完成闭关安排。';
  if(state.dungeons?.run&&!path.startsWith('dungeon/')&&path!=='battle/turn')return '请先结束当前秘境探索。';
  if(state.ended)return '这一世已结束，请先查看结算。';
  if(state.battle&&path!=='battle/turn')return '请先完成当前斗法。';
  if(state.pending&&!['choose','battle/turn',...(state.pending.type==='ascensionTournament'?['ascension-tournament/enter']:[])].includes(path))return '请先处理当前抉择。';
  if(state.job&&!['advance','cancel-job','plan','choose','battle/turn'].includes(path))return `当前安排「${state.job.name}」尚余 ${state.job.remaining} 天，请先完成或中止。`;
  return '';
}
export function serviceDestination(target,body={}){
  if(target==='market')return ['inventory','market'];
  if(target==='sect'||target==='society')return ['social','sects'];
  if(target==='dungeon')return ['map',body.id==='intel'?'intel':'dungeons'];
  if(target==='city'||target==='economy')return ['inventory','jobs'];
  return null;
}
