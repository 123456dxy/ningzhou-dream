const PowerCap = 10;
const AdjustmentAmount = 2;
const BaseOperations = 14;
const StageNames = ['任脉', '督脉', '冲脉'];
const DefaultSeed = 0x6E696E67;
const NodeLayout = [
    { id: 'start', label: '入脉', x: 8, y: 50 },
    { id: 'up1', label: '上支一穴', x: 30, y: 22 },
    { id: 'down1', label: '下支一穴', x: 30, y: 78 },
    { id: 'up2', label: '上支二穴', x: 57, y: 22 },
    { id: 'down2', label: '下支二穴', x: 57, y: 78 },
    { id: 'detour', label: '回旋穴', x: 76, y: 50 },
    { id: 'end', label: '关窍', x: 94, y: 50 },
];
const Edges = [
    { from: 'start', to: 'up1' }, { from: 'start', to: 'down1' },
    { from: 'up1', to: 'up2' }, { from: 'up1', to: 'down2' },
    { from: 'down1', to: 'up2' }, { from: 'down1', to: 'down2' },
    { from: 'up2', to: 'end' }, { from: 'up2', to: 'detour' },
    { from: 'down2', to: 'end' }, { from: 'down2', to: 'detour' },
    { from: 'detour', to: 'end' },
];
const Distances = { start: 3, up1: 2, down1: 2, up2: 1, down2: 1, detour: 1, end: 0 };
const Templates = [
    { up1: [7, 9, 3, -4], down1: [4, 6, 2, -1], up2: [3, 5, 3, 2], down2: [7, 9, 3, -2], detour: [5, 7, 2, -2], end: [3, 6, 1, 0] },
    { up1: [3, 5, 3, 3], down1: [6, 8, 2, -2], up2: [6, 8, 3, -3], down2: [2, 4, 3, 2], detour: [4, 6, 2, 2], end: [5, 8, 1, -1] },
    { up1: [5, 7, 3, -2], down1: [2, 4, 2, 3], up2: [2, 4, 3, 3], down2: [6, 8, 3, -4], detour: [3, 5, 2, 2], end: [4, 7, 1, -2] },
    { up1: [2, 4, 3, 4], down1: [7, 9, 2, -3], up2: [5, 7, 3, -2], down2: [3, 5, 3, 2], detour: [6, 8, 2, -3], end: [3, 6, 1, 1] },
];

function mfRequire(Condition, Message) {
    if (!Condition) throw new Error(Message);
}

function mfObject(Value, Keys, Name, Required = []) {
    mfRequire(Value !== null && typeof Value === 'object' && !Array.isArray(Value), `${Name}必须是对象`);
    const Prototype = Object.getPrototypeOf(Value);
    mfRequire(Prototype === Object.prototype || Prototype === null, `${Name}对象类型非法`);
    for (const Key of Reflect.ownKeys(Value)) {
        mfRequire(typeof Key === 'string' && Keys.includes(Key), `${Name}包含未知字段：${String(Key)}`);
    }
    for (const Key of Required) mfRequire(Object.hasOwn(Value, Key), `${Name}缺少字段：${Key}`);
}

function mfSettings(Settings) {
    mfObject(Settings, ['pills', 'core', 'talent'], '准备');
    const Prepared = { pills: 0, core: false, talent: 'none', ...Settings };
    mfRequire(Number.isInteger(Prepared.pills) && Prepared.pills >= 0 && Prepared.pills <= 6, '筑基丹数量须为0至6的整数');
    mfRequire(typeof Prepared.core === 'boolean', '灵核准备须为布尔值');
    mfRequire(['none', 'daoBody', 'steady'].includes(Prepared.talent), '未知天赋');
    return Prepared;
}

function mfClampPower(Power) { return Math.max(0, Math.min(PowerCap, Power)); }
function mfScore(Run) { return 100 + Math.min(20, Run.extraScore); }
function mfMinimumRemaining(Run, Position = Run.positionId) { return Distances[Position] + (StageNames.length - Run.stage) * 3; }
function mfFreeAdjustment(Run) { return Run.settings.talent === 'steady' && !Run.SteadyUsed; }
function mfAtExit(Run) { return Run.positionId === 'end'; }
function mfCanAdvance(Run) { return Run.status === 'active' && mfAtExit(Run) && Run.stage < StageNames.length; }
function mfGrade(Score) { return Score >= 120 ? '圆满' : Score >= 114 ? '浑厚' : Score >= 108 ? '稳固' : '平实'; }

function mfReward(Score) {
    const Gain = Math.max(0, Math.min(120, Score) - 100);
    return { baseHpGain: Gain, battleHpEquivalent: Gain * 24 / 10 };
}

function mfSummary(Run) {
    if (Run.status === 'success') return `三脉贯通，根基${mfScore(Run)}分（${mfGrade(mfScore(Run))}），基础生命增加${mfReward(mfScore(Run)).baseHpGain}；本次根基已定。`;
    if (Run.status === 'withdrawn') return '已退出行气，本次不结算根基奖励。';
    if (mfAtExit(Run)) return `${StageNames[Run.stage - 1]}已通过；进入下一段时才揭晓该段穴位，气势与剩余次数继续保留。`;
    return `当前行于${StageNames[Run.stage - 1]}，预计底蕴${mfScore(Run)}分；选相邻穴位前进，或用蓄气、泄气调整气势。`;
}

