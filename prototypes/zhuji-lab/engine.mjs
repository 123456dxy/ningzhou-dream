// Independent prototype rules. No game save, clock, shared RNG, or browser dependency.
const Elements = Object.freeze(['gold', 'wood', 'water', 'fire', 'earth']);
const Names = Object.freeze({ gold: '金', wood: '木', water: '水', fire: '火', earth: '土' });
const Generates = Object.freeze({ wood: 'fire', fire: 'earth', earth: 'gold', gold: 'water', water: 'wood' });
const Talents = Object.freeze(['none', 'daoBody', 'steady', ...Elements]);
const SettingKeys = Object.freeze(['pills', 'core', 'talent', 'tide']);
const DirectionalTides = Object.freeze(['tidal', 'tidalSmooth']);
const ElementScores = Object.freeze({ basic: 3, same: 6, generate: 8, five: 12 });
const ElementReturns = Object.freeze({ gather: 3 });
const SegmentNames = Object.freeze(['任脉', '督脉', '冲脉']);
const MeridianLabels = Object.freeze({ safe: '稳妥通脉', nurture: '温养经脉', refine: '精炼经脉' });

function mfClone(Value) { return structuredClone(Value); }
function mfEmptyResources() { return Object.fromEntries(Elements.map(Element => [Element, 0])); }
function mfCount(Resources) { return Elements.reduce((Total, Element) => Total + Resources[Element], 0); }
function mfDirectional(Run) { return Run.mode === 'elements' && DirectionalTides.includes(Run.settings.tide); }
function mfRequire(Condition, Message) { if (!Condition) throw new Error(Message); }
function mfPlainObject(Value) {
    return Value !== null && typeof Value === 'object' && !Array.isArray(Value) &&
        (Object.getPrototypeOf(Value) === Object.prototype || Object.getPrototypeOf(Value) === null);
}

function mfSettings(Settings, Mode) {
    mfRequire(mfPlainObject(Settings), '准备设置必须是对象。');
    const AllowedKeys = Mode === 'elements' ? SettingKeys : SettingKeys.filter(Key => Key !== 'tide');
    const Unknown = Object.keys(Settings).filter(Key => !AllowedKeys.includes(Key));
    mfRequire(Unknown.length === 0, `不支持的准备设置：${Unknown.join('、')}。`);
    const Result = { pills: 0, core: false, talent: 'none', ...(Mode === 'elements' ? { tide: 'tidal' } : {}), ...Settings };
    mfRequire(Number.isInteger(Result.pills) && Result.pills >= 0 && Result.pills <= 6, '筑基丹数量必须是0至6的整数。');
    mfRequire(typeof Result.core === 'boolean', '灵核准备必须为布尔值。');
    mfRequire(Talents.includes(Result.talent), '未知天赋。');
    if (Mode === 'elements') mfRequire(DirectionalTides.includes(Result.tide), '未知数量灵潮。');
    return Result;
}

function mfDrawFiveWay(Count, DrawSeed) {
    const Values = [];
    let State = DrawSeed;
    for (let Index = 0; Index < Count; ++Index) {
        // Reject the single excess uint32 value so all five residues have equal size.
        let Word;
        do {
            State = (State + 0x9e3779b9) >>> 0;
            Word = Math.imul(State ^ (State >>> 16), 0x21f0aaad);
            Word = Math.imul(Word ^ (Word >>> 15), 0x735a2d97);
            Word = (Word ^ (Word >>> 15)) >>> 0;
        } while (Word === 4294967295);
        Values.push(Word % 5);
    }
    return { Values, State };
}

function mfDrawElements(Count, DrawSeed) {
    const Drawn = mfDrawFiveWay(Count, DrawSeed);
    const Draws = Drawn.Values.map(Value => Elements[Value]);
    return { Draws, Resources: mfResourceBundle(Draws), State: Drawn.State };
}

function mfResourceBundle(Tokens) {
    const Resources = mfEmptyResources();
    for (const Element of Tokens) Resources[Element]++;
    return Resources;
}

function mfAddBundle(Run, Bundle) { for (const Element of Elements) Run.resources[Element] += Bundle[Element]; }

function mfNaturalCount(Settings, Round) {
    const Counts = Settings.tide === 'tidalSmooth' ? [6, 6, 6, 6, 6, 8, 4, 6] : [6, 6, 8, 4, 6, 8, 4, 6];
    return Counts[Round - 1];
}

function mfElementProbabilities() { return Object.fromEntries(Elements.map(Element => [Element, 0.2])); }

