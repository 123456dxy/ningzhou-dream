import test from 'node:test';
import assert from 'node:assert/strict';
import { mfCreateRun, mfGetView, mfQuote, mfAct } from './engine.mjs';

function mfMove(Run, TargetId) { return mfAct(Run, { kind: 'move', targetId: TargetId }); }
function mfDirect(Run) { return mfMove(mfMove(mfMove(Run, 'down1'), 'up2'), 'end'); }

test('tutorial direct route carries power between nodes and awards only matched windows', () => {
    const Initial = mfCreateRun();
    const First = mfMove(Initial, 'down1');
    assert.equal(mfGetView(First).power, 5);
    assert.equal(mfGetView(First).extraScore, 2);
    assert.deepEqual(mfGetView(First).lastMove, { from: 'start', to: 'down1', matched: true, gain: 2, powerBefore: 6, powerAfter: 5 });
    const Second = mfMove(First, 'up2');
    assert.equal(mfGetView(Second).power, 7);
    assert.equal(mfGetView(Second).extraScore, 5);
    const End = mfMove(Second, 'end');
    const View = mfGetView(End);
    assert.equal(View.power, 7);
    assert.equal(View.lastMove.matched, false);
    assert.equal(View.lastMove.gain, 0);
    assert.equal(View.score, 105);
    assert.equal(View.operationsLeft, 11);
    assert.equal(View.canAdvance, true);
    assert.deepEqual(View.visitedIds, ['start', 'down1', 'up2', 'end']);
    assert.deepEqual(View.reward, { baseHpGain: 0, battleHpEquivalent: 0 });
    assert.equal(mfGetView(Initial).positionId, 'start');
});

test('charge and release change the same carried power and can align all tutorial gates', () => {
    let Run = mfAct(mfCreateRun(), { kind: 'charge' });
    assert.equal(mfGetView(Run).power, 8);
    Run = mfMove(Run, 'up1');
    assert.equal(mfGetView(Run).power, 4);
    Run = mfMove(Run, 'up2');
    assert.equal(mfGetView(Run).power, 6);
    Run = mfAct(Run, { kind: 'release' });
    assert.equal(mfGetView(Run).power, 4);
    Run = mfMove(Run, 'end');
    assert.equal(mfGetView(Run).extraScore, 7);
    assert.equal(mfGetView(Run).operationsLeft, 9);
    assert.equal(mfGetView(Run).score, 107);
});

test('detours cost one more operation and cannot spend the minimum remaining completion budget', () => {
    const Detour = mfMove(mfMove(mfMove(mfMove(mfCreateRun(), 'down1'), 'up2'), 'detour'), 'end');
    assert.equal(mfGetView(Detour).score, 108);
    assert.equal(mfGetView(Detour).operationsLeft, 10);
    let Run = mfCreateRun();
    for (const Kind of ['charge', 'release', 'charge', 'release', 'charge']) Run = mfAct(Run, { kind: Kind });
    assert.equal(mfGetView(Run).operationsLeft, 9);
    const Before = structuredClone(Run);
    assert.equal(mfQuote(Run, { kind: 'release' }).enabled, false);
    assert.throws(() => mfAct(Run, { kind: 'release' }), /余程|次数/);
    assert.deepEqual(Run, Before);
    Run = mfMove(mfMove(Run, 'up1'), 'up2');
    assert.equal(mfGetView(Run).operationsLeft, 7);
    assert.equal(mfQuote(Run, { kind: 'move', targetId: 'detour' }).enabled, false);
    assert.equal(mfQuote(Run, { kind: 'move', targetId: 'end' }).enabled, true);
    Run = mfMove(Run, 'end');
    Run = mfAct(Run, { kind: 'advance' });
    Run = mfDirect(Run);
    Run = mfAct(Run, { kind: 'advance' });
    Run = mfDirect(Run);
    assert.equal(mfGetView(Run).status, 'success');
    assert.equal(mfGetView(Run).operationsLeft, 0);
});