function mfBasicView(Run) {
    const Score = mfScore(Run);
    return {
        revision: Run.revision, status: Run.status, settings: { ...Run.settings }, stage: Run.stage,
        stageNames: [...StageNames], positionId: Run.positionId, power: Run.power,
        operationsLeft: Run.operationsLeft, operationsTotal: Run.operationsTotal,
        extraScore: Run.extraScore, score: Score, grade: mfGrade(Score),
        reward: mfReward(Run.status === 'success' ? Score : 0),
        sealReward: mfReward(Run.status !== 'withdrawn' ? Score : 0),
        visitedIds: [...Run.visitedIds], lastMove: structuredClone(Run.lastMove),
        atExit: mfAtExit(Run), canAdvance: mfCanAdvance(Run), summary: mfSummary(Run),
    };
}

export function mfCreateRun(Settings = {}, Seed = DefaultSeed) {
    const Prepared = mfSettings(Settings);
    mfRequire(Number.isInteger(Seed) && Seed >= 0 && Seed <= 0xFFFFFFFF, '内部随机种子须为uint32整数');
    const Operations = BaseOperations + (Prepared.core ? 2 : 0) + (Prepared.talent === 'daoBody' ? 1 : 0);
    return {
        revision: 0, status: 'active', settings: Prepared, stage: 1, positionId: 'start',
        power: mfClampPower(6 + Prepared.pills), operationsLeft: Operations, operationsTotal: Operations,
        extraScore: 0, visitedIds: ['start'], history: [], lastMove: null,
        RandomState: Seed >>> 0, TemplateIndex: 0, SteadyUsed: false,
    };
}

function mfNode(Run, Layout) {
    const Data = Layout.id === 'start' ? [null, null, 0, 0] : Templates[Run.TemplateIndex][Layout.id];
    return { ...Layout, minPower: Data[0], maxPower: Data[1], bonus: Data[2], powerDelta: Data[3],
        visited: Run.visitedIds.includes(Layout.id), isCurrent: Run.positionId === Layout.id,
        isAdjacent: Run.status === 'active' && Edges.some(Edge => Edge.from === Run.positionId && Edge.to === Layout.id) };
}

export function mfGetView(Run) {
    const Nodes = NodeLayout.map(Layout => mfNode(Run, Layout));
    const Actions = [
        ...Edges.filter(Edge => Edge.from === Run.positionId).map(Edge => ({ action: { kind: 'move', targetId: Edge.to }, label: `行至${Nodes.find(Node => Node.id === Edge.to).label}` })),
        { action: { kind: 'charge' }, label: '蓄气 +2' },
        { action: { kind: 'release' }, label: '泄气 −2' },
        { action: { kind: 'advance' }, label: '进入下一段' },
        { action: { kind: 'withdraw' }, label: '退出行气' },
    ];
    return structuredClone({
        ...mfBasicView(Run), nodes: Nodes, edges: Edges, history: Run.history,
        choices: Actions.map(Choice => ({ ...Choice, quote: mfQuote(Run, Choice.action) })),
        rules: {
            powerCap: PowerCap, chargeAmount: AdjustmentAmount, releaseAmount: AdjustmentAmount,
            moveCost: 1, adjustmentCost: 1, stageCount: StageNames.length,
            minimumOperationsToFinish: mfMinimumRemaining(Run), freeAdjustmentAvailable: Run.status === 'active' && mfFreeAdjustment(Run),
            freeAdjustmentsPerStage: Run.settings.talent === 'steady' ? 1 : 0, scoreCap: 120,
            preparationHint: `初始气势${mfClampPower(6 + Run.settings.pills)}（筑基丹每颗加1，上限10）；共有${Run.operationsTotal}次行气（灵核加2，道体加1）。稳健使每段首次有效蓄气或泄气免次数。`,
            guideHints: [
                '每段只可沿箭头前进，当前段全部穴位可见，后段进入时才揭晓。',
                '进入穴位时气势在所示区间内才获得底蕴；无论是否吻合，随后都结算该穴气势变化。',
                '普通前进和调气各耗1次；直达一段至少3次，绕经回旋穴多耗1次。后端始终保留走完全程的最低次数。',
                '气势跨穴、跨段保留。三段走完自动结基；100分以上每分增加1基础生命，最多120分。',
            ],
        },
    });
}

function mfCheckBudget(Run, Cost, Position = Run.positionId) {
    mfRequire(Run.operationsLeft - Cost >= mfMinimumRemaining(Run, Position), '剩余行气次数不足：须保留走完余程的最低次数');
}

function mfDrawTemplate(Run) {
    Run.RandomState = (Math.imul(Run.RandomState, 1664525) + 1013904223) >>> 0;
    return (Run.RandomState >>> 16) % Templates.length;
}

