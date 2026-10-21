import Assert from 'node:assert/strict';
import { test as mfTest } from 'node:test';
import { mfCreateRun, mfGetView, mfAct, mfQuote } from './engine.mjs';

const Elements = ['gold', 'wood', 'water', 'fire', 'earth'];
const Names = { gold: '金', wood: '木', water: '水', fire: '火', earth: '土' };
function mfEmpty() { return { gold: 0, wood: 0, water: 0, fire: 0, earth: 0 }; }
function mfTotal(Bundle) { return Object.values(Bundle).reduce((Sum, Count) => Sum + Count, 0); }
function mfTokens(Bundle) { return Elements.flatMap(Element => Array(Bundle[Element]).fill(Element)); }
function mfSum(Left, Right) { return Object.fromEntries(Elements.map(Element => [Element, Left[Element] + Right[Element]])); }
function mfNaturalTotals(Run) { return Run.tide.reduce((Sum, Entry) => mfSum(Sum, Entry.resources), mfEmpty()); }
function mfPrepared(Seed = 123) { return mfCreateRun('elements', { pills: 6 }, Seed); }
function mfRevealAll(Run) {
    while (Run.round < Run.maxRounds) Run = mfAct(Run, { kind: 'next', keep: [] });
    return Run;
}

function mfGreedy(Run) {
    while (Run.status === 'active') {
        const Choices = mfGetView(Run).compass.choices.filter(Choice => ['basic', 'same', 'generate', 'five'].includes(Choice.kind) && Choice.quote.enabled);
        Choices.sort((Left, Right) => Right.quote.after.score - Left.quote.after.score);
        Run = Choices.length ? mfAct(Run, Choices[0].action) : mfAct(Run, { kind: 'next', keep: mfTokens(Run.resources).slice(0, Run.carryLimit) });
    }
    return Run;
}

mfTest('A reveals only the current round and announces only the next random count', () => {
    for (const Tide of ['tidal', 'tidalSmooth']) {
        const Run = mfCreateRun('elements', { tide: Tide }, 42);
        const View = mfGetView(Run);
        Assert.deepEqual(View.preview.elementProbabilities, { gold: 0.2, wood: 0.2, water: 0.2, fire: 0.2, earth: 0.2 });
        Assert.equal(Run.tide.length, 1);
        Assert.equal(View.tide.length, 1);
        Assert.equal(View.preview.nextDrawCount, 6);
        Assert.equal(Object.hasOwn(View.compass, 'incoming'), false);
        for (const Entry of Run.tide) {
            Assert.equal(mfTotal(Entry.resources), Entry.total);
            Assert.equal(Entry.label, Entry.phase);
            Assert.ok(['平潮', '潮峰', '退潮'].includes(Entry.label));
        }
    }
    Assert.equal(mfCreateRun('elements').settings.tide, 'tidal');
    Assert.equal(Object.hasOwn(mfCreateRun('meridians').settings, 'tide'), false);
    for (const Tide of ['stable', 'balanced', 'changing', 'tidalMetal']) Assert.throws(() => mfCreateRun('elements', { tide: Tide }), Error);
    for (const Tide of ['tidal', 'tidalSmooth']) Assert.throws(() => mfCreateRun('meridians', { tide: Tide }), Error);
});

mfTest('committed rounds reproduce by seed and quantity smoothing preserves total five-element counts', () => {
    for (const Seed of [0, 1, 123, 4294967295]) {
        const Run = mfCreateRun('elements', { tide: 'tidal' }, Seed);
        Assert.deepEqual(Run, mfCreateRun('elements', { tide: 'tidal' }, Seed));
        const Revealed = mfRevealAll(Run);
        const Smooth = mfRevealAll(mfCreateRun('elements', { tide: 'tidalSmooth' }, Seed));
        Assert.deepEqual(mfNaturalTotals(Revealed), mfNaturalTotals(Smooth));
        Assert.equal(mfTotal(mfNaturalTotals(Revealed)), 48);
        Assert.deepEqual(Revealed.tide.map(Entry => Entry.naturalCount), [6, 6, 8, 4, 6, 8, 4, 6]);
        Assert.deepEqual(Smooth.tide.map(Entry => Entry.naturalCount), [6, 6, 6, 6, 6, 8, 4, 6]);
    }
    const Forecasts = Array.from({ length: 32 }, (_, Seed) => mfCreateRun('elements', {}, Seed).tide);
    Assert.ok(new Set(Forecasts.map(Tide => JSON.stringify(Tide))).size >= 16);
    Assert.deepEqual(new Set(Forecasts.flatMap(Tide => Tide.flatMap(Entry => mfTokens(Entry.resources)))), new Set(Elements));
});

