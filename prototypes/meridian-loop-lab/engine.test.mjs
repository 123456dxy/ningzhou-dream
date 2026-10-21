import test from 'node:test';
import assert from 'node:assert/strict';
import { mfCreateRun, mfGetView, mfQuote, mfAct } from './engine.mjs';

const OuterPath = [20, 21, 22, 23, 24, 19, 14, 9, 4, 3, 2, 1, 0, 5, 10, 15, 20];
const RunAction = { kind: 'run', changes: [] };
const InnerPath = [20, 21, 22, 23, 18, 19, 14, 9, 8, 3, 2, 1, 6, 5, 10, 15, 20];
const InnerChanges = [
    { index: 23, shape: 'corner', rotation: 3, pieceId: null },
    { index: 18, shape: 'corner', rotation: 1, pieceId: null },
    { index: 19, shape: 'corner', rotation: 3, pieceId: null },
    { index: 9, shape: 'corner', rotation: 2, pieceId: null },
    { index: 8, shape: 'corner', rotation: 0, pieceId: null },
    { index: 3, shape: 'corner', rotation: 2, pieceId: null },
    { index: 1, shape: 'corner', rotation: 1, pieceId: null },
    { index: 6, shape: 'corner', rotation: 3, pieceId: null },
    { index: 5, shape: 'corner', rotation: 1, pieceId: null },
];

function mfPiece(View, Id) { return View.pieces.find(Piece => Piece.id === Id); }

test('outer loop has exact authoritative cost, gate order and first-lap settlement', () => {
    const Run = mfCreateRun();
    const Before = structuredClone(Run);
    const Quote = mfQuote(Run, RunAction);
    assert.equal(Quote.enabled, true);
    assert.deepEqual(Quote.path, OuterPath);
    assert.deepEqual(Quote.cost, { time: 16, edits: 0, travel: 16 });
    assert.equal(Quote.outcome.qualified, true);
    assert.equal(Quote.outcome.liquidGain, 1);
    assert.equal(Quote.outcome.stable, true);
    assert.equal(Quote.after.timeLeft, 56);
    assert.equal(Quote.after.power, 5);
    assert.equal(Quote.after.disorder, 1);
    assert.equal(Quote.after.score, 103);
    assert.deepEqual(Quote.after.reward, { baseHpGain: 0, battleHpEquivalent: 0 });
    assert.deepEqual(Quote.after.sealReward, { baseHpGain: 3, battleHpEquivalent: 7.2 });
    const Next = mfAct(Run, RunAction);
    assert.deepEqual(mfGetView(Next), Quote.after);
    assert.deepEqual(Run, Before);
    assert.equal(Next.revision, 1);
    assert.equal(mfGetView(Next).phase, 'paused');
    assert.equal(mfPiece(mfGetView(Next), 'gather-1').charge, 1);
});

test('views and repeated previews cannot mutate the board, inventory or accumulated charge', () => {
    const Run = mfAct(mfCreateRun(), RunAction);
    const Before = structuredClone(Run);
    for (let Attempt = 0; Attempt < 4; ++Attempt) {
        const View = mfGetView(Run);
        View.board[21].pieceId = null;
        View.pieces[0].charge = 99;
        View.fixed[0].ports[0] = 99;
        const Quote = mfQuote(Run, RunAction);
        Quote.after.board[21].shape = 'empty';
        Quote.trace[0].power = 99;
    }
    assert.deepEqual(Run, Before);
    assert.equal(mfQuote(Run, RunAction).after.timeLeft, 40);
    assert.equal(mfPiece(mfGetView(Run), 'gather-1').charge, 1);
});

test('a different inner detour is valid and all first-lap draft edits are free', () => {
    const Run = mfCreateRun();
    const Quote = mfQuote(Run, { kind: 'run', changes: InnerChanges });
    assert.equal(Quote.enabled, true);
    assert.deepEqual(Quote.path, InnerPath);
    assert.deepEqual(Quote.cost, { time: 16, edits: 0, travel: 16 });
    assert.equal(Quote.outcome.qualified, true);
    assert.equal(Quote.after.score, 103);
    const Next = mfAct(Run, { kind: 'run', changes: InnerChanges });
    assert.deepEqual(mfGetView(Next), Quote.after);
    assert.equal(mfAct(Next, { kind: 'seal' }).status, 'success');
});

