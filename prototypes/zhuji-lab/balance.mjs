// Offline policy experiment. Uses the actual engine for allocation, randomness,
// legal actions, costs, scores, rewards and termination. Only human input and
// rendering are replaced. Policies read public current-segment offers, never
// future segments or private RNG. The same seed/config/policy reproduces a trace.
import { mfCreateRun, mfGetView, mfAct } from './engine.mjs';

const SeedCount = Number(process.argv[2] ?? 128);
if (!Number.isInteger(SeedCount) || SeedCount < 1 || SeedCount > 10000) throw new Error('Seed count must be 1..10000.');

const Experiments = [
    { policy: 'safe-v1', settings: {} },
    { policy: 'nurture-first-v1', settings: {} },
    { policy: 'current-efficiency-v1', settings: {} },
    { policy: 'refine-first-v1', settings: { pills: 1 } },
    { policy: 'current-efficiency-v1', settings: { pills: 1, core: true } },
];

function mfChooseRoute(View, Policy) {
    const Legal = View.journey.current.routes.filter(Route => Route.quote.enabled);
    if (Policy === 'safe-v1') return Legal.find(Route => Route.kind === 'safe');
    if (Policy === 'nurture-first-v1') return Legal.find(Route => Route.kind === 'nurture') || Legal.find(Route => Route.kind === 'safe');
    if (Policy === 'refine-first-v1') return Legal.find(Route => Route.kind === 'refine') || Legal.find(Route => Route.kind === 'nurture') || Legal[0];
    return [...Legal].sort((Left, Right) => {
        // Policy preference only: compare current bonus per step beyond safety.
        const SafeCost = Legal.find(Route => Route.kind === 'safe').costSteps;
        const Efficiency = Route => Route.extraScore / Math.max(1, Route.costSteps - SafeCost);
        return Efficiency(Right) - Efficiency(Left) || Right.extraScore - Left.extraScore;
    })[0];
}

for (const Experiment of Experiments) {
    for (let Seed = 0; Seed < SeedCount; ++Seed) {
        let Run = mfCreateRun('meridians', Experiment.settings, Seed);
        const Trace = [];
        for (let Decision = 0; Run.status === 'active' && Decision < 8; ++Decision) {
            const View = mfGetView(Run);
            const Route = mfChooseRoute(View, Experiment.policy);
            if (!Route) throw new Error(`No legal route at seed ${Seed}, policy ${Experiment.policy}`);
            Trace.push({ segment: View.round, remainingSteps: View.journey.remainingSteps,
                considered: View.journey.current.routes.map(Choice => ({ kind: Choice.kind, costSteps: Choice.costSteps,
                    extraScore: Choice.extraScore, enabled: Choice.quote.enabled, reason: Choice.quote.reason })),
                selected: Route.kind });
            Run = mfAct(Run, Route.action);
        }
        const Final = mfGetView(Run);
        console.log(JSON.stringify({ balance: 'three-meridians-v1', policy: Experiment.policy, settings: Experiment.settings,
            seed: Seed, outcome: Run.status, won: Run.status === 'success', reached_stage: Final.journey.completed.length,
            score: Run.score, resources: { remainingSteps: Final.journey.remainingSteps }, durationTurns: Trace.length,
            failure_reason: Run.status === 'success' ? null : Run.summary, reward: Run.reward, trace: Trace }));
    }
}
