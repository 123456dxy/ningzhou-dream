import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';

test('manual-flow HTTP preserves previews, rejects illegal moves, and settles each input once', async () => {
    const Child = spawn(process.execPath, [fileURLToPath(new URL('./server.mjs', import.meta.url))],
        { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let Errors = '', Output = '';
    Child.stderr.on('data', Chunk => { Errors += Chunk; });
    const Base = await new Promise((Resolve, Reject) => {
        const Timer = setTimeout(() => Reject(new Error(`Flow startup timed out: ${Errors}`)), 10000);
        Child.stdout.on('data', Chunk => { Output += Chunk; const Match = Output.match(/http:\/\/127\.0\.0\.1:\d+/);
            if (Match) { clearTimeout(Timer); Resolve(Match[0]); } });
        Child.once('error', Error => { clearTimeout(Timer); Reject(Error); });
        Child.once('exit', Code => { clearTimeout(Timer); Reject(new Error(`Flow exited ${Code}: ${Errors}`)); });
    });
    let Session;
    async function mfCall(Path, Body, Method) {
        const Response = await fetch(`${Base}/api/${Path}`, { method: Method || (Body === undefined ? 'GET' : 'POST'),
            headers: { 'Content-Type': 'application/json', ...(Session ? { 'x-flow-session': Session } : {}) },
            ...(Body === undefined ? {} : { body: JSON.stringify(Body) }) });
        return { status: Response.status, body: await Response.json() };
    }
    async function mfCommit(Action, Id) {
        const Before = (await mfCall('state')).body.view;
        const Body = { revision: Before.revision, requestId: Id, action: Action };
        const Result = await mfCall('action', Body);
        assert.equal(Result.status, 200);
        return { Body, View: Result.body.view };
    }
    try {
        assert.equal((await mfCall('health')).status, 200);
        assert.equal((await mfCall('state')).status, 401);
        const Created = await mfCall('session', {});
        assert.equal(Created.status, 200); Session = Created.body.sessionId;
        const Initial = Created.body.view;
        assert.equal(Initial.stage, 1); assert.equal(Initial.positionId, 'start');
        assert.equal(Initial.power, 6); assert.equal(Initial.operationsLeft, 14);
        assert.equal(Initial.nodes.length, 7);
        const ChargeQuote = await mfCall('quote', { revision: 0, action: { kind: 'charge' } });
        assert.equal(ChargeQuote.body.quote.enabled, true);
        assert.equal(ChargeQuote.body.quote.after.power, 8);
        assert.deepEqual((await mfCall('state')).body.view, Initial);
        const ChargeBody = { revision: 0, requestId: 'charge-once', action: { kind: 'charge' } };
        const Repeated = await Promise.all(Array.from({ length: 5 }, () => mfCall('action', ChargeBody)));
        assert.ok(Repeated.every(Result => Result.status === 200));
        const Charged = (await mfCall('state')).body.view;
        assert.equal(Charged.revision, 1); assert.equal(Charged.power, 8); assert.equal(Charged.operationsLeft, 13);
        assert.equal((await mfCall('action', { revision: 1, requestId: 'skip-map', action: { kind: 'move', targetId: 'end' } })).status, 400);
        assert.equal((await mfCall('action', { revision: 1, requestId: 'bad-id', action: { kind: 'move', targetId: '__proto__' } })).status, 400);
        assert.deepEqual((await mfCall('state')).body.view, Charged);
        assert.equal((await mfCall('action', { revision: 0, requestId: 'stale-charge', action: { kind: 'release' } })).status, 409);
        const FirstMove = await mfCommit({ kind: 'move', targetId: 'up1' }, 'upper-first');
        assert.equal(FirstMove.View.power, 4); assert.equal(FirstMove.View.extraScore, 3);
        assert.deepEqual((await mfCall('action', FirstMove.Body)).body.view, FirstMove.View);
        await mfCommit({ kind: 'move', targetId: 'up2' }, 'upper-second');
        await mfCommit({ kind: 'move', targetId: 'detour' }, 'detour-first');
        const Exit = await mfCommit({ kind: 'move', targetId: 'end' }, 'exit-first');
        assert.equal(Exit.View.atExit, true); assert.equal(Exit.View.extraScore, 9);
        assert.equal(Exit.View.operationsLeft, 9);
        const AdvanceQuote = await mfCall('quote', { revision: Exit.View.revision, action: { kind: 'advance' } });
        assert.equal(AdvanceQuote.body.quote.enabled, true);
        assert.equal(Object.hasOwn(AdvanceQuote.body.quote.after, 'nodes'), false);
        assert.doesNotMatch(JSON.stringify(AdvanceQuote.body.quote), /RandomState|templateId/);
        assert.deepEqual((await mfCall('state')).body.view, Exit.View);
        const Advanced = await mfCommit({ kind: 'advance' }, 'open-second');
        assert.equal(Advanced.View.stage, 2); assert.equal(Advanced.View.positionId, 'start');
        assert.equal(Advanced.View.power, Exit.View.power);
        assert.equal(Advanced.View.operationsLeft, Exit.View.operationsLeft);
        assert.deepEqual((await mfCall('action', Advanced.Body)).body.view, Advanced.View, 'retrying advance cannot reveal a different map');
        let Current = Advanced.View;
        let LastBody;
        for (let Step = 0; Current.status === 'active' && Step < 15; ++Step) {
            const Choice = Current.choices.find(Item => Item.action.kind === (Current.atExit ? 'advance' : 'move') && Item.quote.enabled);
            assert.ok(Choice, 'a legal forward path must remain available');
            const Next = await mfCommit(Choice.action, `finish-${Step}`);
            Current = Next.View; LastBody = Next.Body;
        }
        assert.equal(Current.status, 'success'); assert.equal(Current.stage, 3);
        assert.equal(Current.reward.baseHpGain, Current.score - 100);
        assert.equal(Current.reward.battleHpEquivalent, Current.reward.baseHpGain * 24 / 10);
        assert.deepEqual((await mfCall('action', LastBody)).body.view, Current);
        assert.equal((await mfCall('action', { revision: Current.revision, requestId: 'after-end', action: { kind: 'charge' } })).status, 400);
        const ResetBody = { revision: Current.revision, requestId: 'new-flow', settings: { pills: 1, core: true, talent: 'none' } };
        const Restarted = await mfCall('start', ResetBody);
        assert.equal(Restarted.status, 200);
        assert.equal(Restarted.body.view.revision, Current.revision + 1);
        assert.equal(Restarted.body.view.operationsLeft, 16); assert.equal(Restarted.body.view.power, 7);
        assert.deepEqual((await mfCall('action', ChargeBody)).body.view, Restarted.body.view, 'old retries cannot roll back a restart');
        const InvalidSettings = await mfCall('start', { revision: Restarted.body.view.revision, requestId: 'bad-prepare', settings: { seed: 123 } });
        assert.equal(InvalidSettings.status, 400);
        assert.deepEqual((await mfCall('state')).body.view, Restarted.body.view);
        assert.equal((await fetch(`${Base}/engine.mjs`)).status, 404);
        assert.equal(Errors, '');
    } finally { if (Child.exitCode === null) { const Closed = once(Child, 'exit'); Child.kill(); await Closed; } }
});