test('strict input and invalid geometry reject atomically, including duplicate inventory IDs', () => {
    const Run = mfAct(mfCreateRun(), RunAction);
    const Before = structuredClone(Run);
    const InvalidActions = [
        { kind: 'unknown' }, { kind: 'run' }, { kind: 'seal', extra: true },
        { kind: 'run', changes: [], extra: true },
        { kind: 'run', changes: [{ index: 20, shape: 'corner', rotation: 0, pieceId: null }] },
        { kind: 'run', changes: [{ index: 21, shape: 'empty', rotation: 0, pieceId: null }] },
        { kind: 'run', changes: [{ index: 21, shape: 'straight', rotation: NaN, pieceId: null }] },
        { kind: 'run', changes: [{ index: 21, shape: 'straight', rotation: 4, pieceId: null }] },
        { kind: 'run', changes: [{ index: -1, shape: 'straight', rotation: 1, pieceId: null }] },
        { kind: 'run', changes: [{ index: 21, shape: 'straight', rotation: 1, pieceId: 'bad' }] },
        { kind: 'run', changes: [{ index: 23, shape: 'straight', rotation: 1, pieceId: 'gather-1' }] },
        { kind: 'run', changes: [{ index: 21, shape: 'empty', rotation: 0, pieceId: 'gather-1' }] },
        { kind: 'run', changes: [{ index: 21, shape: 'straight', rotation: 1, pieceId: null, charge: 9 }] },
        { kind: 'run', changes: [
            { index: 21, shape: 'straight', rotation: 1, pieceId: null },
            { index: 21, shape: 'straight', rotation: 1, pieceId: 'gather-1' },
        ] },
    ];
    for (const Action of InvalidActions) {
        const Quote = mfQuote(Run, Action);
        assert.equal(Quote.enabled, false, JSON.stringify(Action));
        assert.ok(Quote.reason.length > 0);
        assert.equal(Quote.after, null);
        assert.throws(() => mfAct(Run, Action), Error);
        assert.deepEqual(Run, Before);
    }
    for (const Settings of [null, [], { pills: -1 }, { pills: 7 }, { pills: 0.5 }, { pills: NaN },
        { core: 1 }, { talent: 'wood' }, { unknown: 1 }]) {
        assert.throws(() => mfCreateRun(Settings), Error);
    }
});

test('after first lap only two net edits are allowed, and no-op patches cost no edit time', () => {
    const Run = mfAct(mfCreateRun(), RunAction);
    const NoOp = { index: 21, shape: 'straight', rotation: 1, pieceId: 'gather-1' };
    const Changes = [NoOp,
        { index: 11, shape: 'straight', rotation: 0, pieceId: null },
        { index: 12, shape: 'corner', rotation: 0, pieceId: null }];
    const Quote = mfQuote(Run, { kind: 'run', changes: Changes });
    assert.equal(Quote.enabled, true);
    assert.deepEqual(Quote.cost, { time: 18, edits: 2, travel: 16 });
    assert.equal(mfGetView(mfAct(Run, { kind: 'run', changes: Changes })).timeLeft, 38);
    const ThreeEdits = [...Changes.slice(1), { index: 13, shape: 'straight', rotation: 0, pieceId: null }];
    assert.equal(mfQuote(Run, { kind: 'run', changes: ThreeEdits }).enabled, false);
    assert.throws(() => mfAct(Run, { kind: 'run', changes: ThreeEdits }), /2/);
});

test('charge follows an unmoved ID through rotation, while moving or dismantling resets it', () => {
    let Run = mfAct(mfAct(mfCreateRun(), RunAction), RunAction);
    assert.equal(mfPiece(mfGetView(Run), 'gather-1').charge, 2);
    const Rotated = mfAct(Run, { kind: 'run', changes: [
        { index: 21, shape: 'straight', rotation: 3, pieceId: 'gather-1' },
    ] });
    assert.equal(mfPiece(mfGetView(Rotated), 'gather-1').charge, 0);
    assert.ok(mfGetView(Rotated).lastLap.trace.some(Event => /放出4/.test(Event.event)));
    const Moved = mfAct(Run, { kind: 'run', changes: [
        { index: 21, shape: 'straight', rotation: 1, pieceId: null },
        { index: 23, shape: 'straight', rotation: 1, pieceId: 'gather-1' },
    ] });
    assert.equal(mfPiece(mfGetView(Moved), 'gather-1').charge, 1);
    assert.equal(mfGetView(Moved).lastLap.cost.edits, 2);
    const Removed = mfAct(Run, { kind: 'run', changes: [
        { index: 21, shape: 'straight', rotation: 1, pieceId: null },
    ] });
    assert.equal(mfPiece(mfGetView(Removed), 'gather-1').charge, 0);
    assert.equal(mfPiece(mfGetView(Run), 'gather-1').charge, 2);
});