function mfNextDrawCount(Run) {
    if (Run.mode !== 'elements' || Run.status !== 'active' || Run.round >= Run.maxRounds) return 0;
    return mfNaturalCount(Run.settings, Run.round + 1) + (Run.settings.core && Run.round + 1 === 4 ? 4 : 0) + Run.pending;
}

function mfReceiveElementRound(Run) {
    const NaturalCount = mfNaturalCount(Run.settings, Run.round);
    const Drawn = mfDrawElements(NaturalCount, Run.NaturalRandomState);
    Run.NaturalRandomState = Drawn.State;
    const Resources = Drawn.Resources;
    const CoreCount = Run.settings.core && Run.round === 4 ? 4 : 0;
    if (CoreCount) {
        const Core = mfDrawElements(CoreCount, Run.CoreRandomState);
        Run.CoreRandomState = Core.State;
        for (const Element of Elements) Resources[Element] += Core.Resources[Element];
    }
    const GatherCount = Run.pending;
    if (GatherCount) {
        const Gather = mfDrawElements(GatherCount, Run.GatherRandomState);
        Run.GatherRandomState = Gather.State;
        for (const Element of Elements) Resources[Element] += Gather.Resources[Element];
    }
    mfAddBundle(Run, Resources);
    const Phase = NaturalCount === 8 ? '潮峰' : NaturalCount === 4 ? '退潮' : '平潮';
    const Total = NaturalCount + CoreCount + GatherCount;
    Run.tide.push({ round: Run.round, resources: Resources, total: Total, naturalCount: NaturalCount, coreCount: CoreCount, gatherCount: GatherCount, phase: Phase, label: Phase });
    return Total;
}

function mfNote(Run, Text) { Run.history.push({ round: Run.round, text: Text }); }

function mfScoreReward(Score) {
    const BaseHpGain = Math.max(0, Math.min(120, Score) - 100);
    return { baseHpGain: BaseHpGain, battleHpEquivalent: BaseHpGain * 24 / 10 };
}

function mfEnterMeridianSegment(Run) {
    const Drawn = mfDrawFiveWay(2, Run.MeridianRandomState);
    Run.MeridianRandomState = Drawn.State;
    Run.CurrentSegment = { name: SegmentNames[Run.round - 1], nurtureBonus: 4 + Drawn.Values[0], refineBonus: 8 + Drawn.Values[1] };
}

function mfMeridianRoute(Run, Kind) {
    mfRequire(Object.hasOwn(MeridianLabels, Kind), '未知经脉路线。');
    mfRequire(Run.CurrentSegment !== null, '当前没有待完成的经脉段。');
    const Discount = Kind === 'nurture' && Run.settings.talent === 'steady' && !Run.SteadyNurtureUsed;
    const CostSteps = Kind === 'safe' ? 2 : Kind === 'nurture' ? (Discount ? 2 : 3) : 4;
    const ExtraScore = Kind === 'safe' ? 0 : Kind === 'nurture' ? Run.CurrentSegment.nurtureBonus : Run.CurrentSegment.refineBonus;
    return { kind: Kind, label: MeridianLabels[Kind], costSteps: CostSteps, extraScore: ExtraScore,
        hint: `耗${CostSteps}步 · 额外底蕴＋${ExtraScore}${Discount ? ' · 稳健首次温养减1步' : ''} · 后续须保留${(Run.maxRounds - Run.round) * 2}步`, action: { kind: Kind } };
}

function mfJourneyView(Run) {
    const Current = Run.status === 'active' && Run.CurrentSegment ? {
        ...mfClone(Run.CurrentSegment),
        routes: ['safe', 'nurture', 'refine'].map(Kind => {
            const Route = mfMeridianRoute(Run, Kind);
            return { ...Route, quote: mfQuote(Run, Route.action) };
        }),
    } : null;
    return { segmentNames: [...SegmentNames], totalSteps: Run.TotalSteps, spentSteps: Run.SpentSteps,
        remainingSteps: Run.RemainingSteps, minFutureSteps: Run.status === 'active' ? (Run.maxRounds - Run.round) * 2 : 0,
        completed: mfClone(Run.CompletedSegments), current: Current, preparation: mfClone(Run.Preparation) };
}

