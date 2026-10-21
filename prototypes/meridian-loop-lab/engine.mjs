const Size = 5;
const MaxEdits = 2;
const MaxFunctionalPieces = 4;
const BaseTime = 72;
const CoreTime = 8;
const PowerCap = 12;
const GatePowerMinimum = 2;
const GatherCapacity = 2;
const GatherRelease = 4;
const CondensePowerMinimum = 3;
const CondensePowerCost = 2;
const ReturnPower = 4;
const StableDisorderMaximum = 6;
const ScoreBase = 100;
const ScoreCap = 120;
const OuterPath = [20, 21, 22, 23, 24, 19, 14, 9, 4, 3, 2, 1, 0, 5, 10, 15, 20];
const Fixed = [
    { index: 20, label: '丹田', ports: [0, 1] },
    { index: 22, label: '任脉', ports: [1, 3] },
    { index: 14, label: '督脉', ports: [0, 2] },
    { index: 2, label: '冲脉', ports: [1, 3] },
];
const PieceIds = ['gather-1', 'gather-2', 'condense-1', 'condense-2', 'soothe-1', 'soothe-2'];
const PieceLabels = { gather: '蓄气穴', condense: '凝元穴', soothe: '稳脉穴' };

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
    const Result = { pills: 0, core: false, talent: 'none', ...Settings };
    mfRequire(Number.isInteger(Result.pills) && Result.pills >= 0 && Result.pills <= 6, '筑基丹数量须为0至6的整数');
    mfRequire(typeof Result.core === 'boolean', '灵核准备须为布尔值');
    mfRequire(['none', 'daoBody', 'steady'].includes(Result.talent), '未知天赋');
    return Result;
}

function mfPorts(Cell) {
    if (Cell.shape === 'empty') return [];
    if (Cell.shape === 'straight') return Cell.rotation % 2 === 0 ? [0, 2] : [1, 3];
    return [Cell.rotation, (Cell.rotation + 1) % 4].sort((Left, Right) => Left - Right);
}

function mfDirection(From, To) {
    if (To === From - Size) return 0;
    if (To === From + 1 && Math.floor(From / Size) === Math.floor(To / Size)) return 1;
    if (To === From + Size) return 2;
    if (To === From - 1 && Math.floor(From / Size) === Math.floor(To / Size)) return 3;
    throw new Error('阵段不是相邻格');
}

function mfNeighbor(Index, Direction) {
    const Row = Math.floor(Index / Size);
    const Column = Index % Size;
    const NextRow = Row + [-1, 0, 1, 0][Direction];
    const NextColumn = Column + [0, 1, 0, -1][Direction];
    mfRequire(NextRow >= 0 && NextRow < Size && NextColumn >= 0 && NextColumn < Size, `第${Index}格气路越出棋盘`);
    return NextRow * Size + NextColumn;
}

function mfCellForPorts(Ports) {
    for (const Shape of ['straight', 'corner']) {
        for (let Rotation = 0; Rotation < 4; ++Rotation) {
            const Cell = { shape: Shape, rotation: Rotation, pieceId: null };
            if (mfPorts(Cell).join(',') === [...Ports].sort((Left, Right) => Left - Right).join(',')) return Cell;
        }
    }
    throw new Error('无法构成阵段端口');
}

function mfInitialBoard() {
    const Board = Array.from({ length: Size * Size }, () => ({ shape: 'empty', rotation: 0, pieceId: null }));
    for (let PathIndex = 0; PathIndex < OuterPath.length - 1; ++PathIndex) {
        const Index = OuterPath[PathIndex];
        const Previous = OuterPath[PathIndex === 0 ? OuterPath.length - 2 : PathIndex - 1];
        const Next = OuterPath[PathIndex + 1];
        Board[Index] = mfCellForPorts([mfDirection(Index, Previous), mfDirection(Index, Next)]);
    }
    Board[21].pieceId = 'gather-1';
    Board[15].pieceId = 'condense-1';
    Board[10].pieceId = 'soothe-1';
    return Board;
}

