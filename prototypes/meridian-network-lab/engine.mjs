import { Nodes, Edges, GateRegions } from './board.mjs';

const NodeById = new Map(Nodes.map(Node => [Node.id, Node]));
const DefaultSettings = Object.freeze({ pills: 1, core: true, talent: 'none' });

function mfIsRecord(Value) {
  return Value !== null && typeof Value === 'object' && !Array.isArray(Value)
    && (Object.getPrototypeOf(Value) === Object.prototype || Object.getPrototypeOf(Value) === null);
}

function mfValidateSettings(Settings) {
  if (!mfIsRecord(Settings) || Object.keys(Settings).some(Key => !Object.hasOwn(DefaultSettings, Key))) {
    throw new TypeError('准备参数含未知字段或格式错误。');
  }
  const Result = { ...DefaultSettings, ...Settings };
  if (!Number.isInteger(Result.pills) || Result.pills < 0 || Result.pills > 6) throw new RangeError('丹药数量必须是 0 到 6 的整数。');
  if (typeof Result.core !== 'boolean') throw new TypeError('灵核准备必须为布尔值。');
  if (!['none', 'daoBody', 'steady'].includes(Result.talent)) throw new TypeError('未知天赋。');
  return Result;
}

function mfValidateAction(Action) {
  if (!mfIsRecord(Action) || !['strike', 'move', 'restore', 'seal', 'withdraw'].includes(Action.kind)) throw new TypeError('未知动作。');
  const IsTargeted = Action.kind === 'strike' || Action.kind === 'move';
  const Keys = IsTargeted ? ['kind', 'targetId'] : ['kind'];
  if (Object.keys(Action).some(Key => !Keys.includes(Key))) throw new TypeError('动作含未知字段。');
  if (IsTargeted && (typeof Action.targetId !== 'string' || !NodeById.has(Action.targetId))) throw new TypeError('未知穴位 ID。');
}

function mfIsEdgeOpen(Run, Edge) {
  return !Edge.unlockedBy || Run.nodes[Edge.unlockedBy].opened;
}

function mfNeighbors(Run, Id) {
  return Edges.filter(Edge => mfIsEdgeOpen(Run, Edge) && (Edge.from === Id || Edge.to === Id))
    .map(Edge => Edge.from === Id ? Edge.to : Edge.from);
}

function mfReveal(Run, Id, Depth) {
  const Visited = new Set([Id]);
  let Frontier = [Id];
  for (let Distance = 0; Distance <= Depth; ++Distance) {
    for (const Current of Frontier) Run.nodes[Current].revealed = true;
    const Next = [];
    for (const Current of Frontier) {
      for (const Neighbor of mfNeighbors(Run, Current)) {
        if (!Visited.has(Neighbor)) { Visited.add(Neighbor); Next.push(Neighbor); }
      }
    }
    Frontier = Next;
  }
}

function mfGateViews(Run) {
  return GateRegions.map(Region => ({ ...Region, opened: Nodes.some(Node => Node.gateId === Region.id && Run.nodes[Node.id].opened) }));
}

function mfScore(RawScore) { return Math.min(120, RawScore); }

function mfReward(RawScore) {
  const BaseHp = Math.max(0, mfScore(RawScore) - 100);
  return { baseHpGain: BaseHp, battleHpEquivalent: Math.round(BaseHp * 24) / 10 };
}

function mfCost(Run, Node) { return Math.max(1, Node.cost - (Run.settings.talent === 'daoBody' ? 1 : 0)); }

function mfSuccessPercent(Run, Node) {
  const BuffBonus = Run.buff && (Run.buff.targetId === null || Run.buff.targetId === Node.id) ? Run.buff.bonusPercent : 0;
  return Math.min(100, Node.successPercent + (Node.kind === 'gate' && Run.settings.talent === 'steady' ? 10 : 0) + BuffBonus);
}

function mfReturnCost(Run) {
  const Visited = new Set([Run.positionId]);
  let Frontier = [Run.positionId];
  let Distance = 0;
  while (Frontier.length) {
    if (Frontier.includes('start')) return Distance;
    const Next = [];
    for (const Current of Frontier) for (const Neighbor of mfNeighbors(Run, Current)) {
      if (!Visited.has(Neighbor) && Run.nodes[Neighbor].opened) { Visited.add(Neighbor); Next.push(Neighbor); }
    }
    Frontier = Next;
    ++Distance;
  }
  return null;
}

