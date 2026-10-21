import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';

test('network HTTP keeps random previews isolated and settles repeated requests once', async () => {
    const Child = spawn(process.execPath, [fileURLToPath(new URL('./server.mjs', import.meta.url)), '--port', '0'], { stdio: ['ignore', 'pipe', 'pipe'] });
    let ErrorOutput = '';
    Child.stderr.on('data', Chunk => { ErrorOutput += Chunk; });
    const Address = await new Promise((Resolve, Reject) => {
        let Output = '';
        const Timer = setTimeout(() => { Child.kill(); Reject(new Error(`startup timeout: ${ErrorOutput}`)); }, 8000);
        Child.stdout.on('data', Chunk => {
            Output += Chunk;
            const Match = Output.match(/http:\/\/127\.0\.0\.1:\d+/);
            if (Match) { clearTimeout(Timer); Resolve(Match[0]); }
        });
        Child.once('exit', Code => { clearTimeout(Timer); Reject(new Error(`server exited ${Code}: ${ErrorOutput}`)); });
        Child.once('error', Failure => { clearTimeout(Timer); Reject(Failure); });
    });
    let SessionId;
    const Request = async (Path, Body, ExtraHeaders = {}) => {
        const Response = await fetch(`${Address}${Path}`, {
            method: Body === undefined ? 'GET' : 'POST',
            headers: { 'content-type': 'application/json', ...(SessionId ? { 'x-network-session': SessionId } : {}), ...ExtraHeaders },
            ...(Body === undefined ? {} : { body: JSON.stringify(Body) }),
        });
        return { status: Response.status, data: await Response.json() };
    };
    try {
        assert.deepEqual((await Request('/api/health')).data, { ok: true, isolated: true, prototype: 'meridian-network' });
        assert.equal((await Request('/api/state')).status, 401);
        const Session = await Request('/api/session', {});
        assert.equal(Session.status, 200);
        SessionId = Session.data.sessionId;
        const Initial = Session.data.view;
        assert.equal(Initial.qi, 115);
        assert.equal(Initial.score, 0);
        assert.equal(Initial.positionId, 'start');
        assert.ok(Initial.nodes.length >= 30 && Initial.nodes.length <= 40);
        const Hidden = Initial.nodes.filter(Node => !Node.revealed);
        assert.ok(Hidden.length > 0);
        for (const Node of Hidden) {
            assert.equal(Node.label, '未明穴');
            for (const Key of ['cost', 'successPercent', 'points', 'effectText', 'maxAttempts']) assert.equal(Node[Key], undefined);
        }
        const TargetChoice = Initial.choices.find(Choice => Choice.action.kind === 'strike' && Choice.quote.enabled
            && Initial.nodes.find(Node => Node.id === Choice.action.targetId)?.kind !== 'gather');
        assert.ok(TargetChoice, 'an affordable non-gather opening must be available');
        const Action = TargetChoice.action;
        const Quotes = await Promise.all(Array.from({ length: 5 }, () => Request('/api/quote', { revision: Initial.revision, action: Action })));
        for (const Quote of Quotes) {
            assert.equal(Quote.status, 200);
            assert.equal(Quote.data.quote.successPercent, TargetChoice.quote.successPercent);
            assert.ok(!/randomState|randomSeed|"roll"/i.test(JSON.stringify(Quote.data)));
        }
        assert.deepEqual((await Request('/api/state')).data.view, Initial);
        const Foreign = await Request('/api/quote', { revision: Initial.revision, action: Action }, { origin: 'http://foreign.invalid' });
        assert.equal(Foreign.status, 403);
        assert.equal((await Request('/api/action', { revision: Initial.revision, requestId: 'invalid', action: { kind: 'strike', targetId: '__proto__' } })).status, 400);
        assert.deepEqual((await Request('/api/state')).data.view, Initial);

        const Body = { revision: Initial.revision, requestId: 'first-strike', action: Action };
        const Responses = await Promise.all(Array.from({ length: 5 }, () => Request('/api/action', Body)));
        const After = Responses[0].data.view;
        for (const Response of Responses) { assert.equal(Response.status, 200); assert.deepEqual(Response.data.view, After); }
        assert.equal(After.revision, Initial.revision + 1);
        assert.equal(After.qi, Initial.qi - TargetChoice.quote.cost.qi);
        const Target = After.nodes.find(Node => Node.id === Action.targetId);
        if (Target.opened) {
            assert.equal(After.positionId, Target.id);
            assert.equal(After.rawScore, Initial.rawScore + TargetChoice.quote.points);
        } else {
            assert.equal(After.positionId, Initial.positionId);
            assert.equal(Target.attemptsLeft, Target.maxAttempts - 1);
            assert.equal(After.rawScore, 0);
            const Restore = await Request('/api/action', { revision: After.revision, requestId: 'restore', action: { kind: 'restore' } });
            assert.equal(Restore.status, 200);
            assert.equal(Restore.data.view.coreAvailable, false);
            assert.equal(Restore.data.view.qi, After.qi);
            assert.equal(Restore.data.view.nodes.find(Node => Node.id === Target.id).attemptsLeft, Target.maxAttempts);
        }
        const Current = (await Request('/api/state')).data.view;
        assert.equal((await Request('/api/action', { revision: Initial.revision, requestId: 'stale', action: Action })).status, 409);
        assert.deepEqual((await Request('/api/state')).data.view, Current);
        assert.equal((await Request('/api/action', { ...Body, action: { kind: 'withdraw' } })).status, 409);
        const RestartBody = { revision: Current.revision, requestId: 'restart', settings: { pills: 2, core: true, talent: 'daoBody' } };
        const Restart = await Request('/api/start', RestartBody);
        assert.equal(Restart.status, 200);
        assert.equal(Restart.data.view.revision, Current.revision + 1);
        assert.equal(Restart.data.view.qi, 130);
        assert.equal(Restart.data.view.positionId, 'start');
        assert.deepEqual((await Request('/api/action', Body)).data.view, Restart.data.view);
        assert.equal((await Request('/api/start', { revision: Restart.data.view.revision, requestId: 'seed', settings: { pills: 0, seed: 7 } })).status, 400);
        assert.deepEqual((await Request('/api/state')).data.view, Restart.data.view);
        const WithdrawBody = { revision: Restart.data.view.revision, requestId: 'withdraw', action: { kind: 'withdraw' } };
        const End = await Request('/api/action', WithdrawBody);
        assert.equal(End.status, 200);
        assert.equal(End.data.view.status, 'withdrawn');
        assert.equal(End.data.view.reward, null);
        assert.deepEqual((await Request('/api/action', WithdrawBody)).data.view, End.data.view);
        assert.equal((await Request('/api/action', { revision: End.data.view.revision, requestId: 'terminal', action: { kind: 'withdraw' } })).status, 400);
        for (const Path of ['/engine.mjs', '/board.mjs']) assert.equal((await fetch(`${Address}${Path}`)).status, 404);
        assert.equal(ErrorOutput, '');
    } finally {
        if (Child.exitCode === null) { const Exited = once(Child, 'exit'); Child.kill(); await Exited; }
    }
});