function mfDisorderLimit(Run) { return Run.settings.talent === 'daoBody' ? 11 : 9; }
function mfSootheReduction(Run) { return Run.settings.talent === 'steady' ? 4 : 3; }
function mfInstalled(Board) { return Board.filter(Cell => Cell.pieceId !== null).length; }
function mfScore(Run) { return Run.qualifiedLaps > 0 ? ScoreBase + Math.min(ScoreCap - ScoreBase, Run.liquid * 2 + Run.stableLaps) : 0; }

function mfReward(Score) {
    const Gain = Math.max(0, Math.min(ScoreCap, Score) - ScoreBase);
    return { baseHpGain: Gain, battleHpEquivalent: Gain * 24 / 10 };
}

function mfCatalog(Run) {
    return [
        { kind: 'gather', label: PieceLabels.gather, hint: '每次经过蓄1气；已有2气时改为放出4气（上限12）、清空蓄气并增加2紊乱。' },
        { kind: 'condense', label: PieceLabels.condense, hint: '经过时气量至少3才耗2气，凝成本圈1元液并增加1紊乱；不足则跳过。' },
        { kind: 'soothe', label: PieceLabels.soothe, hint: `经过时降低${mfSootheReduction(Run)}紊乱，最低为0。` },
    ];
}

function mfSummary(Run) {
    if (Run.status === 'success') return `结基完成：${mfScore(Run)}分，基础生命增加${mfReward(mfScore(Run)).baseHpGain}。`;
    if (Run.status === 'withdrawn') return '已退出闭关，本次不结算根基奖励。';
    if (Run.lapCount === 0) return '首圈前可自由布置。试运只预览，正式运行才消耗息数并结算。';
    return `第${Run.lapCount}圈结束，已贯通${Run.qualifiedLaps}圈；可调整最多2格后再运行，或在贯通后结基。`;
}

export function mfCreateRun(Settings = {}) {
    const Prepared = mfSettings(Settings);
    const Time = BaseTime + (Prepared.core ? CoreTime : 0);
    return {
        revision: 0, status: 'active', settings: Prepared, board: mfInitialBoard(),
        charges: Object.fromEntries(PieceIds.map(Id => [Id, 0])),
        timeLeft: Time, timeTotal: Time, lapCount: 0, power: Math.min(PowerCap, 6 + Prepared.pills),
        disorder: 0, liquid: 0, stableLaps: 0, qualifiedLaps: 0,
        history: [], lastLap: null,
    };
}

export function mfGetView(Run) {
    const Catalog = mfCatalog(Run);
    const Score = mfScore(Run);
    return structuredClone({
        revision: Run.revision, status: Run.status,
        phase: Run.status !== 'active' ? 'ended' : Run.lapCount === 0 ? 'building' : 'paused',
        settings: Run.settings,
        board: Run.board.map((Cell, Index) => {
            const FixedCell = Fixed.find(Entry => Entry.index === Index);
            return { index: Index, ...Cell, ports: mfPorts(Cell), fixed: Boolean(FixedCell), label: FixedCell?.label || '' };
        }),
        fixed: Fixed,
        pieces: PieceIds.map(Id => {
            const Kind = Id.split('-')[0];
            return { id: Id, kind: Kind, label: PieceLabels[Kind], charge: Run.charges[Id], capacity: GatherCapacity,
                hint: Catalog.find(Entry => Entry.kind === Kind).hint };
        }),
        installedPieces: mfInstalled(Run.board),
        timeLeft: Run.timeLeft, timeTotal: Run.timeTotal, lapCount: Run.lapCount,
        power: Run.power, disorder: Run.disorder, liquid: Run.liquid,
        stableLaps: Run.stableLaps, qualifiedLaps: Run.qualifiedLaps, score: Score,
        reward: mfReward(Run.status === 'success' ? Score : 0),
        sealReward: mfReward(Run.status !== 'withdrawn' && Run.qualifiedLaps > 0 ? Score : 0),
        history: Run.history, lastLap: Run.lastLap,
        rules: { size: Size, maxEdits: MaxEdits, maxFunctionalPieces: MaxFunctionalPieces,
            powerCap: PowerCap, disorderLimit: mfDisorderLimit(Run), gatePowerMinimum: GatePowerMinimum,
            gatherCapacity: GatherCapacity, gatherRelease: GatherRelease,
            condensePowerMinimum: CondensePowerMinimum, condensePowerCost: CondensePowerCost,
            sootheReduction: mfSootheReduction(Run), returnPower: ReturnPower,
            stableDisorderMaximum: StableDisorderMaximum, scoreBase: ScoreBase, scoreCap: ScoreCap,
            firstLapEditsFree: true, catalog: Catalog,
            preparationHint: `初始气量${Math.min(PowerCap, 6 + Run.settings.pills)}（每颗筑基丹加1，上限12）；总时限${Run.timeTotal}息（灵核加8）。道体将关窍紊乱界限提高至11；稳健使抚息每次降低4紊乱。` },
        summary: mfSummary(Run),
    });
}