test('three stages preserve power and automatically settle without an extra seal operation', () => {
    let Run = mfDirect(mfCreateRun());
    const Carried = mfGetView(Run).power;
    const Advance = mfQuote(Run, { kind: 'advance' });
    assert.equal(Advance.enabled, true);
    assert.equal(Advance.cost.operations, 0);
    assert.equal(Advance.outcome.unknownNextStage, true);
    assert.equal(Advance.after.stage, 2);
    assert.equal(Advance.after.positionId, 'start');
    assert.equal(Object.hasOwn(Advance.after, 'nodes'), false);
    assert.equal(Object.hasOwn(Advance.after, 'edges'), false);
    assert.equal(Object.hasOwn(Advance.after, 'choices'), false);
    Run = mfAct(Run, { kind: 'advance' });
    assert.equal(mfGetView(Run).power, Carried);
    assert.deepEqual(mfGetView(Run).visitedIds, ['start']);
    Run = mfDirect(Run);
    Run = mfAct(Run, { kind: 'advance' });
    Run = mfDirect(Run);
    const View = mfGetView(Run);
    assert.equal(View.stage, 3);
    assert.equal(View.status, 'success');
    assert.equal(View.operationsLeft, 5);
    assert.equal(View.canAdvance, false);
    assert.ok(View.score >= 100 && View.score <= 120);
    assert.equal(View.reward.baseHpGain, View.score - 100);
    assert.equal(View.reward.battleHpEquivalent, (View.score - 100) * 24 / 10);
    assert.throws(() => mfAct(Run, { kind: 'seal' }), Error);
});

test('preview and views do not consume RNG or expose later-stage graphs', () => {
    const Run = mfDirect(mfCreateRun({}, 12345));
    const Before = structuredClone(Run);
    const ExpectedNext = mfAct(Run, { kind: 'advance' });
    for (let Attempt = 0; Attempt < 6; ++Attempt) {
        const View = mfGetView(Run);
        assert.equal(Object.hasOwn(View, 'RandomState'), false);
        assert.equal(Object.hasOwn(View, 'TemplateIndex'), false);
        assert.equal(Object.hasOwn(View, 'seed'), false);
        assert.equal(View.nodes.length, 7);
        View.nodes[0].label = '篡改';
        View.history.length = 0;
        mfQuote(Run, { kind: 'advance' });
        mfQuote(Run, { kind: 'charge' });
        mfQuote(Run, { kind: 'move', targetId: 'up1' });
    }
    assert.deepEqual(Run, Before);
    assert.deepEqual(mfAct(Run, { kind: 'advance' }), ExpectedNext);
    const Replay = mfAct(mfDirect(mfCreateRun({}, 12345)), { kind: 'advance' });
    assert.deepEqual(mfGetView(Replay), mfGetView(ExpectedNext));
    const Templates = new Set();
    for (const Seed of [1, 2, 3, 4, 5, 77, 999, 4294967295]) {
        const Current = mfAct(mfDirect(mfCreateRun({}, Seed)), { kind: 'advance' });
        Templates.add(JSON.stringify(mfGetView(Current).nodes.map(Node => [Node.minPower, Node.maxPower, Node.bonus, Node.powerDelta])));
        assert.deepEqual(mfGetView(mfCreateRun({}, Seed)).nodes, mfGetView(mfCreateRun({}, 1)).nodes);
    }
    assert.ok(Templates.size >= 2);
});

test('ordinary exact previews share settlement with actions and never mutate their run', () => {
    const Run = mfCreateRun();
    const Before = structuredClone(Run);
    for (const Action of [{ kind: 'move', targetId: 'up1' }, { kind: 'charge' }, { kind: 'release' }, { kind: 'withdraw' }]) {
        const Quote = mfQuote(Run, Action);
        assert.equal(Quote.enabled, true);
        const Actual = mfGetView(mfAct(Run, Action));
        for (const [Key, Value] of Object.entries(Quote.after)) assert.deepEqual(Actual[Key], Value, Key);
        assert.deepEqual(Run, Before);
    }
    assert.equal(mfQuote(Run, { kind: 'move', targetId: 'up1' }).after.lastMove.matched, false);
    assert.equal(mfQuote(Run, { kind: 'move', targetId: 'up1' }).after.power, 2);
});