mfTest('natural, pill and core streams do not disturb one another or depend on tide partition', () => {
    const Bare = mfCreateRun('elements', {}, 123);
    const Prepared = mfCreateRun('elements', { pills: 6, core: true }, 123);
    const Smooth = mfCreateRun('elements', { tide: 'tidalSmooth', pills: 6, core: true }, 123);
    Assert.deepEqual(Bare.tide, Prepared.tide);
    Assert.deepEqual(Prepared.pillDraws, Smooth.pillDraws);
    Assert.deepEqual(Prepared.pillResources, Smooth.pillResources);
    Assert.equal(Object.hasOwn(mfGetView(Prepared), 'coreResources'), false);
    const BareAll = mfRevealAll(Bare);
    const PreparedAll = mfRevealAll(Prepared);
    const SmoothAll = mfRevealAll(Smooth);
    for (let Index = 0; Index < 8; ++Index) if (Index !== 3) Assert.deepEqual(BareAll.tide[Index].resources, PreparedAll.tide[Index].resources);
    Assert.equal(PreparedAll.tide[3].total - BareAll.tide[3].total, 4);
    Assert.equal(PreparedAll.tide[3].coreCount, 4);
    Assert.deepEqual(mfNaturalTotals(PreparedAll), mfNaturalTotals(SmoothAll));
    Assert.deepEqual(Prepared.pillDraws, ['fire', 'fire', 'earth', 'gold', 'water', 'water', 'wood', 'gold', 'wood', 'gold', 'gold', 'earth']);
});

mfTest('actual next appends one realized round and delivers its exact random counts', () => {
    for (const Tide of ['tidal', 'tidalSmooth']) {
        let Run = mfCreateRun('elements', { tide: Tide }, 42);
        let Total = 0;
        for (let Round = 1; Round <= 8; ++Round) {
            Assert.deepEqual(Run.resources, Run.tide[Round - 1].resources);
            Assert.equal(Run.tide.length, Round);
            Total += mfTotal(Run.resources);
            if (Round < 8) Run = mfAct(Run, { kind: 'next', keep: [] });
        }
        Assert.equal(Total, 48);
    }
});

mfTest('pill draws are traceable, reproducible, allow repeats and add exactly two per pill', () => {
    for (const Pills of [0, 1, 3, 6]) {
        const Run = mfCreateRun('elements', { pills: Pills }, 123456);
        Assert.equal(Run.pillDraws.length, Pills * 2);
        Assert.ok(Run.pillDraws.every(Element => Elements.includes(Element)));
        Assert.equal(mfTotal(Run.pillResources), Pills * 2);
        for (const Element of Elements) {
            Assert.equal(Run.pillResources[Element], Run.pillDraws.filter(Draw => Draw === Element).length);
            Assert.equal(Run.resources[Element], Run.tide[0].resources[Element] + Run.pillResources[Element]);
        }
    }
    const Runs = Array.from({ length: 128 }, (_, Seed) => mfCreateRun('elements', { pills: 1 }, Seed));
    Assert.ok(new Set(Runs.map(Run => Run.pillDraws.join(','))).size >= 10);
    Assert.ok(Runs.some(Run => Run.pillDraws[0] === Run.pillDraws[1]));
    Assert.deepEqual(mfCreateRun('meridians', { pills: 6 }, 0).pillDraws, []);
    Assert.deepEqual(mfCreateRun('meridians', { pills: 6 }, 4294967295).pillResources, mfEmpty());
});

mfTest('views, ordinary actions and rejections never advance hidden draw states', () => {
    const Run = mfCreateRun('elements', { pills: 6, core: true }, 123);
    const Before = JSON.stringify(Run);
    for (let Repeat = 0; Repeat < 5; ++Repeat) {
        const View = mfGetView(Run);
        const Choice = View.compass.choices.find(Entry => Entry.quote.enabled && Entry.kind === 'basic');
        Assert.ok(Choice);
        Assert.equal(mfQuote(Run, Choice.action).enabled, true);
        const Next = mfAct(Run, Choice.action);
        for (const Key of ['pillDraws', 'pillResources', 'NaturalRandomState', 'CoreRandomState', 'GatherRandomState', 'tide']) Assert.deepEqual(Next[Key], Run[Key]);
        Assert.throws(() => mfAct(Run, { kind: 'unknown' }), Error);
        View.pillDraws.push('unknown');
        View.pillResources.gold = 999;
        Assert.equal(Object.hasOwn(View, 'NaturalRandomState'), false);
        Assert.equal(Object.hasOwn(View, 'CoreRandomState'), false);
        Assert.equal(Object.hasOwn(View, 'GatherRandomState'), false);
        View.tide[0].resources.earth = 999;
    }
    Assert.equal(JSON.stringify(Run), Before);
});