export function mfCreateRun(Mode, Settings = {}, DrawSeed = 0x9e3779b9) {
    mfRequire(['elements', 'meridians'].includes(Mode), '未知筑基方案。');
    mfRequire(Number.isInteger(DrawSeed) && DrawSeed >= 0 && DrawSeed <= 4294967295, '内部抽气种子seed须为0至4294967295的整数。');
    const Options = mfSettings(Settings, Mode);
    const Drawn = Mode === 'elements' && Options.pills > 0 ? mfDrawElements(Options.pills * 2, DrawSeed)
        : { Draws: [], Resources: mfEmptyResources() };
    const Run = {
        mode: Mode, revision: 0, status: 'active', round: 1, maxRounds: Mode === 'elements' ? 8 : 3,
        score: Mode === 'elements' ? 0 : 100, grade: '', reward: mfScoreReward(0),
        summary: Mode === 'elements' ? '尚未定型，可查看施术结果；新一轮灵气在推进时随机抽取。' : '从任脉开始，当前段路线收益已揭示；完成三脉后自动结基。', history: [],
        settings: Options, resources: mfEmptyResources(), pillDraws: Drawn.Draws, pillResources: Drawn.Resources,
        NaturalRandomState: Mode === 'elements' ? (DrawSeed ^ 0xa511e9b3) >>> 0 : 0,
        CoreRandomState: Mode === 'elements' ? (DrawSeed ^ 0x63d83595) >>> 0 : 0,
        GatherRandomState: Mode === 'elements' ? (DrawSeed ^ 0x7f4a7c15) >>> 0 : 0,
        actionPoints: Mode === 'elements' ? 3 : 1,
        carryLimit: Options.talent === 'daoBody' ? 10 : 8,
        tide: [], ...(Mode === 'elements' ? { pressure: 0, breath: 0, pressureLimit: Options.talent === 'daoBody' ? 12 : 10 } : {}),
        pending: 0, converted: false, gathered: false, inhaled: false, guarded: false,
    };
    if (Mode === 'elements') {
        const InitialTotal = mfReceiveElementRound(Run);
        for (const Element of Elements) Run.resources[Element] += Run.pillResources[Element];
        mfNote(Run, `第1轮获得${InitialTotal}枚灵气${Options.pills ? `，筑基丹随机抽得${Options.pills * 2}枚` : ''}。`);
    } else {
        const RequestedSteps = 9 + Options.pills + (Options.core ? 1 : 0) + (Options.talent === 'daoBody' ? 1 : 0);
        Run.TotalSteps = Math.min(12, RequestedSteps);
        Run.SpentSteps = 0;
        Run.RemainingSteps = Run.TotalSteps;
        Run.CompletedSegments = [];
        Run.SteadyNurtureUsed = false;
        Run.MeridianRandomState = (DrawSeed ^ 0xc2b2ae35) >>> 0;
        Run.Preparation = { requestedSteps: RequestedSteps, effectiveSteps: Run.TotalSteps, redundantSteps: RequestedSteps - Run.TotalSteps,
            hint: `基础9步，准备后共${RequestedSteps}步，实际生效${Run.TotalSteps}步${RequestedSteps > 12 ? `；超出12步上限的${RequestedSteps - 12}步不计入本局` : '，无冗余准备'}` };
        mfEnterMeridianSegment(Run);
        mfNote(Run, `以${Run.TotalSteps}步入关；三脉贯通后自动结基。${Run.Preparation.hint}。`);
    }
    return Run;
}

function mfAfter(Run) {
    const Keys = ['score', 'actionPoints', 'round', 'status', 'grade', 'reward'];
    if (Run.mode === 'elements') Keys.push('pressure', 'breath', 'resources');
    const After = mfClone(Object.fromEntries(Keys.map(Key => [Key, Run[Key]])));
    if (Run.mode === 'meridians') { After.spentSteps = Run.SpentSteps; After.remainingSteps = Run.RemainingSteps; }
    return After;
}