test('steady discounts only the first effective adjustment in each stage', () => {
    let Run = mfCreateRun({ pills: 4, talent: 'steady' });
    const Before = structuredClone(Run);
    assert.equal(mfQuote(Run, { kind: 'charge' }).enabled, false);
    assert.throws(() => mfAct(Run, { kind: 'charge' }), /气势/);
    assert.deepEqual(Run, Before);
    assert.equal(mfQuote(Run, { kind: 'release' }).cost.operations, 0);
    Run = mfAct(Run, { kind: 'release' });
    assert.equal(mfGetView(Run).power, 8);
    assert.equal(mfGetView(Run).operationsLeft, 14);
    assert.equal(mfQuote(Run, { kind: 'charge' }).cost.operations, 1);
    Run = mfDirect(Run);
    Run = mfAct(Run, { kind: 'advance' });
    const Effective = mfGetView(Run).power < 10 ? 'charge' : 'release';
    assert.equal(mfQuote(Run, { kind: Effective }).cost.operations, 0);
    const Operations = mfGetView(Run).operationsLeft;
    Run = mfAct(Run, { kind: Effective });
    assert.equal(mfGetView(Run).operationsLeft, Operations);
});

test('preparation caps power and adds operations, and partial boundary adjustments still apply', () => {
    const Prepared = mfGetView(mfCreateRun({ pills: 6, core: true, talent: 'daoBody' }));
    assert.equal(Prepared.power, 10);
    assert.equal(Prepared.operationsTotal, 17);
    let Run = mfCreateRun({ pills: 3 });
    assert.equal(mfGetView(mfAct(Run, { kind: 'charge' })).power, 10);
    Run = mfMove(mfCreateRun(), 'up1');
    Run = mfAct(Run, { kind: 'release' });
    assert.equal(mfGetView(Run).power, 0);
    assert.equal(mfQuote(Run, { kind: 'release' }).enabled, false);
    assert.equal(mfGetView(Run).operationsLeft, 12);
});

test('unknown inputs, backwards moves and repeats are atomic rejections', () => {
    const Run = mfMove(mfCreateRun(), 'down1');
    const Before = structuredClone(Run);
    const Invalid = [null, [], { kind: 'move' }, { kind: 'move', targetId: 'start' },
        { kind: 'move', targetId: 'down1' }, { kind: 'move', targetId: 'end' }, { kind: 'move', targetId: 'unknown' },
        { kind: 'move', targetId: 2 }, { kind: 'move', targetId: 'up2', bonus: 99 },
        { kind: 'charge', targetId: 'up2' }, { kind: 'advance' }, { kind: 'seal' }, { kind: 'unknown' }];
    for (const Action of Invalid) {
        assert.equal(mfQuote(Run, Action).enabled, false);
        assert.ok(mfQuote(Run, Action).reason.length > 0);
        assert.throws(() => mfAct(Run, Action), Error);
        assert.deepEqual(Run, Before);
    }
    for (const Settings of [null, [], { pills: -1 }, { pills: NaN }, { pills: 7 }, { pills: 0.5 }, { core: 1 },
        { talent: 'wood' }, { seed: 1 }, { unknown: true }]) assert.throws(() => mfCreateRun(Settings), Error);
    for (const Seed of [-1, NaN, 1.5, Infinity, 4294967296, '1']) assert.throws(() => mfCreateRun({}, Seed), Error);
});

test('withdrawal and terminal actions cannot cash out or spend further resources', () => {
    const Withdrawn = mfAct(mfDirect(mfCreateRun()), { kind: 'withdraw' });
    assert.deepEqual(mfGetView(Withdrawn).reward, { baseHpGain: 0, battleHpEquivalent: 0 });
    assert.deepEqual(mfGetView(Withdrawn).sealReward, { baseHpGain: 0, battleHpEquivalent: 0 });
    let Completed = mfCreateRun();
    for (let Stage = 1; Stage <= 3; ++Stage) {
        Completed = mfDirect(Completed);
        if (Stage < 3) Completed = mfAct(Completed, { kind: 'advance' });
    }
    for (const Run of [Withdrawn, Completed]) {
        const Before = structuredClone(Run);
        for (const Action of [{ kind: 'move', targetId: 'up1' }, { kind: 'charge' }, { kind: 'release' }, { kind: 'advance' }, { kind: 'withdraw' }]) {
            assert.equal(mfQuote(Run, Action).enabled, false);
            assert.throws(() => mfAct(Run, Action), Error);
        }
        assert.deepEqual(Run, Before);
    }
});