mfTest('quote after equals actual settlement and leaves the original run intact in both modes', () => {
    for (const Mode of ['elements', 'meridians']) {
        const Run = mfCreateRun(Mode);
        const Before = JSON.stringify(Run);
        const Action = Mode === 'elements' ? mfGetView(Run).compass.choices.find(Choice => Choice.kind === 'basic' && Choice.quote.enabled).action : { kind: 'safe' };
        const Quote = mfQuote(Run, Action);
        Assert.equal(Quote.enabled, true);
        const Next = mfAct(Run, Action);
        const View = mfGetView(Next);
        for (const Key of Object.keys(Quote.after)) Assert.deepEqual(Quote.after[Key], Object.hasOwn(View, Key) ? View[Key] : View.journey?.[Key]);
        Assert.equal(JSON.stringify(Run), Before);
        Assert.equal(Next.revision, 1);
    }
});

mfTest('carry cap requires explicit legal selection and preserves precisely those counts', () => {
    const Run = mfPrepared();
    const Before = JSON.stringify(Run);
    Assert.equal(mfTotal(Run.resources), 18);
    Assert.throws(() => mfAct(Run, { kind: 'next' }), /保留/);
    const Keep = mfTokens(Run.resources).slice(0, 3);
    const Counts = mfEmpty();
    for (const Element of Keep) Counts[Element]++;
    const Next = mfAct(Run, { kind: 'next', keep: Keep });
    Assert.deepEqual(Next.resources, mfSum(Next.tide[1].resources, Counts));
    Assert.equal(JSON.stringify(Run), Before);
    Assert.throws(() => mfAct(Run, { kind: 'next', keep: Array(9).fill('wood') }), Error);
});

mfTest('gather spends a same-element pair but draws three random elements only on next', () => {
    const Run = mfPrepared();
    const Available = Elements.filter(Element => Run.resources[Element] > 0);
    Assert.ok(Available.length >= 2);
    for (const Kind of ['basic', 'gather']) {
        const Before = JSON.stringify(Run);
        Assert.throws(() => mfAct(Run, { kind: Kind, spend: [Available[0], Available[1]] }), /同系/);
        Assert.equal(JSON.stringify(Run), Before);
    }
    const Source = Elements.find(Element => Run.resources[Element] >= 2);
    const Gathered = mfAct(Run, { kind: 'gather', spend: [Source, Source] });
    Assert.equal(Gathered.pending, 3);
    Assert.equal(Gathered.GatherRandomState, Run.GatherRandomState);
    Assert.equal(Gathered.NaturalRandomState, Run.NaturalRandomState);
    const View = mfGetView(Gathered);
    Assert.equal(View.compass.pendingRandomCount, 3);
    Assert.equal(Object.hasOwn(View.compass, 'pending'), false);
    Assert.equal(View.preview.pendingDrawCount, 3);
    Assert.equal(View.preview.nextDrawCount, 9);
    const Before = JSON.stringify(Gathered);
    const Quote = mfQuote(Gathered, { kind: 'next', keep: [] });
    Assert.equal(Quote.randomDraw.count, 9);
    Assert.equal(Object.hasOwn(Quote.after, 'resources'), false);
    Assert.equal(JSON.stringify(Gathered), Before);
    const BareNext = mfAct(Run, { kind: 'next', keep: [] });
    const Next = mfAct(Gathered, { kind: 'next', keep: [] });
    Assert.equal(Next.NaturalRandomState, BareNext.NaturalRandomState);
    Assert.notEqual(Next.GatherRandomState, Gathered.GatherRandomState);
    const Return = Object.fromEntries(Elements.map(Element => [Element, Next.resources[Element] - BareNext.resources[Element]]));
    Assert.ok(Object.values(Return).every(Count => Count >= 0));
    Assert.equal(mfTotal(Return), 3);
    Assert.equal(Next.tide[1].gatherCount, 3);
    Assert.equal(Next.tide[1].total, 9);
    Assert.equal(Next.pending, 0);
    Assert.throws(() => mfAct(Gathered, { kind: 'gather', spend: [Source, Source] }), /聚气/);
    const OtherSource = Elements.find(Element => Element !== Source && Run.resources[Element] >= 2);
    Assert.ok(OtherSource);
    const Other = mfAct(mfAct(Run, { kind: 'gather', spend: [OtherSource, OtherSource] }), { kind: 'next', keep: [] });
    Assert.deepEqual(Other.resources, Next.resources, 'the paid element must not determine returned elements');
});