function mfApplyDraft(Run, Changes) {
    mfRequire(Array.isArray(Changes), '改格列表必须是数组');
    mfRequire(Changes.length <= Size * Size, '改格列表过长');
    const Seen = new Set();
    const Board = structuredClone(Run.board);
    for (const Change of Changes) {
        mfObject(Change, ['index', 'shape', 'rotation', 'pieceId'], '改格', ['index', 'shape', 'rotation', 'pieceId']);
        mfRequire(Number.isInteger(Change.index) && Change.index >= 0 && Change.index < Size * Size, '格子编号须为0至24的整数');
        mfRequire(!Seen.has(Change.index), '改格列表不能重复同一格');
        Seen.add(Change.index);
        mfRequire(!Fixed.some(Entry => Entry.index === Change.index), '固定丹田与关窍不可修改');
        mfRequire(['empty', 'straight', 'corner'].includes(Change.shape), '未知阵段形状');
        mfRequire(Number.isInteger(Change.rotation) && Change.rotation >= 0 && Change.rotation < 4, '旋转须为0至3的整数');
        mfRequire(Change.pieceId === null || PieceIds.includes(Change.pieceId), '未知功能件ID');
        mfRequire(Change.shape !== 'empty' || Change.pieceId === null, '空格不能安装功能件');
        const Rotation = Change.shape === 'empty' ? 0 : Change.shape === 'straight' ? Change.rotation % 2 : Change.rotation;
        Board[Change.index] = { shape: Change.shape, rotation: Rotation, pieceId: Change.pieceId };
    }
    const Locations = new Map();
    for (let Index = 0; Index < Board.length; ++Index) {
        const Id = Board[Index].pieceId;
        if (Id === null) continue;
        mfRequire(!Locations.has(Id), `功能件${Id}只能安装一次`);
        Locations.set(Id, Index);
    }
    mfRequire(Locations.size <= MaxFunctionalPieces, `最多同时安装${MaxFunctionalPieces}个功能件`);
    const NetEdits = Board.reduce((Count, Cell, Index) => {
        const Previous = Run.board[Index];
        return Count + (Cell.shape !== Previous.shape || Cell.rotation !== Previous.rotation || Cell.pieceId !== Previous.pieceId ? 1 : 0);
    }, 0);
    mfRequire(Run.lapCount === 0 || NetEdits <= MaxEdits, `首圈后每圈最多修改${MaxEdits}个净变格`);
    const ResetIds = PieceIds.filter(Id => Run.board.findIndex(Cell => Cell.pieceId === Id) !== Board.findIndex(Cell => Cell.pieceId === Id));
    return { Board, EditCost: Run.lapCount === 0 ? 0 : NetEdits, NetEdits, ResetIds };
}

function mfPathError(Message, Path, Index, RelatedIndex) {
    const Failure = new Error(Message);
    Failure.path = [...Path];
    Failure.blockedCell = Index;
    Failure.issues = [...new Set([RelatedIndex, Index].filter(Cell => Number.isInteger(Cell)))].map(Cell => ({ index: Cell, message: Message }));
    throw Failure;
}

