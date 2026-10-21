import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';

test('isolated HTTP session preserves previews, rejects stale actions, and deduplicates retries', async () => {
    const Child = spawn(process.execPath, [fileURLToPath(new URL('./server.mjs', import.meta.url))],
        { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let Output = '';
    let Errors = '';
    Child.stderr.on('data', Chunk => { Errors += Chunk; });
    const Base = await new Promise((Resolve, Reject) => {
        const Timer = setTimeout(() => Reject(new Error(`Server did not start: ${Errors}`)), 10000);
        Child.stdout.on('data', Chunk => {
            Output += Chunk;
            const Match = Output.match(/http:\/\/127\.0\.0\.1:\d+/);
            if (Match) { clearTimeout(Timer); Resolve(Match[0]); }
        });
        Child.once('error', Reject);
    });
    let Session;
    async function mfCall(Path, Body, Method) {
        const Response = await fetch(`${Base}/api/${Path}`, { method: Method || (Body === undefined ? 'GET' : 'POST'),
            headers: { 'Content-Type': 'application/json', ...(Session ? { 'x-lab-session': Session } : {}) },
            ...(Body === undefined ? {} : { body: JSON.stringify(Body) }) });
        return { status: Response.status, body: await Response.json() };
    }
    try {
        assert.equal((await mfCall('health')).status, 200);
        assert.equal((await mfCall('session', undefined, 'GET')).status, 405);
        const Created = await mfCall('session', {});
        Session = Created.body.sessionId;
        assert.equal(Created.body.views.elements.score, 0);
        assert.equal(Created.body.views.elements.settings.tide, 'tidal', 'new sessions open the compass tide design');
        assert.deepEqual(Created.body.views.elements.preview.elementProbabilities, { gold: 0.2, wood: 0.2, water: 0.2, fire: 0.2, earth: 0.2 });
        assert.equal(Created.body.views.elements.tide.length, 1, 'only the actually drawn first round is published');
        assert.equal(Created.body.views.elements.preview.nextDrawCount, 6);
        assert.deepEqual(Created.body.views.elements.reward, { baseHpGain: 0, battleHpEquivalent: 0 });
        assert.deepEqual(Created.body.views.elements.preview.sealReward, { baseHpGain: 0, battleHpEquivalent: 0 });
        assert.equal(Created.body.views.meridians.score, 100, 'the current estimate is conditional on completing all three meridians');
        assert.equal(Created.body.views.meridians.journey.remainingSteps, 9);
        assert.equal(Created.body.views.meridians.journey.completed.length, 0);
        assert.equal(Created.body.views.meridians.journey.current.routes.length, 3);
        const InitialChoice = Created.body.views.elements.compass.choices.find(Entry => Entry.kind === 'basic' && Entry.quote.enabled);
        assert.ok(InitialChoice, 'six equally drawn points always contain a same-element pair');
        const Action = InitialChoice.action;
        const Quote = await mfCall('quote', { mode: 'elements', revision: 0, action: Action });
        assert.equal(Quote.body.quote.after.score, 3);
        assert.equal((await mfCall('state')).body.views.elements.score, 0, 'preview cannot commit');
        const Body = { mode: 'elements', revision: 0, action: Action, requestId: 'first-action' };
        const Duplicates = await Promise.all(Array.from({ length: 5 }, () => mfCall('action', Body)));
        assert.ok(Duplicates.every(Result => Result.status === 200));
        const After = (await mfCall('state')).body;
        assert.equal(After.views.elements.score, 3, 'five retries settle once');
        assert.equal(After.views.elements.revision, 1);
        assert.equal(After.views.meridians.score, 100, 'modes remain independent');
        const Stale = await mfCall('action', { ...Body, requestId: 'stale-action' });
        assert.equal(Stale.status, 409);
        assert.equal(Stale.body.views.elements.score, 3);
        const Invalid = await mfCall('action', { mode: 'elements', revision: 1,
            action: { kind: 'same', spend: ['unknown', 'unknown', 'unknown'] }, requestId: 'invalid-payment' });
        assert.equal(Invalid.status, 400);
        assert.deepEqual((await mfCall('state')).body, After, 'rejected payment preserves every field');
        const AvailableConvert = After.views.elements.compass.choices.find(Entry => Entry.kind === 'convert' && Entry.quote.enabled);
        const Auto = await mfCall('quote', { mode: 'elements', revision: 1,
            action: { kind: 'convert', target: AvailableConvert.target, spend: [] }, autoPick: true });
        assert.equal(Auto.body.quote.enabled, true);
        assert.equal(Auto.body.action.spend.length, 1);
        const WrongSettings = await mfCall('start', { mode: 'elements', revision: 1, settings: { pills: -1 }, requestId: 'negative-pills' });
        assert.equal(WrongSettings.status, 400);
        const Reset = await mfCall('start', { mode: 'elements', revision: 1, settings: { pills: 2 }, requestId: 'reset' });
        assert.equal(Reset.body.views.elements.revision, 2, 'restart revision cannot return to zero');
        assert.equal(Reset.body.views.elements.score, 0);
        const Replay = await mfCall('action', Body);
        assert.equal(Replay.body.views.elements.revision, 2, 'old retry returns current snapshot');
        assert.equal(Replay.body.views.elements.score, 0);
        assert.equal((await fetch(`${Base}/engine.mjs`)).status, 404, 'only public page assets are served');
        assert.equal((await mfCall('action', { mode: 'elements', revision: 2, action: { kind: 'seal' }, requestId: 'too-early' })).status, 400);
        const CompassStartBody = { mode: 'elements', revision: 2,
            settings: { tide: 'tidal', pills: 2 }, requestId: 'compass-start' };
        const CompassStart = await mfCall('start', CompassStartBody);
        assert.equal(CompassStart.status, 200);
        const CompassView = CompassStart.body.views.elements;
        assert.ok(Array.isArray(CompassView.pillDraws), 'the committed start exposes its actual random pill draws');
        assert.equal(CompassView.pillDraws.length, 4, 'two pills draw four elements at the start boundary');
        assert.equal(Object.values(CompassView.pillResources).reduce((Sum, Count) => Sum + Count, 0), 4);
        assert.equal(Object.values(CompassView.resources).reduce((Sum, Count) => Sum + Count, 0), 10);
        assert.ok(CompassView.compass.choices.every(Entry => Entry.kind !== 'elixir'));
        const CompassReplay = await mfCall('start', CompassStartBody);
        assert.equal(CompassReplay.body.replayed, true);
        assert.deepEqual(CompassReplay.body.views, CompassStart.body.views, 'retrying an accepted start never redraws pills');
        const Choice = CompassView.compass.choices.find(Entry => Entry.kind === 'convert' && Entry.quote.enabled);
        assert.equal(Choice.quote.enabled, true);
        const BeforeCompass = (await mfCall('state')).body;
        const CompassQuote = await mfCall('quote', { mode: 'elements', revision: 3, action: Choice.action });
        assert.deepEqual(CompassQuote.body.quote, Choice.quote, 'displayed compass quote shares the HTTP execution rule');
        assert.deepEqual((await mfCall('state')).body, BeforeCompass, 'compass previews do not change tide or resources');
        const CompassCommit = await mfCall('action', { mode: 'elements', revision: 3, action: Choice.action, requestId: 'compass-convert' });
        assert.equal(CompassCommit.status, 200);
        for (const Key of Object.keys(Choice.quote.after)) assert.deepEqual(CompassCommit.body.views.elements[Key], Choice.quote.after[Key]);
        const MixedBefore = (await mfCall('state')).body;
        assert.equal((await mfCall('action', { mode: 'elements', revision: 4,
            action: { kind: 'basic', spend: ['wood', 'fire'] }, requestId: 'mixed-payment' })).status, 400);
        assert.deepEqual((await mfCall('state')).body, MixedBefore, 'new-tide mixed payment is rejected atomically');
        for (const Settings of [{ pills: 2, pillElement: 'wood' }, { elixir: false }, { tide: 'tidalMetal' }]) {
            assert.equal((await mfCall('start', { mode: 'elements', revision: 4, settings: Settings,
                requestId: `removed-setting-${Object.keys(Settings).join('-')}` })).status, 400);
            assert.deepEqual((await mfCall('state')).body, MixedBefore, 'removed preparation controls cannot alter an active run');
        }
        assert.equal((await mfCall('action', { mode: 'elements', revision: 4,
            action: { kind: 'elixir', target: 'gold' }, requestId: 'removed-elixir' })).status, 400);
        assert.deepEqual((await mfCall('state')).body, MixedBefore);
        const NextAction = { kind: 'next', keep: [] };
        const NextQuote = await mfCall('quote', { mode: 'elements', revision: 4, action: NextAction });
        assert.equal(NextQuote.body.quote.enabled, true);
        assert.equal(Object.hasOwn(NextQuote.body.quote.after, 'resources'), false, 'previews cannot reveal the next random elements');
        assert.equal(NextQuote.body.quote.randomDraw.count, 6);
        assert.deepEqual((await mfCall('state')).body, MixedBefore, 'next-round previews cannot advance or expose the active draw');
        const NextBody = { mode: 'elements', revision: 4, action: NextAction, requestId: 'draw-next-round' };
        const NextCommit = await mfCall('action', NextBody);
        assert.equal(NextCommit.status, 200);
        assert.equal(NextCommit.body.views.elements.round, 2);
        assert.equal(NextCommit.body.views.elements.tide.length, 2);
        assert.equal(Object.values(NextCommit.body.views.elements.resources).reduce((Sum, Count) => Sum + Count, 0), 6);
        const NextRetry = await mfCall('action', NextBody);
        assert.equal(NextRetry.body.replayed, true);
        assert.deepEqual(NextRetry.body.views, NextCommit.body.views, 'retrying an advance never draws another batch');
        const GatherView = NextCommit.body.views.elements;
        const GatherChoice = GatherView.compass.choices.find(Entry => Entry.kind === 'gather' && Entry.quote.enabled);
        assert.ok(GatherChoice, 'six random points contain a legal same-element investment');
        const GatherBody = { mode: 'elements', revision: GatherView.revision, action: GatherChoice.action, requestId: 'gather-random' };
        const GatherCommit = await mfCall('action', GatherBody);
        assert.equal(GatherCommit.status, 200);
        const PendingView = GatherCommit.body.views.elements;
        assert.equal(PendingView.compass.pendingRandomCount, 3);
        assert.equal(PendingView.preview.pendingDrawCount, 3);
        assert.equal(PendingView.preview.nextDrawCount, 11, 'eight natural and three gathered points share the next random total');
        assert.equal(Object.hasOwn(PendingView.compass, 'pending'), false, 'random gathering cannot publish an element-specific reserve');
        const GatherRetry = await mfCall('action', GatherBody);
        assert.deepEqual(GatherRetry.body.views, GatherCommit.body.views, 'retries cannot create a second investment');
        const DrawGatherBody = { mode: 'elements', revision: PendingView.revision, action: { kind: 'next', keep: [] }, requestId: 'receive-gather-random' };
        const GatherNextQuote = await mfCall('quote', { mode: 'elements', revision: PendingView.revision, action: DrawGatherBody.action });
        assert.equal(GatherNextQuote.body.quote.randomDraw.count, 11);
        assert.equal(Object.hasOwn(GatherNextQuote.body.quote.after, 'resources'), false);
        assert.deepEqual((await mfCall('state')).body.views.elements, PendingView, 'gather preview cannot commit its future draw');
        const GatherNext = await mfCall('action', DrawGatherBody);
        assert.equal(GatherNext.status, 200);
        assert.equal(GatherNext.body.views.elements.tide.at(-1).gatherCount, 3);
        assert.equal(Object.values(GatherNext.body.views.elements.resources).reduce((Sum, Count) => Sum + Count, 0), 11);
        const GatherNextRetry = await mfCall('action', DrawGatherBody);
        assert.deepEqual(GatherNextRetry.body.views, GatherNext.body.views, 'random gathered points settle once');
        const BeforeJourney = (await mfCall('state')).body;
        const JourneyStart = BeforeJourney.views.meridians;
        const JourneyQuote = await mfCall('quote', { mode: 'meridians', revision: 0, action: { kind: 'refine' } });
        assert.equal(JourneyQuote.body.quote.enabled, true);
        assert.equal(Object.hasOwn(JourneyQuote.body.quote.after, 'journey'), false, 'route previews cannot publish the next segment');
        assert.doesNotMatch(JSON.stringify(JourneyQuote.body.quote), /nurtureBonus|refineBonus|MeridianRandomState/);
        assert.deepEqual((await mfCall('state')).body, BeforeJourney, 'route preview cannot advance the active segment or RNG');
        const FirstJourneyBody = { mode: 'meridians', revision: 0, action: { kind: 'refine' }, requestId: 'journey-first' };
        const FirstJourneys = await Promise.all(Array.from({ length: 3 }, () => mfCall('action', FirstJourneyBody)));
        assert.ok(FirstJourneys.every(Result => Result.status === 200));
        const FirstJourney = FirstJourneys.at(-1).body.views.meridians;
        assert.equal(FirstJourney.revision, 1);
        assert.equal(FirstJourney.journey.completed.length, 1);
        assert.equal(FirstJourney.journey.remainingSteps, 5);
        assert.equal(FirstJourney.journey.current.name, JourneyStart.journey.segmentNames[1]);
        const BeforeBlockedJourney = (await mfCall('state')).body;
        const BlockedJourneyQuote = await mfCall('quote', { mode: 'meridians', revision: 1, action: { kind: 'refine' } });
        assert.equal(BlockedJourneyQuote.body.quote.enabled, false, 'four more steps cannot consume the final segment reservation');
        assert.equal((await mfCall('action', { mode: 'meridians', revision: 1, action: { kind: 'refine' }, requestId: 'journey-over-budget' })).status, 400);
        assert.equal((await mfCall('action', { mode: 'meridians', revision: 1, action: { kind: 'condense' }, requestId: 'journey-old-action' })).status, 400);
        assert.deepEqual((await mfCall('state')).body, BeforeBlockedJourney);
        const SecondJourney = await mfCall('action', { mode: 'meridians', revision: 1, action: { kind: 'nurture' }, requestId: 'journey-second' });
        assert.equal(SecondJourney.status, 200);
        assert.equal(SecondJourney.body.views.meridians.journey.remainingSteps, 2);
        const FinalJourneyBody = { mode: 'meridians', revision: 2, action: { kind: 'safe' }, requestId: 'journey-final' };
        const FinalJourney = await mfCall('action', FinalJourneyBody);
        const JourneyResult = FinalJourney.body.views.meridians;
        assert.equal(JourneyResult.status, 'success', 'the third meridian automatically closes the breakthrough');
        assert.equal(JourneyResult.journey.completed.length, 3);
        assert.equal(JourneyResult.journey.current, null);
        assert.equal(JourneyResult.journey.remainingSteps, 0);
        assert.equal(JourneyResult.reward.baseHpGain, Math.max(0, JourneyResult.score - 100));
        assert.equal(JourneyResult.reward.battleHpEquivalent, JourneyResult.reward.baseHpGain * 24 / 10);
        const FinalJourneyRetry = await mfCall('action', FinalJourneyBody);
        assert.deepEqual(FinalJourneyRetry.body.views, FinalJourney.body.views, 'retrying auto-completion cannot award twice');
        const JourneyRestart = await mfCall('start', { mode: 'meridians', revision: 3,
            settings: { pills: 1, core: true, talent: 'none' }, requestId: 'journey-restart' });
        assert.equal(JourneyRestart.status, 200);
        assert.equal(JourneyRestart.body.views.meridians.journey.totalSteps, 11);
        assert.equal(Object.hasOwn(JourneyRestart.body.views.meridians.settings, 'tide'), false);
        const InvalidOldJourney = await mfCall('start', { mode: 'meridians', revision: 4,
            settings: { tide: 'stable' }, requestId: 'journey-old-setting' });
        assert.equal(InvalidOldJourney.status, 400);
        assert.deepEqual((await mfCall('state')).body, JourneyRestart.body, 'retired pressure settings cannot reset an active journey');
        assert.equal(Errors, '');
    } finally {
        const Closed = once(Child, 'exit');
        Child.kill();
        await Closed;
    }
});