mfTest('next quotes hide random attributes and expose only known random counts', () => {
    let Run = mfCreateRun('elements', { core: true }, 42);
    const InitialBefore = JSON.stringify(Run);
    for (let Repeat = 0; Repeat < 5; ++Repeat) {
        const Quote = mfQuote(Run, { kind: 'next', keep: [] });
        Assert.equal(Quote.enabled, true);
        Assert.equal(Object.hasOwn(Quote.after, 'resources'), false);
        Assert.deepEqual(Quote.randomDraw, { count: 6, elementProbabilities: { gold: 0.2, wood: 0.2, water: 0.2, fire: 0.2, earth: 0.2 } });
        Assert.ok(Quote.changes.every(Text => !Object.values(Names).some(Name => Text.includes(Name))));
        Assert.equal(mfGetView(Run).tide.length, 1);
    }
    Assert.equal(JSON.stringify(Run), InitialBefore);
    for (let Round = 1; Round < 3; ++Round) Run = mfAct(Run, { kind: 'next', keep: [] });
    const Before = JSON.stringify(Run);
    Assert.equal(mfGetView(Run).preview.nextDrawCount, 8);
    Assert.equal(mfQuote(Run, { kind: 'next', keep: [] }).randomDraw.count, 8);
    const Next = mfAct(Run, { kind: 'next', keep: [] });
    Assert.equal(Next.tide[3].total, 8);
    Assert.equal(Next.tide[3].coreCount, 4);
    Assert.deepEqual(Next.resources, Next.tide[3].resources);
    Assert.equal(JSON.stringify(Run), Before);
    Assert.deepEqual(mfAct(Run, { kind: 'next', keep: [] }), Next, 'replaying the same immutable revision cannot produce a second draw');
    Assert.throws(() => mfAct(Run, { kind: 'next', keep: ['unknown'] }), Error);
    Assert.equal(JSON.stringify(Run), Before);
    for (let Round = 3; Round < 8; ++Round) Run = mfAct(Run, { kind: 'next', keep: [] });
    Assert.equal(mfGetView(Run).preview.nextDrawCount, 0);
    const LastQuote = mfQuote(Run, { kind: 'next' });
    Assert.equal(Object.hasOwn(LastQuote.after, 'resources'), true);
    Assert.equal(Object.hasOwn(LastQuote, 'randomDraw'), false);
    Assert.equal(mfGetView(mfAct(Run, { kind: 'withdraw' })).preview.nextDrawCount, 0);
});

mfTest('all 41 compass choices retain accurate costs, hints and pure quote results', () => {
    for (const Tide of ['tidal', 'tidalSmooth']) for (const Talent of ['none', 'wood', 'water']) {
        const Run = mfCreateRun('elements', { tide: Tide, pills: 2, talent: Talent }, 7);
        const Before = JSON.stringify(Run);
        const View = mfGetView(Run);
        Assert.equal(View.compass.choices.length, 41);
        for (const Choice of View.compass.choices) {
            Assert.deepEqual(Choice.quote, mfQuote(Run, Choice.action));
            Assert.equal(mfTotal(Choice.payment), Choice.action.spend.length);
            Assert.ok(Choice.hint.length > 0);
            if (Choice.quote.enabled) {
                const After = mfGetView(mfAct(Run, Choice.action));
                for (const Key of Object.keys(Choice.quote.after)) Assert.deepEqual(Choice.quote.after[Key], After[Key]);
            } else {
                Assert.ok(Choice.quote.reason.length > 0);
                Assert.throws(() => mfAct(Run, Choice.action), Error);
            }
        }
        View.compass.choices[0].action.spend.push('earth');
        View.compass.pendingRandomCount = 999;
        Assert.equal(JSON.stringify(Run), Before);
    }
});

mfTest('recipe hints match actual 3/6/8/12 gains and remain readable when disabled', () => {
    const Run = mfPrepared();
    const View = mfGetView(Run);
    for (const [Kind, Gain] of [['basic', 3], ['same', 6], ['generate', 8], ['five', 12]]) {
        const Choice = View.compass.choices.find(Entry => Entry.kind === Kind && Entry.quote.enabled);
        Assert.ok(Choice, `prepared fixture offers ${Kind}`);
        Assert.ok(Choice.hint.endsWith(`凝练＋${Gain}`));
        Assert.equal(mfAct(Run, Choice.action).score - Run.score, Gain);
    }
    const Five = mfGetView(mfCreateRun('elements', {}, 42)).compass.choices.find(Choice => Choice.kind === 'five');
    Assert.equal(Five.hint, '金1、木1、水1、火1、土1 → 凝练＋12');
    Assert.equal(Five.quote.enabled, false);
    const Gather = View.compass.choices.find(Choice => Choice.kind === 'gather');
    Assert.equal(Gather.hint, `${Names[Gather.source]}2 → 下轮随机3`);
});