function mfTracePath(Board) {
    const Path = [20];
    const Seen = new Set([20]);
    let Previous = 20;
    let Current = 21;
    while (true) {
        if (Current === 20) {
            if (mfDirection(20, Previous) !== 0) mfPathError('气流必须从丹田北口回归', [...Path, 20], 20, Previous);
            for (const Gate of Fixed.slice(1)) {
                if (!Seen.has(Gate.index)) mfPathError(`气路未经过${Gate.label}`, [...Path, 20], Gate.index);
            }
            Path.push(20);
            return Path;
        }
        if (Seen.has(Current)) mfPathError(`第${Current}格在回丹田前重复经过`, [...Path, Current], Current, Previous);
        Seen.add(Current);
        Path.push(Current);
        const Ports = mfPorts(Board[Current]);
        const Entry = mfDirection(Current, Previous);
        if (!Ports.includes(Entry)) mfPathError(`第${Current}格端口不匹配，气路中断`, Path, Current, Previous);
        const Exit = Ports.find(Port => Port !== Entry);
        if (Exit === undefined) mfPathError(`第${Current}格没有出口`, Path, Current, Previous);
        Previous = Current;
        try { Current = mfNeighbor(Current, Exit); }
        catch (Error) { mfPathError(Error.message, Path, Current); }
    }
}

function mfRunLap(Run, Changes) {
    const Draft = mfApplyDraft(Run, Changes);
    const Path = mfTracePath(Draft.Board);
    const Cost = { time: Path.length - 1 + Draft.EditCost, edits: Draft.EditCost, travel: Path.length - 1 };
    mfRequire(Run.timeLeft >= Cost.time, `剩余${Run.timeLeft}息，不足本圈所需${Cost.time}息`);
    Run.board = Draft.Board;
    for (const Id of Draft.ResetIds) Run.charges[Id] = 0;
    Run.timeLeft -= Cost.time;
    const Trace = [{ index: 20, label: '丹田', power: Run.power, disorder: Run.disorder, event: '从丹田东口启行。' }];
    const GateChecks = [];
    let LiquidProduced = 0;
    for (const Index of Path.slice(1, -1)) {
        const Cell = Run.board[Index];
        const Gate = Fixed.find(Entry => Entry.index === Index);
        let Event = '气流经过。';
        let Label = '普通阵段';
        if (Gate) {
            const Matched = Run.power >= GatePowerMinimum && Run.disorder < mfDisorderLimit(Run);
            GateChecks.push({ index: Index, name: Gate.label, qualified: Matched, power: Run.power, disorder: Run.disorder });
            if (Matched) Run.power -= 1;
            Run.disorder += 1;
            Label = Gate.label;
            Event = `${Gate.label}${Matched ? '吻合，耗1气' : '失配，未扣关窍气量'}；紊乱增加1。`;
        } else if (Cell.pieceId) {
            const Kind = Cell.pieceId.split('-')[0];
            Label = PieceLabels[Kind];
            if (Kind === 'gather') {
                if (Run.charges[Cell.pieceId] < GatherCapacity) {
                    Run.charges[Cell.pieceId] += 1;
                    Event = `蓄气增加1，现为${Run.charges[Cell.pieceId]}/2。`;
                } else {
                    Run.power = Math.min(PowerCap, Run.power + GatherRelease);
                    Run.charges[Cell.pieceId] = 0;
                    Run.disorder += 2;
                    Event = '放出4气（气量上限12），蓄气清零，紊乱增加2。';
                }
            } else if (Kind === 'condense') {
                if (Run.power >= CondensePowerMinimum) {
                    Run.power -= CondensePowerCost;
                    LiquidProduced += 1;
                    Run.disorder += 1;
                    Event = '耗2气凝成1元液，紊乱增加1；元液须整圈贯通才入账。';
                } else Event = '气量不足3，跳过凝液。';
            } else {
                Run.disorder = Math.max(0, Run.disorder - mfSootheReduction(Run));
                Event = `抚息降低${mfSootheReduction(Run)}紊乱，最低为0。`;
            }
        }
        Trace.push({ index: Index, label: Label, power: Run.power, disorder: Run.disorder, event: Event,
            ...(Cell.pieceId?.startsWith('gather-') ? { charge: Run.charges[Cell.pieceId] } : {}) });
    }
    Run.power = Math.min(PowerCap, Run.power + ReturnPower);
    const Vented = Run.power > 10 || Run.disorder >= mfDisorderLimit(Run);
    let LapLiquid = LiquidProduced;
    if (Vented) {
        Run.power = Math.max(0, Run.power - 2);
        Run.disorder = Math.max(0, Run.disorder - 3);
        LapLiquid = Math.max(0, LapLiquid - 1);
    }
    Trace.push({ index: 20, label: '丹田', power: Run.power, disorder: Run.disorder,
        event: `回丹田补4气（上限12）。${Vented ? '触发逸气：减2气、减3紊乱，本圈元液减少1（最低0）。' : '未触发逸气。'}` });
    const Qualified = GateChecks.length === 3 && GateChecks.every(Check => Check.qualified);
    const Stable = Qualified && Run.disorder <= StableDisorderMaximum;
    const LiquidGain = Qualified ? LapLiquid : 0;
    Run.lapCount += 1;
    if (Qualified) Run.qualifiedLaps += 1;
    if (Stable) Run.stableLaps += 1;
    Run.liquid += LiquidGain;
    const Outcome = { qualified: Qualified, liquidProduced: LiquidProduced, liquidGain: LiquidGain,
        liquidLost: LiquidProduced - LiquidGain, stable: Stable, vented: Vented, gateChecks: GateChecks };
    Run.lastLap = { path: Path, trace: Trace, cost: Cost, outcome: Outcome };
    const ChangeText = [
        `本圈行进${Cost.travel}息，改格${Cost.edits}息，共耗${Cost.time}息，剩余${Run.timeLeft}息。`,
        `三关窍${Qualified ? '均吻合，整圈贯通' : '存在失配，本圈元液散失'}。`,
        `本圈凝成${LiquidProduced}元液，实际入账${LiquidGain}元液；${Stable ? '增加1稳定圈' : '未增加稳定圈'}。`,
        `结算后气量${Run.power}、紊乱${Run.disorder}，预计根基${mfScore(Run)}分。`,
    ];
    if (Draft.ResetIds.some(Id => Id.startsWith('gather-'))) ChangeText.unshift('移动或拆除的蓄气穴已清空未兑蓄气。');
    Run.history.push({ lap: Run.lapCount, text: ChangeText.join(' ') });
    return { changes: ChangeText, path: Path, trace: Trace, cost: Cost, outcome: Outcome };
}