test('gate mismatch is a committed lap that discards its liquid instead of a technical rejection', () => {
    const Run = mfCreateRun();
    const Action = { kind: 'run', changes: [
        { index: 23, shape: 'straight', rotation: 1, pieceId: 'condense-2' },
        { index: 19, shape: 'straight', rotation: 0, pieceId: 'condense-1' },
        { index: 15, shape: 'straight', rotation: 0, pieceId: null },
    ] };
    const Quote = mfQuote(Run, Action);
    assert.equal(Quote.enabled, true);
    assert.equal(Quote.outcome.qualified, false);
    assert.equal(Quote.outcome.liquidProduced, 2);
    assert.equal(Quote.outcome.liquidGain, 0);
    assert.equal(Quote.after.timeLeft, 56);
    assert.equal(Quote.after.power, 5);
    assert.equal(Quote.after.disorder, 2);
    assert.equal(Quote.after.score, 0);
    const Next = mfAct(Run, Action);
    assert.deepEqual(mfGetView(Next), Quote.after);
    assert.equal(Next.revision, 1);
    assert.equal(mfGetView(Next).lapCount, 1);
    assert.equal(mfGetView(Next).qualifiedLaps, 0);
    assert.equal(mfQuote(Next, { kind: 'seal' }).enabled, false);
    assert.equal(mfPiece(mfGetView(Next), 'gather-1').charge, 1);
});

test('72 time allows exactly four unedited outer laps, then rejects without spending anything', () => {
    let Run = mfCreateRun();
    for (let Lap = 0; Lap < 4; ++Lap) Run = mfAct(Run, RunAction);
    const Before = structuredClone(Run);
    const View = mfGetView(Run);
    assert.equal(View.timeLeft, 8);
    assert.equal(View.qualifiedLaps, 4);
    assert.equal(View.liquid, 3);
    assert.equal(View.stableLaps, 4);
    assert.equal(View.score, 110);
    assert.equal(mfQuote(Run, RunAction).enabled, false);
    assert.throws(() => mfAct(Run, RunAction), /息|时间/);
    assert.deepEqual(Run, Before);
    const Sealed = mfAct(Run, { kind: 'seal' });
    assert.deepEqual(mfGetView(Sealed).reward, { baseHpGain: 10, battleHpEquivalent: 24 });
    assert.equal(mfGetView(Sealed).timeLeft, 8);
});

test('preparation and talents affect authoritative power, time, soothing and overflow', () => {
    const Prepared = mfCreateRun({ pills: 6, core: true, talent: 'steady' });
    assert.equal(mfGetView(Prepared).timeTotal, 80);
    assert.equal(mfGetView(Prepared).power, 12);
    const First = mfGetView(mfAct(Prepared, RunAction));
    assert.equal(First.power, 9);
    assert.equal(First.disorder, 0);
    assert.equal(First.lastLap.outcome.vented, true);
    assert.equal(First.liquid, 0);
    assert.equal(First.score, 101);
    assert.equal(mfGetView(mfCreateRun({ talent: 'daoBody' })).rules.disorderLimit, 11);
    const NormalSecond = mfGetView(mfAct(mfAct(mfCreateRun(), RunAction), RunAction));
    const SteadySecond = mfGetView(mfAct(mfAct(mfCreateRun({ talent: 'steady' }), RunAction), RunAction));
    assert.equal(NormalSecond.disorder, 1);
    assert.equal(SteadySecond.disorder, 0);
});

test('seal requires a qualified lap; withdrawal and ended states cannot grant or change rewards', () => {
    const Initial = mfCreateRun();
    assert.throws(() => mfAct(Initial, { kind: 'seal' }), /贯通/);
    const Run = mfAct(Initial, RunAction);
    const SealQuote = mfQuote(Run, { kind: 'seal' });
    const Sealed = mfAct(Run, { kind: 'seal' });
    assert.deepEqual(mfGetView(Sealed), SealQuote.after);
    assert.equal(Sealed.status, 'success');
    assert.equal(mfGetView(Sealed).phase, 'ended');
    assert.deepEqual(mfGetView(Sealed).reward, { baseHpGain: 3, battleHpEquivalent: 7.2 });
    const Withdrawn = mfAct(Run, { kind: 'withdraw' });
    assert.equal(Withdrawn.status, 'withdrawn');
    assert.deepEqual(mfGetView(Withdrawn).reward, { baseHpGain: 0, battleHpEquivalent: 0 });
    assert.deepEqual(mfGetView(Withdrawn).sealReward, { baseHpGain: 0, battleHpEquivalent: 0 });
    for (const Ended of [Sealed, Withdrawn]) {
        const Before = structuredClone(Ended);
        for (const Action of [RunAction, { kind: 'seal' }, { kind: 'withdraw' }]) {
            assert.equal(mfQuote(Ended, Action).enabled, false);
            assert.throws(() => mfAct(Ended, Action), Error);
        }
        assert.deepEqual(Ended, Before);
    }
});