mfTest('random gather may return its input element but never guarantees that element', () => {
    let SameWasSeen = false;
    let DifferentWasSeen = false;
    const ReturnedElements = new Set();
    for (let Seed = 0; Seed < 64; ++Seed) {
        const Run = mfPrepared(Seed);
        const Source = Elements.find(Element => Run.resources[Element] >= 2);
        const Bare = mfAct(Run, { kind: 'next', keep: [] });
        const Gathered = mfAct(mfAct(Run, { kind: 'gather', spend: [Source, Source] }), { kind: 'next', keep: [] });
        for (const Element of Elements) if (Gathered.resources[Element] > Bare.resources[Element]) {
            ReturnedElements.add(Element);
            if (Element === Source) SameWasSeen = true;
            else DifferentWasSeen = true;
        }
    }
    Assert.equal(SameWasSeen, true);
    Assert.equal(DifferentWasSeen, true);
    Assert.deepEqual(ReturnedElements, new Set(Elements));
});

mfTest('round-four natural, core and gather counts combine without previewing or double-paying returns', () => {
    let Run = mfCreateRun('elements', { core: true }, 42);
    for (let Round = 1; Round < 3; ++Round) Run = mfAct(Run, { kind: 'next', keep: [] });
    const Source = Elements.find(Element => Run.resources[Element] >= 2);
    Run = mfAct(Run, { kind: 'gather', spend: [Source, Source] });
    const Before = JSON.stringify(Run);
    const Quote = mfQuote(Run, { kind: 'next', keep: [] });
    Assert.equal(Quote.randomDraw.count, 11);
    Assert.equal(mfGetView(Run).preview.nextDrawCount, 11);
    Assert.equal(Object.hasOwn(Quote.after, 'resources'), false);
    Assert.equal(JSON.stringify(Run), Before);
    const Next = mfAct(Run, { kind: 'next', keep: [] });
    Assert.equal(mfTotal(Next.resources), 11);
    Assert.equal(Next.tide[3].naturalCount, 4);
    Assert.equal(Next.tide[3].coreCount, 4);
    Assert.equal(Next.tide[3].gatherCount, 3);
    Assert.equal(Next.tide[3].total, 11);
    Assert.deepEqual(mfAct(Run, { kind: 'next', keep: [] }), Next);
    Run = mfRevealAll(Next);
    const FinalBefore = JSON.stringify(Run);
    const LastSource = Elements.find(Element => Run.resources[Element] >= 2);
    Assert.throws(() => mfAct(Run, { kind: 'gather', spend: [LastSource, LastSource] }), /最后/);
    Assert.equal(JSON.stringify(Run), FinalBefore);
});

mfTest('A reward begins strictly above 100 and matches preview, quote and sealed values', () => {
    for (const [Score, Base, Battle] of [[60, 0, 0], [99, 0, 0], [100, 0, 0], [101, 1, 2.4], [107, 7, 16.8], [110, 10, 24], [120, 20, 48]]) {
        const Run = { ...mfPrepared(), score: Score };
        const Expected = { baseHpGain: Base, battleHpEquivalent: Battle };
        const View = mfGetView(Run);
        Assert.deepEqual(View.reward, { baseHpGain: 0, battleHpEquivalent: 0 });
        Assert.deepEqual(View.preview.sealReward, Expected);
        Assert.equal(Object.hasOwn(View, 'hpBonus'), false);
        Assert.ok(View.preview.grades.every(Grade => !Object.hasOwn(Grade, 'hpBonus')));
        Assert.deepEqual(mfQuote(Run, { kind: 'seal' }).after.reward, Expected);
        const Success = mfAct(Run, { kind: 'seal' });
        Assert.deepEqual(mfGetView(Success).reward, Expected);
        Assert.ok(!Success.summary.includes('%'));
    }
    const High = { ...mfPrepared(), score: 119 };
    const Five = mfGetView(High).compass.choices.find(Choice => Choice.kind === 'five');
    const Capped = mfAct(High, Five.action);
    Assert.equal(Capped.score, 120);
    Assert.deepEqual(mfAct(Capped, { kind: 'seal' }).reward, { baseHpGain: 20, battleHpEquivalent: 48 });
    const Withdrawn = mfAct({ ...mfPrepared(), score: 117 }, { kind: 'withdraw' });
    Assert.deepEqual(Withdrawn.reward, { baseHpGain: 0, battleHpEquivalent: 0 });
    Assert.deepEqual(mfGetView(Withdrawn).preview.sealReward, Withdrawn.reward);
    const Failed = mfAct({ ...mfPrepared(), score: 59, round: 8 }, { kind: 'next' });
    Assert.equal(Failed.status, 'failed');
    Assert.deepEqual(Failed.reward, { baseHpGain: 0, battleHpEquivalent: 0 });
    Assert.deepEqual(mfGetView(Failed).preview.sealReward, Failed.reward);
    const B = mfGetView(mfCreateRun('meridians'));
    Assert.deepEqual(B.reward, { baseHpGain: 0, battleHpEquivalent: 0 });
    Assert.deepEqual(B.preview.sealReward, { baseHpGain: 0, battleHpEquivalent: 0 });
});

