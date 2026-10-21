import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';

test('isolated loop HTTP keeps previews pure, mutations atomic, and retries idempotent', async () => {
    const Child = spawn(process.execPath, [fileURLToPath(new URL('./server.mjs', import.meta.url))],
        { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let Errors = '';
    let Output = '';
    Child.stderr.on('data', Chunk => { Errors += Chunk; });
    const Base = await new Promise((Resolve, Reject) => {
        const Timer = setTimeout(() => Reject(new Error(`Loop server startup timed out: ${Errors}`)), 10000);
        Child.stdout.on('data', Chunk => {
            Output += Chunk;
            const Match = Output.match(/http:\/\/127\.0\.0\.1:\d+/);
            if (Match) { clearTimeout(Timer); Resolve(Match[0]); }
        });
        Child.once('error', Error => { clearTimeout(Timer); Reject(Error); });
        Child.once('exit', Code => { clearTimeout(Timer); Reject(new Error(`Loop server exited ${Code}: ${Errors}`)); });
    });
    let Session;
    async function mfCall(Path, Body, Method) {
        const Response = await fetch(`${Base}/api/${Path}`, { method: Method || (Body === undefined ? 'GET' : 'POST'),
            headers: { 'Content-Type': 'application/json', ...(Session ? { 'x-loop-session': Session } : {}) },
            ...(Body === undefined ? {} : { body: JSON.stringify(Body) }) });
        return { status: Response.status, body: await Response.json() };
    }
    try {
        assert.equal((await mfCall('health')).status, 200);
        assert.equal((await mfCall('state')).status, 401);
        assert.equal((await mfCall('session', undefined, 'GET')).status, 405);
        const Created = await mfCall('session', {});
        assert.equal(Created.status, 200);
        Session = Created.body.sessionId;
        assert.equal(Created.body.view.board.length, 25);
        assert.equal(Created.body.view.timeLeft, 72);
        const Original = (await mfCall('state')).body;
        const RunAction = { kind: 'run', changes: [] };
        const Quote = await mfCall('quote', { revision: 0, action: RunAction });
        assert.equal(Quote.body.quote.enabled, true);
        assert.equal(Quote.body.quote.cost.time, 16);
        assert.equal(Quote.body.quote.path.length, 17);
        assert.deepEqual((await mfCall('state')).body, Original);
        const FirstBody = { revision: 0, requestId: 'run-first-circle', action: RunAction };
        const Duplicates = await Promise.all(Array.from({ length: 5 }, () => mfCall('action', FirstBody)));
        assert.ok(Duplicates.every(Result => Result.status === 200));
        const First = (await mfCall('state')).body;
        assert.equal(First.view.revision, 1);
        assert.equal(First.view.lapCount, 1);
        assert.equal(First.view.timeLeft, 56);
        assert.equal(First.view.pieces.find(Piece => Piece.id === 'gather-1').charge, 1);
        for (const Key of ['power', 'disorder', 'liquid', 'score', 'timeLeft']) assert.equal(First.view[Key], Quote.body.quote.after[Key]);
        assert.equal((await mfCall('action', { ...FirstBody, requestId: 'stale-circle' })).status, 409);
        assert.equal((await mfCall('action', { ...FirstBody, action: { kind: 'withdraw' } })).status, 409, 'one receipt cannot identify a different action');
        const InvalidChanges = [
            [{ index: 20, shape: 'empty', rotation: 0, pieceId: null }],
            [{ index: -1, shape: 'straight', rotation: 1, pieceId: null }],
            [{ index: 21, shape: 'straight', rotation: 1, pieceId: 'unknown-piece' }],
            [{ index: 21, shape: 'empty', rotation: 0, pieceId: null }],
            [{ index: 10, shape: 'straight', rotation: 0, pieceId: null },
                { index: 15, shape: 'straight', rotation: 0, pieceId: null },
                { index: 23, shape: 'straight', rotation: 1, pieceId: 'gather-2' }],
        ];
        for (let Index = 0; Index < InvalidChanges.length; ++Index) {
            const Rejected = await mfCall('action', { revision: 1, requestId: `invalid-layout-${Index}`,
                action: { kind: 'run', changes: InvalidChanges[Index] } });
            assert.equal(Rejected.status, 400);
            assert.deepEqual((await mfCall('state')).body, First, 'invalid or disconnected layouts cannot partially clear charge or consume time');
        }
        const RemoveGather = { kind: 'run', changes: [{ index: 21, shape: 'straight', rotation: 1, pieceId: null }] };
        const RemoveQuote = await mfCall('quote', { revision: 1, action: RemoveGather });
        assert.equal(RemoveQuote.body.quote.enabled, true);
        assert.equal(RemoveQuote.body.quote.cost.time, 17);
        assert.deepEqual((await mfCall('state')).body, First, 'previewed dismantling cannot discard active stored qi');
        const RemoveBody = { revision: 1, requestId: 'dismantle-and-run', action: RemoveGather };
        const Removed = await mfCall('action', RemoveBody);
        assert.equal(Removed.status, 200);
        assert.equal(Removed.body.view.pieces.find(Piece => Piece.id === 'gather-1').charge, 0);
        assert.equal(Removed.body.view.timeLeft, 39);
        const RemoveRetry = await mfCall('action', RemoveBody);
        assert.deepEqual(RemoveRetry.body.view, Removed.body.view);
        const InvalidStart = await mfCall('start', { revision: 2, requestId: 'invalid-preparation', settings: { pills: -1 } });
        assert.equal(InvalidStart.status, 400);
        assert.deepEqual((await mfCall('state')).body, Removed.body);
        const ResetBody = { revision: 2, requestId: 'restart-prepared', settings: { pills: 6, core: true, talent: 'daoBody' } };
        const Reset = await mfCall('start', ResetBody);
        assert.equal(Reset.status, 200);
        assert.equal(Reset.body.view.revision, 3);
        assert.equal(Reset.body.view.timeLeft, 80);
        assert.equal(Reset.body.view.power, 12);
        assert.deepEqual((await mfCall('start', ResetBody)).body.view, Reset.body.view);
        assert.equal((await mfCall('action', { revision: 3, requestId: 'premature-seal', action: { kind: 'seal' } })).status, 400);
        const PreparedRun = await mfCall('action', { revision: 3, requestId: 'prepared-circle', action: RunAction });
        assert.equal(PreparedRun.status, 200);
        const SealQuote = await mfCall('quote', { revision: 4, action: { kind: 'seal' } });
        assert.equal(SealQuote.body.quote.enabled, true);
        const SealBody = { revision: 4, requestId: 'seal-loop', action: { kind: 'seal' } };
        const Sealed = await mfCall('action', SealBody);
        assert.equal(Sealed.body.view.status, 'success');
        assert.deepEqual(Sealed.body.view.reward, SealQuote.body.quote.after.reward);
        assert.equal(Sealed.body.view.reward.baseHpGain, Math.max(0, Sealed.body.view.score - 100));
        assert.equal(Sealed.body.view.reward.battleHpEquivalent, Sealed.body.view.reward.baseHpGain * 24 / 10);
        assert.deepEqual((await mfCall('action', SealBody)).body.view, Sealed.body.view);
        assert.equal((await mfCall('action', { revision: 5, requestId: 'run-after-seal', action: RunAction })).status, 400);
        assert.equal((await fetch(`${Base}/engine.mjs`)).status, 404);
        assert.equal(Errors, '');
    } finally {
        if (Child.exitCode === null) { const Closed = once(Child, 'exit'); Child.kill(); await Closed; }
    }
});