test('inventory permits four functional pieces but rejects a fifth before any lap is committed', () => {
    const Run = mfCreateRun();
    const Fourth = { index: 23, shape: 'straight', rotation: 1, pieceId: 'soothe-2' };
    const Fifth = { index: 19, shape: 'straight', rotation: 0, pieceId: 'gather-2' };
    const Allowed = mfQuote(Run, { kind: 'run', changes: [Fourth] });
    assert.equal(Allowed.enabled, true);
    assert.equal(Allowed.after.rules.maxFunctionalPieces, 4);
    assert.equal(Allowed.after.installedPieces, 4);
    const Before = structuredClone(Run);
    const Action = { kind: 'run', changes: [Fourth, Fifth] };
    assert.equal(mfQuote(Run, Action).enabled, false);
    assert.throws(() => mfAct(Run, Action), /4/);
    assert.deepEqual(Run, Before);
});

test('broken connections expose their partial path and both affected cells without committing a draft', () => {
    const Run = mfCreateRun();
    const Before = structuredClone(Run);
    const Action = { kind: 'run', changes: [{ index: 21, shape: 'straight', rotation: 0, pieceId: 'gather-1' }] };
    const Quote = mfQuote(Run, Action);
    assert.equal(Quote.enabled, false);
    assert.deepEqual(Quote.path, [20, 21]);
    assert.equal(Quote.blockedCell, 21);
    assert.deepEqual(Quote.issues.map(Issue => Issue.index), [20, 21]);
    assert.ok(Quote.issues.every(Issue => Issue.message.length > 0));
    assert.throws(() => mfAct(Run, Action), /端口/);
    assert.deepEqual(Run, Before);
});

test('equivalent straight half-turns are canonical no-ops, preserve charge and do not consume edit budget', () => {
    const Run = mfAct(mfCreateRun(), RunAction);
    const Before = structuredClone(Run);
    const Action = { kind: 'run', changes: [
        { index: 21, shape: 'straight', rotation: 3, pieceId: 'gather-1' },
        { index: 10, shape: 'straight', rotation: 2, pieceId: 'soothe-1' },
        { index: 15, shape: 'straight', rotation: 2, pieceId: 'condense-1' },
    ] };
    const Quote = mfQuote(Run, Action);
    assert.equal(Quote.enabled, true);
    assert.deepEqual(Quote.cost, { time: 16, edits: 0, travel: 16 });
    assert.equal(Quote.after.board[21].rotation, 1);
    assert.equal(Quote.after.board[10].rotation, 0);
    assert.equal(Quote.after.board[15].rotation, 0);
    assert.equal(mfPiece(Quote.after, 'gather-1').charge, 2);
    assert.deepEqual(Quote.after, mfGetView(mfAct(Run, RunAction)));
    assert.deepEqual(mfGetView(mfAct(Run, Action)), Quote.after);
    assert.deepEqual(Run, Before);

    const Corner = mfQuote(Run, { kind: 'run', changes: [
        { index: 12, shape: 'corner', rotation: 2, pieceId: null },
    ] });
    assert.equal(Corner.enabled, true);
    assert.equal(Corner.cost.edits, 1);
    assert.equal(Corner.after.board[12].rotation, 2);
    assert.deepEqual(Corner.after.board[12].ports, [2, 3]);
    for (const Rotation of [-1, 4, NaN, 1.5]) {
        const Invalid = { kind: 'run', changes: [
            { index: 21, shape: 'straight', rotation: Rotation, pieceId: 'gather-1' },
        ] };
        assert.equal(mfQuote(Run, Invalid).enabled, false);
        assert.throws(() => mfAct(Run, Invalid), /旋转/);
        assert.deepEqual(Run, Before);
    }
});