mfTest('root converts same-source pairs and falls back to one when only one remains', () => {
    for (const DesiredCount of [1, 2]) {
        let Fixture;
        for (let Seed = 0; Seed < 32 && !Fixture; ++Seed) {
            const Run = mfCreateRun('elements', {}, Seed);
            const Source = Elements.find(Element => DesiredCount === 1 ? Run.resources[Element] === 1 : Run.resources[Element] >= 2);
            if (Source) Fixture = { Seed, Source, Target: Elements.find(Element => Element !== Source) };
        }
        Assert.ok(Fixture);
        const Run = mfCreateRun('elements', { talent: Fixture.Target }, Fixture.Seed);
        const Choice = mfGetView(Run).compass.choices.find(Entry => Entry.kind === 'convert' && Entry.source === Fixture.Source && Entry.target === Fixture.Target);
        Assert.equal(Choice.action.spend.length, DesiredCount);
        Assert.equal(Choice.hint, `${Names[Fixture.Source]}${DesiredCount} → ${Names[Fixture.Target]}${DesiredCount}`);
        Assert.equal(mfAct(Run, Choice.action).resources[Fixture.Target], Run.resources[Fixture.Target] + DesiredCount);
    }
    Assert.equal(mfCreateRun('elements', { talent: 'daoBody' }).carryLimit, 10);
});

mfTest('malformed payments, settings, seeds and removed elixirs reject atomically', () => {
    const Run = mfPrepared();
    const Before = JSON.stringify(Run);
    for (const Action of [
        { kind: 'basic', spend: ['wood', 'unknown'] }, { kind: 'same', spend: ['wood', 'wood', 'fire'] },
        { kind: 'generate', spend: ['wood', 'wood', 'gold', 'gold'] },
        { kind: 'five', spend: ['wood', 'wood', 'fire', 'fire', 'earth'] },
        { kind: 'convert', spend: ['wood'], target: 'wood' },
        { kind: 'basic', spend: ['wood', 'wood'], extra: true }, { kind: 'unknown' },
        { kind: 'elixir' }, { kind: 'elixir', target: 'wood' },
    ]) {
        Assert.throws(() => mfAct(Run, Action), Error);
        Assert.equal(mfQuote(Run, Action).enabled, false);
        Assert.equal(JSON.stringify(Run), Before);
    }
    for (const Settings of [{ pills: -1 }, { pills: 7 }, { pills: NaN }, { core: 1 }, { talent: 'unknown' }, { tide: 'unknown' }, { extra: 1 }, { elixir: false }, { pillElement: 'wood' }, { seed: 1 }]) Assert.throws(() => mfCreateRun('elements', Settings), Error);
    for (const Seed of [-1, 4294967296, 1.5, NaN, Infinity, '1', null]) Assert.throws(() => mfCreateRun('elements', {}, Seed), /seed|种子/i);
    Assert.throws(() => mfAct(mfCreateRun('meridians'), { kind: 'elixir' }), /取消/);
});

mfTest('zero-preparation A completes a legal run using only backend canonical choices', () => {
    const Run = mfGreedy(mfCreateRun('elements', {}, 42));
    Assert.equal(Run.status, 'success');
    Assert.ok(Run.score >= 60);
    Assert.equal(Run.round, 8);
    Assert.ok(Run.summary.length > 0);
});