export function mfGetView(Run) {
    const Keys = ['mode', 'revision', 'status', 'round', 'maxRounds', 'score', 'grade', 'reward', 'summary', 'history', 'settings', 'resources', 'pillDraws', 'pillResources', 'actionPoints', 'carryLimit', 'tide'];
    if (Run.mode === 'elements') Keys.push('pressure', 'breath', 'pressureLimit');
    const View = mfClone(Object.fromEntries(Keys.map(Key => [Key, Run[Key]])));
    View.preview = {
        elementProbabilities: Run.mode === 'elements' ? mfElementProbabilities() : null,
        nextDrawCount: mfNextDrawCount(Run),
        successThreshold: Run.mode === 'elements' ? 60 : 100,
        scoreCap: 120,
        pendingDrawCount: Run.mode === 'elements' ? Run.pending : 0,
        sealReward: Run.status === 'active' ? mfScoreReward(Run.score) : mfClone(Run.reward),
        ...(Run.mode === 'elements' ? { convertAvailable: !Run.converted, gatherAvailable: !Run.gathered,
            inhaleAvailable: !Run.inhaled, guardAvailable: Run.settings.talent === 'steady' && !Run.guarded } : {}),
        grades: Run.mode === 'elements'
            ? [{ score: 60, grade: '平实' }, { score: 80, grade: '稳固' }, { score: 100, grade: '浑厚' }, { score: 110, grade: '圆满' }]
            : [{ score: 100, grade: '平实' }, { score: 108, grade: '稳固' }, { score: 114, grade: '浑厚' }, { score: 120, grade: '圆满' }],
    };
    View.compass = mfCompass(Run);
    if (Run.mode === 'meridians') View.journey = mfJourneyView(Run);
    return View;
}

function mfCompass(Run) {
    const Choices = [];
    const Pending = Run.mode === 'elements' ? Run.pending : 0;
    if (Run.mode !== 'elements') return { choices: Choices, pendingRandomCount: Pending };
    const Add = (Kind, Source, Target, Action) => {
        const Payment = mfResourceBundle(Action.spend ?? []);
        Choices.push({ kind: Kind, source: Source, target: Target, action: Action,
            payment: Payment, hint: mfElementHint(Run, Action, Payment), quote: mfQuote(Run, Action) });
    };
    for (const Source of Elements) Add('basic', Source, null, { kind: 'basic', spend: [Source, Source] });
    for (const Source of Elements) Add('same', Source, null, { kind: 'same', spend: [Source, Source, Source] });
    for (const Source of Elements) Add('generate', Source, null, { kind: 'generate', spend: [Source, Source, Generates[Source], Generates[Source]] });
    Add('five', null, null, { kind: 'five', spend: [...Elements] });
    for (const Source of Elements) Add('gather', Source, null, { kind: 'gather', spend: [Source, Source] });
    for (const Source of Elements) for (const Target of Elements) {
        if (Source === Target) continue;
        const Count = Run.settings.talent === Target && Run.resources[Source] >= 2 ? 2 : 1;
        Add('convert', Source, Target, { kind: 'convert', spend: Array(Count).fill(Source), target: Target });
    }
    return { choices: Choices, pendingRandomCount: Pending };
}

function mfElementHint(Run, Action, Payment) {
    const Paid = Elements.filter(Element => Payment[Element] > 0).map(Element => `${Names[Element]}${Payment[Element]}`).join('、');
    if (Object.hasOwn(ElementScores, Action.kind)) return `${Paid} → 凝练＋${ElementScores[Action.kind]}`;
    if (Action.kind === 'convert') return `${Paid} → ${Names[Action.target]}${Action.spend.length}`;
    if (Action.kind === 'gather') return `${Paid} → 下轮随机${ElementReturns.gather}`;
    return '';
}

function mfFinish(Run, Status, Reason) {
    Run.status = Status;
    Run.actionPoints = 0;
    if (Status === 'success') {
        const Grades = Run.mode === 'elements'
            ? [[60, '平实'], [80, '稳固'], [100, '浑厚'], [110, '圆满']]
            : [[100, '平实'], [108, '稳固'], [114, '浑厚'], [120, '圆满']];
        for (const [Minimum, Grade] of Grades) if (Run.score >= Minimum) {
            Run.grade = Grade;
        }
        Run.reward = mfScoreReward(Run.score);
        Run.summary = `${Reason}；以${Run.score}分筑成${Run.grade}道基，基础生命＋${Run.reward.baseHpGain}，斗法等效生命＋${Run.reward.battleHpEquivalent}。成功后不可补修，本次仅为独立试玩结果。`;
    } else {
        Run.grade = '';
        Run.reward = mfScoreReward(0);
        Run.summary = `${Reason}；本次不形成永久道基奖励，可重新准备后再试。`;
    }
    if (Run.mode === 'meridians') Run.CurrentSegment = null;
    mfNote(Run, Run.summary);
}

function mfSpend(Run, Tokens, Required) {
    mfRequire(Array.isArray(Tokens) && Tokens.length === Required && Tokens.every(Element => Elements.includes(Element)), `请选择恰好${Required}枚合法灵气。`);
    const Payment = mfResourceBundle(Tokens);
    mfRequire(Elements.every(Element => Payment[Element] <= Run.resources[Element]), '所选灵气不足，请重新选择。');
    for (const Element of Elements) Run.resources[Element] -= Payment[Element];
    return Payment;
}