/** Create one isolated run. Raw runs stay on the server; expose only mfGetView. */
export function mfCreateRun(Settings = {}, Seed = 0x4e5a2026) {
  const Prepared = mfValidateSettings(Settings);
  if (!Number.isInteger(Seed) || Seed < 0 || Seed > 0xffffffff) throw new RangeError('种子必须是 uint32。');
  const Run = {
    revision: 0, status: 'active', settings: Prepared, qi: 100 + Prepared.pills * 15,
    qiInitial: 100 + Prepared.pills * 15, rawScore: 0, positionId: 'start', randomState: Seed,
    nodes: Object.fromEntries(Nodes.map(Node => [Node.id, { opened: Node.id === 'start', revealed: Node.kind === 'gate', attemptsLeft: Node.maxAttempts }])),
    coreAvailable: Prepared.core, lastFailedId: null, lastFailurePositionId: null, buff: null,
    history: [{ text: '真气归于丹田。任选相邻穴位，冲开任、督、冲三关后回归定型。' }], lastEvent: null,
  };
  mfReveal(Run, 'start', 2);
  return Run;
}

/** Pure quote: legal but unavailable actions return enabled:false, malformed input throws. */
export function mfQuote(Run, Action) {
  mfValidateAction(Action);
  const Quote = { enabled: false, reason: '', cost: { qi: 0 }, successPercent: null, points: 0, effectText: '', changes: [], outcomes: {} };
  const mfDeny = Reason => ({ ...Quote, reason: Reason });
  if (Run.status !== 'active') return mfDeny('本次经脉已结束，不能再补修。');
  if (Action.kind === 'withdraw') return { ...Quote, enabled: true, effectText: '撤退并结束本次试玩，不获得生命奖励。', after: { status: 'withdrawn' } };
  if (Action.kind === 'restore') {
    if (!Run.coreAvailable) return mfDeny('没有可用灵核。');
    if (!Run.lastFailedId || Run.lastFailurePositionId !== Run.positionId) return mfDeny('只能恢复刚刚失败、尚未离位或改冲其他穴位的穴。');
    const Node = NodeById.get(Run.lastFailedId);
    return { ...Quote, enabled: true, effectText: `消耗一次灵核，将${Node.label}剩余冲穴次数恢复为 ${Node.maxAttempts}；不返还真气。`,
      changes: [`${Node.label}剩余冲穴次数恢复为 ${Node.maxAttempts}，真气不变。`],
      outcomes: { success: { qi: Run.qi, score: mfScore(Run.rawScore), rawScore: Run.rawScore, attemptsLeft: Node.maxAttempts } } };
  }
  if (Action.kind === 'seal') {
    if (!mfGateViews(Run).every(Gate => Gate.opened)) return mfDeny('任、督、冲三个关区尚未全部贯通。');
    if (Run.positionId !== 'start') return mfDeny('请先沿已通经脉回到丹田。');
    const After = { qi: Run.qi, rawScore: Run.rawScore + 20, score: mfScore(Run.rawScore + 20), reward: mfReward(Run.rawScore + 20), status: 'success' };
    return { ...Quote, enabled: true, points: 20, effectText: '完成周天，额外得 20 分并定型；本次不能再补修。', after: After,
      changes: ['定型额外获得 20 分，按最终根基结算生命收益；本局不再补修。'], outcomes: { success: { ...After, reward: { ...After.reward } } } };
  }
  const Node = NodeById.get(Action.targetId);
  const State = Run.nodes[Node.id];
  if (!State.revealed) return mfDeny('此穴尚未照见详情，请先沿经脉接近。');
  if (!mfNeighbors(Run, Run.positionId).includes(Node.id)) return mfDeny('真气只能前往当前穴位相邻、连线已开的穴位。');
  if (Action.kind === 'move') {
    if (!State.opened) return mfDeny('此穴尚未冲开。');
    if (Run.qi < 1) return mfDeny('回走需要 1 真气。');
    return { ...Quote, enabled: true, cost: { qi: 1 }, effectText: '沿已通经脉移动，不重复得分或触发效果。',
      outcomes: { success: { qi: Run.qi - 1, score: mfScore(Run.rawScore), rawScore: Run.rawScore, positionId: Node.id } } };
  }
  if (State.opened) return mfDeny('此穴已通，请用回走移动。');
  if (State.attemptsLeft === 0) return mfDeny('此穴冲穴次数已耗尽；若刚刚失败，可用灵核恢复。');
  const Cost = mfCost(Run, Node);
  const SuccessPercent = mfSuccessPercent(Run, Node);
  Object.assign(Quote, { cost: { qi: Cost }, successPercent: SuccessPercent, points: Node.points, effectText: Node.effectText,
    outcomes: {
      success: { qi: Run.qi - Cost + (Node.kind === 'gather' ? 12 : 0), rawScore: Run.rawScore + Node.points,
        score: mfScore(Run.rawScore + Node.points), positionId: Node.id, opened: true },
      failure: { qi: Run.qi - Cost, rawScore: Run.rawScore, score: mfScore(Run.rawScore), positionId: Run.positionId,
        attemptsLeft: State.attemptsLeft - 1, blocked: State.attemptsLeft === 1 },
    },
    changes: [`成功贯通${Node.label}，获得 ${Node.points} 分。`, ...(Node.kind === 'link' ? ['成功后打开图上虚线捷径。'] : [])],
  });
  if (Run.qi < Cost) return { ...Quote, reason: `本次冲穴需要 ${Cost} 真气。` };
  return { ...Quote, enabled: true };
}