mfTest('premature seal rejects and terminal runs stop all subsequent actions', () => {
    const Initial = mfCreateRun('elements');
    const Before = JSON.stringify(Initial);
    Assert.throws(() => mfAct(Initial, { kind: 'seal' }), /60/);
    Assert.equal(mfQuote(Initial, { kind: 'seal' }).enabled, false);
    Assert.equal(JSON.stringify(Initial), Before);
    for (const Run of [mfGreedy(mfCreateRun('elements', {}, 42)), mfAct(Initial, { kind: 'withdraw' })]) {
        const Snapshot = JSON.stringify(Run);
        Assert.throws(() => mfAct(Run, { kind: 'next' }), /结束/);
        for (const Choice of mfGetView(Run).compass.choices) Assert.equal(Choice.quote.enabled, false);
        Assert.equal(JSON.stringify(Run), Snapshot);
    }
});

mfTest('B has three sequential segments, nine base steps and automatic completion', () => {
    let Run = mfCreateRun('meridians', {}, 42);
    let View = mfGetView(Run);
    Assert.equal(View.score, 100);
    Assert.equal(View.maxRounds, 3);
    Assert.deepEqual(View.journey.segmentNames, ['任脉', '督脉', '冲脉']);
    Assert.equal(View.journey.totalSteps, 9);
    Assert.equal(View.journey.remainingSteps, 9);
    Assert.equal(View.journey.minFutureSteps, 4);
    for (const [Index, Name] of ['任脉', '督脉', '冲脉'].entries()) {
        View = mfGetView(Run);
        Assert.equal(View.journey.current.name, Name);
        Assert.equal(View.journey.completed.length, Index);
        Run = mfAct(Run, View.journey.current.routes.find(Route => Route.kind === 'safe').action);
    }
    View = mfGetView(Run);
    Assert.equal(View.status, 'success');
    Assert.equal(View.score, 100);
    Assert.equal(View.grade, '平实');
    Assert.equal(View.journey.current, null);
    Assert.equal(View.journey.spentSteps, 6);
    Assert.equal(View.journey.remainingSteps, 3);
    Assert.deepEqual(View.reward, { baseHpGain: 0, battleHpEquivalent: 0 });
    for (const Key of ['hpBonus', 'pressure', 'breath', 'pressureLimit']) Assert.equal(Object.hasOwn(View, Key), false);
});

mfTest('B current random route gains are stable and quotes never draw or reveal the next segment', () => {
    const Run = mfCreateRun('meridians', {}, 42);
    const Before = JSON.stringify(Run);
    Assert.deepEqual(Run, mfCreateRun('meridians', {}, 42));
    const Current = mfGetView(Run).journey.current;
    Assert.ok(Current.nurtureBonus >= 4 && Current.nurtureBonus <= 8);
    Assert.ok(Current.refineBonus >= 8 && Current.refineBonus <= 12);
    for (let Repeat = 0; Repeat < 4; ++Repeat) {
        const View = mfGetView(Run);
        Assert.deepEqual(View.journey.current, Current);
        Assert.equal(Object.hasOwn(View, 'MeridianRandomState'), false);
        Assert.deepEqual(View.tide, []);
        for (const Route of View.journey.current.routes) {
            Assert.deepEqual(Route.quote, mfQuote(Run, Route.action));
            Assert.equal(Object.hasOwn(Route.quote.after, 'journey'), false);
            Assert.equal(Object.hasOwn(Route.quote.after, 'current'), false);
            Assert.equal(Object.hasOwn(Route.quote.after, 'MeridianRandomState'), false);
        }
    }
    Assert.equal(JSON.stringify(Run), Before);
    const Next = mfAct(Run, { kind: 'safe' });
    Assert.equal(Next.round, 2);
    Assert.notEqual(Next.MeridianRandomState, Run.MeridianRandomState);
    Assert.deepEqual(mfAct(Run, { kind: 'safe' }), Next);
    Assert.equal(JSON.stringify(Run), Before);
    Assert.equal(mfGetView(Next).journey.current.name, '督脉');
});

mfTest('B reserves two steps per future segment and rejects any route causing a dead end', () => {
    let Run = mfAct(mfCreateRun('meridians', {}, 42), { kind: 'refine' });
    const View = mfGetView(Run);
    Assert.equal(View.journey.remainingSteps, 5);
    Assert.equal(View.journey.minFutureSteps, 2);
    const Refine = View.journey.current.routes.find(Route => Route.kind === 'refine');
    Assert.equal(Refine.quote.enabled, false);
    Assert.ok(Refine.quote.reason.length > 0);
    const Before = JSON.stringify(Run);
    Assert.throws(() => mfAct(Run, Refine.action), /步/);
    Assert.equal(JSON.stringify(Run), Before);
    Run = mfAct(Run, { kind: 'nurture' });
    Assert.equal(mfGetView(Run).journey.remainingSteps, 2);
    Run = mfAct(Run, { kind: 'safe' });
    Assert.equal(Run.status, 'success');
    Assert.equal(mfGetView(Run).journey.remainingSteps, 0);
});