function mfElementAct(Run, Action) {
    const Kind = Action.kind;
    if (Kind === 'withdraw') { mfFinish(Run, 'withdrawn', '主动收功退出'); return; }
    if (Kind === 'seal') {
        mfRequire(Run.score >= 60, '至少60分凝练才能封基。');
        mfFinish(Run, 'success', '提前封基定型');
        return;
    }
    if (Kind === 'next') {
        if (Run.round === Run.maxRounds) {
            mfFinish(Run, Run.score >= 60 ? 'success' : 'failed', Run.score >= 60 ? '八轮功成定型' : '八轮结束，凝练不足60分');
            return;
        }
        if (Action.keep !== undefined) {
            mfRequire(Array.isArray(Action.keep) && Action.keep.length <= Run.carryLimit, `最多保留${Run.carryLimit}枚灵气。`);
            const Before = mfClone(Run.resources);
            const Keep = mfSpend(Run, Action.keep, Action.keep.length);
            Run.resources = Keep;
            mfNote(Run, `保留${mfCount(Keep)}枚灵气，散去${mfCount(Before) - mfCount(Keep)}枚。`);
        } else mfRequire(mfCount(Run.resources) <= Run.carryLimit, `灵气超出上限，请明确选择最多保留${Run.carryLimit}枚。`);
        Run.round++;
        Run.actionPoints = 3;
        Run.converted = false;
        Run.gathered = false;
        const PendingCount = Run.pending;
        const RandomCount = mfReceiveElementRound(Run);
        Run.pending = 0;
        mfNote(Run, `第${Run.round}轮随机抽取${RandomCount}枚灵气${PendingCount ? `（含聚气随机返还${PendingCount}枚）` : ''}，恢复3次操作。`);
        return;
    }
    mfRequire(Run.actionPoints > 0, '本轮操作已用尽，请进入下一轮或封基。');
    let Gain = 0;
    let Text = '';
    if (Kind === 'basic') {
        const Payment = mfSpend(Run, Action.spend, 2);
        if (mfDirectional(Run)) mfRequire(Elements.some(Element => Payment[Element] === 2), '基础凝练需要2枚同系灵气。');
        Gain = ElementScores.basic; Text = `基础凝练，2枚${mfDirectional(Run) ? '同系' : ''}灵气换${Gain}分`;
    } else if (Kind === 'same') {
        const Payment = mfSpend(Run, Action.spend, 3);
        mfRequire(Elements.some(Element => Payment[Element] === 3), '同气凝练需要3枚同系灵气。');
        Gain = ElementScores.same; Text = `同气凝练，3枚同系灵气换${Gain}分`;
    } else if (Kind === 'generate') {
        const Payment = mfSpend(Run, Action.spend, 4);
        mfRequire(Elements.some(Element => Payment[Element] === 2 && Payment[Generates[Element]] === 2), '相生凝练需要相生两系各2枚灵气。');
        Gain = ElementScores.generate; Text = `相生凝练，两系各2枚换${Gain}分`;
    } else if (Kind === 'five') {
        const Payment = mfSpend(Run, Action.spend, 5);
        mfRequire(Elements.every(Element => Payment[Element] === 1), '五气归一需要五行各1枚灵气。');
        Gain = ElementScores.five; Text = `五气归一，五行各1枚换${Gain}分`;
    } else if (Kind === 'convert') {
        mfRequire(!Run.converted, '每轮仅可调息一次。');
        mfRequire(Elements.includes(Action.target), '请选择合法的调息目标五行。');
        const Count = Array.isArray(Action.spend) ? Action.spend.length : 0;
        mfRequire(Count === 1 || (Count === 2 && Run.settings.talent === Action.target), '普通调息为1换1，本系灵根转本系可2换2。');
        mfRequire(Action.spend.every(Element => Element !== Action.target), '调息必须转换为不同五行。');
        const Payment = mfSpend(Run, Action.spend, Count);
        if (mfDirectional(Run) && Count === 2) mfRequire(Elements.some(Element => Payment[Element] === 2), '本系灵根2换2需要2枚同系来源灵气。');
        Run.resources[Action.target] += Count;
        Run.converted = true;
        Text = `调息，将${Count}枚灵气转为${Names[Action.target]}灵气`;
    } else if (Kind === 'gather') {
        mfRequire(!Run.gathered, '每轮仅可聚气一次。');
        mfRequire(Run.round < Run.maxRounds, '最后一轮已无下轮可聚气。');
        const Payment = mfSpend(Run, Action.spend, 2);
        const Source = Elements.find(Element => Payment[Element] === 2);
        mfRequire(Source !== undefined, '聚气需要2枚同系灵气。');
        Text = `聚气消耗2枚${Names[Source]}灵气，下轮随机返还${ElementReturns.gather}枚（五行各20%，允许抽回原系）`;
        Run.pending += ElementReturns.gather; Run.gathered = true;
    } else throw new Error('未知五行方案操作。');
    Run.score = Math.min(120, Run.score + Gain);
    Run.actionPoints--;
    mfNote(Run, `${Text}，当前凝练${Run.score}分。`);
}