/** Apply to a clone. Rejected actions never change the original state or RNG. */
export function mfAct(Run, Action) {
  const Quote = mfQuote(Run, Action);
  if (!Quote.enabled) throw new Error(Quote.reason);
  const Next = structuredClone(Run);
  const From = Next.positionId;
  const QiBefore = Next.qi;
  let Success = true;
  let Gain = 0;
  let Text;
  Next.qi -= Quote.cost.qi;
  if (Action.kind === 'strike') {
    const Node = NodeById.get(Action.targetId);
    if (Next.buff && Next.buff.targetId !== null && Next.buff.targetId !== Node.id) Next.buff = null;
    if (Next.buff && Next.buff.targetId === null) Next.buff.targetId = Node.id;
    Next.randomState = (Math.imul(Next.randomState, 1664525) + 1013904223) >>> 0;
    Success = Next.randomState / 4294967296 < Quote.successPercent / 100;
    if (Success) {
      Next.nodes[Node.id].opened = true;
      Next.positionId = Node.id;
      Gain = Node.points;
      Next.rawScore += Gain;
      Next.lastFailedId = null;
      Next.lastFailurePositionId = null;
      Next.buff = Node.kind === 'boost' ? { bonusPercent: 20, targetId: null } : null;
      if (Node.kind === 'gather') Next.qi += 12;
      mfReveal(Next, Node.id, Node.kind === 'peek' ? 2 : 1);
      Text = `${Node.label}贯通，得 ${Gain} 分${Node.kind === 'gather' ? '，回收 12 真气' : ''}。`;
    } else {
      --Next.nodes[Node.id].attemptsLeft;
      Next.lastFailedId = Node.id;
      Next.lastFailurePositionId = From;
      Text = `${Node.label}冲穴未成，消耗 ${Quote.cost.qi} 真气；还可尝试 ${Next.nodes[Node.id].attemptsLeft} 次。`;
    }
  } else if (Action.kind === 'move') {
    Next.positionId = Action.targetId;
    Next.lastFailedId = null;
    Next.lastFailurePositionId = null;
    Text = `沿已通经脉回走至${NodeById.get(Action.targetId).label}，消耗 1 真气。`;
  } else if (Action.kind === 'restore') {
    Next.nodes[Next.lastFailedId].attemptsLeft = NodeById.get(Next.lastFailedId).maxAttempts;
    Next.coreAvailable = false;
    Text = `灵核恢复${NodeById.get(Next.lastFailedId).label}的冲穴次数；真气不返还。`;
  } else if (Action.kind === 'seal') {
    Next.rawScore += 20;
    Gain = 20;
    Next.status = 'success';
    Text = `三关周天定型，最终 ${mfScore(Next.rawScore)} 分，基础生命 +${mfReward(Next.rawScore).baseHpGain}。`;
  } else {
    Next.status = 'withdrawn';
    Text = '真气归元，本次撤退，不获得生命奖励。';
  }
  ++Next.revision;
  Next.lastEvent = { kind: Action.kind, from: From, to: Action.targetId ?? Next.positionId, targetId: Action.targetId ?? Next.lastFailedId,
    success: Success, qiBefore: QiBefore, qiAfter: Next.qi, gain: Gain };
  Next.history.push({ text: Text });
  return Next;
}