mfTest('B preparations cap at twelve effective steps and explain unused preparation', () => {
    const Plain = mfGetView(mfCreateRun('meridians', { pills: 1, core: true, talent: 'daoBody' }));
    Assert.equal(Plain.journey.totalSteps, 12);
    const Prepared = mfGetView(mfCreateRun('meridians', { pills: 6, core: true, talent: 'daoBody' }));
    Assert.equal(Prepared.journey.totalSteps, 12);
    Assert.equal(Prepared.journey.preparation.requestedSteps, 17);
    Assert.equal(Prepared.journey.preparation.effectiveSteps, 12);
    Assert.equal(Prepared.journey.preparation.redundantSteps, 5);
    Assert.ok(Prepared.journey.preparation.hint.includes('5'));
    let Run = mfCreateRun('meridians', { pills: 3 }, 42);
    for (let Segment = 0; Segment < 3; ++Segment) Run = mfAct(Run, { kind: 'refine' });
    Assert.equal(Run.status, 'success');
    Assert.equal(Run.score, 120);
    Assert.deepEqual(Run.reward, { baseHpGain: 20, battleHpEquivalent: 48 });
});

mfTest('B steady saves one step only for the first nurture and quotes announce the discount', () => {
    let Run = mfCreateRun('meridians', { talent: 'steady' }, 42);
    const First = mfGetView(Run).journey.current.routes.find(Route => Route.kind === 'nurture');
    Assert.equal(First.costSteps, 2);
    Assert.ok(First.hint.includes('首次'));
    Assert.equal(mfQuote(Run, First.action).after.spentSteps, 2);
    Run = mfAct(Run, First.action);
    Assert.equal(mfGetView(Run).journey.remainingSteps, 7);
    Assert.equal(mfGetView(Run).journey.current.routes.find(Route => Route.kind === 'nurture').costSteps, 3);
    Run = mfAct(mfCreateRun('meridians', { talent: 'steady' }, 42), { kind: 'safe' });
    Assert.equal(mfGetView(Run).journey.current.routes.find(Route => Route.kind === 'nurture').costSteps, 2);
});

mfTest('B ten-step two-refine route can yield 116 instead of guaranteeing 120', () => {
    let Found;
    for (let Seed = 0; Seed < 256 && !Found; ++Seed) {
        let Run = mfCreateRun('meridians', { pills: 1 }, Seed);
        if (mfGetView(Run).journey.current.refineBonus !== 8) continue;
        Run = mfAct(Run, { kind: 'refine' });
        if (mfGetView(Run).journey.current.refineBonus !== 8) continue;
        Run = mfAct(Run, { kind: 'refine' });
        Found = mfAct(Run, { kind: 'safe' });
    }
    Assert.ok(Found);
    Assert.equal(Found.score, 116);
    Assert.equal(Found.status, 'success');
    Assert.deepEqual(Found.reward, { baseHpGain: 16, battleHpEquivalent: 38.4 });
});

mfTest('B rejects all old operations, cancels rewards on withdrawal and never rerolls on rejection', () => {
    const Run = mfCreateRun('meridians', {}, 42);
    const Before = JSON.stringify(Run);
    for (const Kind of ['condense', 'inhale', 'steady', 'disperse', 'guard', 'seal', 'next']) {
        Assert.throws(() => mfAct(Run, { kind: Kind }), Error);
        Assert.equal(mfQuote(Run, { kind: Kind }).enabled, false);
        Assert.equal(JSON.stringify(Run), Before);
    }
    Assert.throws(() => mfCreateRun('meridians', { tide: 'stable' }), Error);
    Assert.throws(() => mfAct(Run, { kind: 'nurture', costSteps: 0 }), Error);
    const Progressed = mfAct(Run, { kind: 'nurture' });
    Assert.ok(Progressed.score >= 104);
    Assert.deepEqual(Progressed.reward, { baseHpGain: 0, battleHpEquivalent: 0 });
    const Withdrawn = mfAct(Progressed, { kind: 'withdraw' });
    Assert.deepEqual(Withdrawn.reward, { baseHpGain: 0, battleHpEquivalent: 0 });
    Assert.equal(mfGetView(Withdrawn).journey.current, null);
    Assert.throws(() => mfAct(Withdrawn, { kind: 'safe' }), /结束/);
});
