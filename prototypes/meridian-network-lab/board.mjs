// Static board geometry and rule definitions belong to the isolated rules engine.
// The browser receives only the filtered snapshot produced by mfGetView.
const Kinds = {
  ordinary: { cost: 4, successPercent: 85, points: 4, effectText: '冲开此穴，得 4 分。' },
  peek: { cost: 4, successPercent: 90, points: 2, effectText: '得 2 分，照见此穴两跳以内的穴位详情。' },
  boost: { cost: 5, successPercent: 80, points: 3, effectText: '得 3 分；下一次真正冲穴的目标成功率 +20 个百分点，同穴重试与灵核恢复保留，同类不叠加。' },
  gather: { cost: 5, successPercent: 75, points: 2, effectText: '得 2 分，并回收 12 真气；可超过开局真气。' },
  link: { cost: 6, successPercent: 80, points: 3, effectText: '得 3 分，打开图上标出的捷径。' },
  refine: { cost: 7, successPercent: 65, points: 8, effectText: '冲开此穴，得 8 分。' },
  gate: { cost: 8, successPercent: 70, points: 10, effectText: '得 10 分；本关区任一主关贯通，即算通过该关区。' },
  start: { cost: 0, successPercent: 100, points: 0, effectText: '任、督、冲三关皆通后，回到丹田定型。' },
};

const Rows = [
  ['start', '丹田', 50, 70, 'start'],
  ['huiyin', '会阴', 50, 84, 'gather'],
  ['qihai', '气海', 36, 78, 'boost'],
  ['guanyuan', '关元', 24, 89, 'ordinary'],
  ['shimen', '石门', 15, 77, 'peek'],
  ['zhongji', '中极', 8, 66, 'gather'],
  ['tianshu', '天枢', 24, 64, 'ordinary'],
  ['daheng', '大横', 35, 63, 'link'],
  ['shenque', '神阙', 35, 48, 'refine'],
  ['renWest', '任关·阴交', 15, 52, 'gate', 'ren'],
  ['renEast', '任关·玉堂', 29, 41, 'gate', 'ren'],
  ['fujie', '腹结', 8, 42, 'peek'],
  ['daimai', '带脉', 16, 30, 'ordinary'],
  ['zhangmen', '章门', 29, 28, 'gather'],
  ['juque', '巨阙', 40, 31, 'ordinary'],
  ['zhongwan', '中脘', 45, 20, 'boost'],
  ['duWest', '督关·百会', 31, 10, 'gate', 'du'],
  ['baihui', '上星', 50, 8, 'refine'],
  ['duEast', '督关·神庭', 67, 10, 'gate', 'du'],
  ['shenting', '印堂', 62, 22, 'ordinary'],
  ['yintang', '玉枕', 78, 20, 'peek'],
  ['fengfu', '风府', 89, 30, 'ordinary'],
  ['tianzhu', '天柱', 68, 31, 'link'],
  ['chongWest', '冲关·膻中', 78, 41, 'gate', 'chong'],
  ['riyue', '日月', 90, 45, 'gather'],
  ['chongEast', '冲关·璇玑', 87, 61, 'gate', 'chong'],
  ['jingmen', '京门', 93, 76, 'refine'],
  ['weidao', '维道', 80, 87, 'gather'],
  ['shuidao', '水道', 67, 78, 'boost'],
  ['fuliu', '复溜', 66, 62, 'ordinary'],
  ['taixi', '太溪', 66, 49, 'peek'],
  ['yongquan', '涌泉', 66, 92, 'refine'],
  ['sanyinjiao', '三阴交', 39, 92, 'ordinary'],
  ['mingmen', '命门', 11, 15, 'refine'],
  ['dazhui', '大椎', 89, 11, 'boost'],
  ['zhiyang', '至阳', 52, 31, 'ordinary'],
];

export const Nodes = Object.freeze(Rows.map(([Id, Label, X, Y, Kind, GateId]) => Object.freeze({
  id: Id, label: Label, x: X, y: Y, kind: Kind, ...Kinds[Kind],
  maxAttempts: Kind === 'gate' ? 3 : Kind === 'start' ? 0 : 2,
  ...(GateId ? { gateId: GateId } : {}),
})));

const Connections = [
  ['start', 'qihai'], ['start', 'huiyin'], ['start', 'shuidao'], ['start', 'zhiyang'],
  ['qihai', 'guanyuan'], ['qihai', 'tianshu'], ['qihai', 'daheng'],
  ['huiyin', 'sanyinjiao'], ['huiyin', 'yongquan'],
  ['shuidao', 'weidao'], ['shuidao', 'fuliu'],
  ['guanyuan', 'shimen'], ['guanyuan', 'sanyinjiao'],
  ['shimen', 'zhongji'], ['shimen', 'tianshu'],
  ['zhongji', 'renWest'], ['zhongji', 'fujie'],
  ['tianshu', 'daheng'], ['tianshu', 'renWest'], ['daheng', 'shenque'],
  ['shenque', 'renEast'], ['shenque', 'zhangmen'],
  ['renWest', 'fujie'], ['renWest', 'renEast'], ['fujie', 'daimai'],
  ['renEast', 'zhangmen'], ['renEast', 'juque'],
  ['daimai', 'mingmen'], ['daimai', 'zhangmen'],
  ['zhangmen', 'duWest'], ['zhangmen', 'juque'],
  ['juque', 'zhongwan'], ['juque', 'zhiyang'],
  ['zhongwan', 'duWest'], ['zhongwan', 'baihui'], ['zhongwan', 'shenting'],
  ['duWest', 'mingmen'], ['duWest', 'baihui'], ['baihui', 'duEast'],
  ['duEast', 'shenting'], ['duEast', 'dazhui'],
  ['shenting', 'tianzhu'], ['shenting', 'zhiyang'],
  ['yintang', 'tianzhu'], ['yintang', 'dazhui'], ['yintang', 'fengfu'],
  ['fengfu', 'dazhui'], ['fengfu', 'chongWest'], ['fengfu', 'riyue'],
  ['tianzhu', 'chongWest'], ['tianzhu', 'taixi'], ['zhiyang', 'tianzhu'],
  ['chongWest', 'riyue'], ['chongWest', 'taixi'], ['riyue', 'chongEast'],
  ['chongEast', 'taixi'], ['chongEast', 'jingmen'], ['chongEast', 'fuliu'],
  ['jingmen', 'weidao'], ['weidao', 'fuliu'], ['weidao', 'yongquan'],
  ['fuliu', 'taixi'], ['yongquan', 'sanyinjiao'],
];

export const Edges = Object.freeze([
  ...Connections.map(([From, To]) => Object.freeze({ from: From, to: To })),
  Object.freeze({ from: 'daheng', to: 'fuliu', shortcutId: 'lowerBridge', unlockedBy: 'daheng' }),
  Object.freeze({ from: 'zhangmen', to: 'tianzhu', shortcutId: 'upperBridge', unlockedBy: 'tianzhu' }),
]);

export const GateRegions = Object.freeze([
  Object.freeze({ id: 'ren', label: '任脉关区' }),
  Object.freeze({ id: 'du', label: '督脉关区' }),
  Object.freeze({ id: 'chong', label: '冲脉关区' }),
]);