/** Filter hidden details and private RNG state before crossing the HTTP boundary. */
export function mfGetView(Run) {
  const Adjacent = new Set(mfNeighbors(Run, Run.positionId));
  const NodeViews = Nodes.map(Node => {
    const State = Run.nodes[Node.id];
    const Base = { id: Node.id, x: Node.x, y: Node.y, label: State.revealed ? Node.label : '未明穴', revealed: State.revealed,
      opened: State.opened, blocked: !State.opened && State.attemptsLeft === 0, adjacent: Adjacent.has(Node.id) };
    if (!State.revealed) return Base;
    return { ...Base, isAdjacent: Adjacent.has(Node.id), isCurrent: Node.id === Run.positionId, kind: Node.kind, ...(Node.gateId ? { gateId: Node.gateId } : {}),
      cost: Node.kind === 'start' ? 0 : mfCost(Run, Node), successPercent: mfSuccessPercent(Run, Node),
      attemptsLeft: State.attemptsLeft, maxAttempts: Node.maxAttempts, points: Node.points, effectText: Node.effectText };
  });
  const Choices = [];
  for (const TargetId of Adjacent) {
    const Node = NodeById.get(TargetId);
    const Action = { kind: Run.nodes[TargetId].opened ? 'move' : 'strike', targetId: TargetId };
    Choices.push({ action: Action, label: `${Action.kind === 'move' ? '回走' : '冲穴'} · ${Run.nodes[TargetId].revealed ? Node.label : '未明穴'}`, quote: mfQuote(Run, Action) });
  }
  for (const Kind of ['restore', 'seal', 'withdraw']) {
    const Action = { kind: Kind };
    Choices.push({ action: Action, label: { restore: '使用灵核', seal: '周天定型', withdraw: '撤退' }[Kind], quote: mfQuote(Run, Action) });
  }
  const Gates = mfGateViews(Run);
  return {
    revision: Run.revision, status: Run.status, settings: { ...Run.settings }, qi: Run.qi, qiInitial: Run.qiInitial,
    rawScore: Run.rawScore, score: mfScore(Run.rawScore), reward: Run.status === 'success' ? mfReward(Run.rawScore) : null,
    positionId: Run.positionId, gates: Gates, nodes: NodeViews,
    edges: Edges.map(Edge => ({ from: Edge.from, to: Edge.to, locked: !mfIsEdgeOpen(Run, Edge), ...(Edge.shortcutId ? { shortcutId: Edge.shortcutId } : {}) })),
    choices: Choices, history: structuredClone(Run.history), lastEvent: structuredClone(Run.lastEvent),
    coreAvailable: Run.coreAvailable, lastFailedId: Run.lastFailedId, buff: structuredClone(Run.buff), returnCost: mfReturnCost(Run),
    rules: { guideHints: [
      '沿连线冲开相邻穴位；成功才移动、得分并触发一次效果。失败扣真气与次数，留在原处。',
      '任、督、冲三个关区各有两处候选主关；每区任选一处冲通即可，再回到丹田定型。',
      '普通穴最多 2 次，主关 3 次。灵核全局一次，只恢复刚失败的穴位次数，不返气。',
      '已通相邻穴回走花 1 真气，不重复得分或触发冲破效果。',
      '定型 +20 分，最终最高 120 分；100 分以上每分 +1 基础生命，斗法等效生命 ×2.4。',
    ], preparationHint: `初始 100 真气，每枚丹药 +15，最多 6 枚。${Run.settings.talent === 'daoBody' ? '先天道体：每次冲穴耗气 -1，最低 1。' : Run.settings.talent === 'steady' ? '稳固根基：主关成功率 +10 个百分点。' : '未选择特殊天赋。'}聚气回气可超过开局真气。` },
    summary: Run.status === 'success' ? `定型 ${mfScore(Run.rawScore)} 分，基础生命 +${mfReward(Run.rawScore).baseHpGain}。`
      : Run.status === 'withdrawn' ? '本次已撤退，无生命奖励。' : `已通 ${Gates.filter(Gate => Gate.opened).length}/3 关区。`,
  };
}