function mfExecute(Run, Action) {
    mfObject(Action, ['kind', 'changes'], '动作', ['kind']);
    mfRequire(['run', 'seal', 'withdraw'].includes(Action.kind), '未知动作');
    mfRequire(Run.status === 'active', '本局已经结束');
    if (Action.kind === 'run') {
        mfRequire(Object.hasOwn(Action, 'changes'), '运行动作缺少改格列表');
    } else mfRequire(!Object.hasOwn(Action, 'changes'), '结基或退出不能携带改格列表');
    const Next = structuredClone(Run);
    let Report;
    if (Action.kind === 'run') Report = mfRunLap(Next, Action.changes);
    else {
        if (Action.kind === 'seal') mfRequire(Next.qualifiedLaps > 0, '至少整圈贯通一次后才能结基');
        Next.status = Action.kind === 'seal' ? 'success' : 'withdrawn';
        const Text = Action.kind === 'seal'
            ? `结基完成：${mfScore(Next)}分，基础生命增加${mfReward(mfScore(Next)).baseHpGain}，斗法生命等效增加${mfReward(mfScore(Next)).battleHpEquivalent}。`
            : '退出闭关，本次根基奖励为0。';
        Next.history.push({ lap: Next.lapCount, text: Text });
        Report = { changes: [Text], path: [], trace: [], cost: { time: 0, edits: 0, travel: 0 }, outcome: null };
    }
    Next.revision += 1;
    return { Next, Report };
}

export function mfAct(Run, Action) { return mfExecute(Run, Action).Next; }

export function mfQuote(Run, Action) {
    try {
        const { Next, Report } = mfExecute(Run, Action);
        return { enabled: true, reason: '', ...Report, after: mfGetView(Next), blockedCell: null, issues: [] };
    } catch (Error) {
        return { enabled: false, reason: Error.message || '动作无法执行', changes: [], path: Error.path || [], trace: [],
            after: null, cost: null, outcome: null, blockedCell: Error.blockedCell ?? null, issues: Error.issues || [] };
    }
}