function mfMeridianAct(Run, Action, IsQuote) {
    const Kind = Action.kind;
    if (Kind === 'withdraw') { mfFinish(Run, 'withdrawn', '主动收功退出'); return; }
    mfRequire(Kind !== 'seal', '三脉贯通后自动结基，无需手动封基。');
    const Route = mfMeridianRoute(Run, Kind);
    const MinFutureSteps = (Run.maxRounds - Run.round) * 2;
    mfRequire(Run.RemainingSteps >= Route.costSteps + MinFutureSteps,
        `此路线需${Route.costSteps}步，并须为后续经脉保留${MinFutureSteps}步；当前仅余${Run.RemainingSteps}步。`);
    Run.RemainingSteps -= Route.costSteps;
    Run.SpentSteps += Route.costSteps;
    if (Kind === 'nurture' && Run.settings.talent === 'steady') Run.SteadyNurtureUsed = true;
    const Name = Run.CurrentSegment.name;
    Run.CompletedSegments.push({ name: Name, kind: Kind, label: Route.label, costSteps: Route.costSteps, extraScore: Route.extraScore });
    Run.score = Math.min(120, Run.score + Route.extraScore);
    mfNote(Run, `${Name}采用${Route.label}，耗${Route.costSteps}步，额外底蕴＋${Route.extraScore}；预计底蕴${Run.score}，剩余${Run.RemainingSteps}步。`);
    if (Run.round === Run.maxRounds) mfFinish(Run, 'success', '三脉贯通，自动结基');
    else {
        Run.round++;
        if (IsQuote) Run.CurrentSegment = null;
        else mfEnterMeridianSegment(Run);
    }
}

function mfExecute(Run, Action, IsQuote = false) {
    mfRequire(mfPlainObject(Run) && ['elements', 'meridians'].includes(Run.mode), '无效的试玩状态。');
    mfRequire(Run.status === 'active', '本次筑基已经结束，请重新准备。');
    mfRequire(mfPlainObject(Action) && typeof Action.kind === 'string', '操作请求必须包含kind。');
    mfRequire(Action.kind !== 'elixir', '引灵丹及对应术式已取消。');
    const Allowed = Run.mode === 'elements'
        ? Action.kind === 'convert' ? ['kind', 'spend', 'target']
            : ['basic', 'same', 'generate', 'five', 'gather'].includes(Action.kind) ? ['kind', 'spend']
                : Action.kind === 'next' ? ['kind', 'keep'] : ['kind']
        : ['kind'];
    mfRequire(Object.keys(Action).every(Key => Allowed.includes(Key)), '操作包含不适用或未知字段。');
    const Next = mfClone(Run);
    if (Next.mode === 'elements') mfElementAct(Next, Action);
    else mfMeridianAct(Next, Action, IsQuote);
    Next.revision++;
    return Next;
}

export function mfAct(Run, Action) { return mfExecute(Run, Action); }

export function mfQuote(Run, Action) {
    try {
        const Next = mfExecute(Run, Action, true);
        const Quote = { enabled: true, reason: '', after: mfAfter(Next), changes: Next.history.slice(Run.history.length).map(Entry => Entry.text) };
        if (Run.mode === 'elements' && Action.kind === 'next' && Run.round < Run.maxRounds) {
            delete Quote.after.resources;
            Quote.randomDraw = { count: mfNextDrawCount(Run), elementProbabilities: mfElementProbabilities() };
        }
        return Quote;
    } catch (Failure) {
        return { enabled: false, reason: Failure.message || '无法执行此操作。', after: mfAfter(Run), changes: [] };
    }
}