function mfExecute(Run, Action, IsPreview) {
    mfObject(Action, ['kind', 'targetId'], '动作', ['kind']);
    mfRequire(['move', 'charge', 'release', 'advance', 'withdraw'].includes(Action.kind), '未知动作');
    mfRequire(Run.status === 'active', '本局已经结束');
    if (Action.kind === 'move') {
        mfRequire(Object.hasOwn(Action, 'targetId') && typeof Action.targetId === 'string', '前进动作需要合法穴位ID');
    } else mfRequire(!Object.hasOwn(Action, 'targetId'), '该动作不能携带穴位ID');
    const Next = structuredClone(Run);
    let Cost = 0;
    let Free = false;
    let Outcome = null;
    let Text;
    if (Action.kind === 'move') {
        mfRequire(NodeLayout.some(Node => Node.id === Action.targetId), '未知穴位ID');
        mfRequire(Edges.some(Edge => Edge.from === Run.positionId && Edge.to === Action.targetId), '只能沿箭头前进到相邻穴位');
        mfRequire(!Run.visitedIds.includes(Action.targetId), '不能重复冲同一穴位');
        Cost = 1;
        mfCheckBudget(Run, Cost, Action.targetId);
        const Node = mfNode(Run, NodeLayout.find(Layout => Layout.id === Action.targetId));
        const Matched = Run.power >= Node.minPower && Run.power <= Node.maxPower;
        const Gain = Matched ? Node.bonus : 0;
        Next.operationsLeft -= Cost;
        Next.power = mfClampPower(Run.power + Node.powerDelta);
        Next.extraScore += Gain;
        Next.positionId = Node.id;
        Next.visitedIds.push(Node.id);
        Next.lastMove = { from: Run.positionId, to: Node.id, matched: Matched, gain: Gain, powerBefore: Run.power, powerAfter: Next.power };
        if (Node.id === 'end' && Run.stage === StageNames.length) Next.status = 'success';
        Outcome = { ...Next.lastMove, powerDelta: Node.powerDelta };
        Text = `行至${Node.label}，入穴气势${Run.power}${Matched ? `吻合${Node.minPower}至${Node.maxPower}，得${Gain}底蕴` : `未吻合${Node.minPower}至${Node.maxPower}，本穴无额外底蕴`}；穴后气势${Next.power}，耗1次行气。`;
        if (Next.status === 'success') Text += `三脉贯通，最终${mfScore(Next)}分，基础生命增加${mfReward(mfScore(Next)).baseHpGain}。`;
    } else if (Action.kind === 'charge' || Action.kind === 'release') {
        const Delta = Action.kind === 'charge' ? AdjustmentAmount : -AdjustmentAmount;
        const AdjustedPower = mfClampPower(Run.power + Delta);
        mfRequire(AdjustedPower !== Run.power, Action.kind === 'charge' ? '气势已达上限，蓄气不会生效' : '气势已为0，泄气不会生效');
        Free = mfFreeAdjustment(Run);
        Cost = Free ? 0 : 1;
        mfCheckBudget(Run, Cost);
        Next.power = AdjustedPower;
        Next.operationsLeft -= Cost;
        if (Free) Next.SteadyUsed = true;
        Outcome = { matched: null, gain: 0, powerBefore: Run.power, powerAfter: Next.power, freeAdjustment: Free };
        Text = `${Action.kind === 'charge' ? '蓄气' : '泄气'}：气势${Run.power}变为${Next.power}；${Free ? '本段稳健护持生效，免行气次数' : '耗1次行气'}。`;
    } else if (Action.kind === 'advance') {
        mfRequire(mfCanAdvance(Run), '须先抵达本段关窍，才可进入下一段');
        Next.stage += 1;
        Next.positionId = 'start';
        Next.visitedIds = ['start'];
        Next.lastMove = null;
        Next.SteadyUsed = false;
        if (!IsPreview) Next.TemplateIndex = mfDrawTemplate(Next);
        Outcome = { matched: null, gain: 0, powerAfter: Next.power, unknownNextStage: true };
        Text = `进入${StageNames[Next.stage - 1]}不耗次数，保留气势${Next.power}与${Next.operationsLeft}次行气；实际进入时才揭晓本段穴位。`;
    } else {
        Next.status = 'withdrawn';
        Text = '退出行气，本次不结算根基奖励。';
    }
    Next.revision += 1;
    Next.history.push({ stage: Next.stage, kind: Action.kind, text: Text });
    return { Next, changes: [Text], cost: { operations: Cost, free: Free }, outcome: Outcome };
}

export function mfAct(Run, Action) { return mfExecute(Run, Action, false).Next; }

export function mfQuote(Run, Action) {
    try {
        const Result = mfExecute(Run, Action, true);
        return { enabled: true, reason: '', changes: Result.changes, after: mfBasicView(Result.Next), cost: Result.cost, outcome: Result.outcome };
    } catch (Error) {
        return { enabled: false, reason: Error.message || '动作无法执行', changes: [], after: null, cost: null, outcome: null };
    }
}
