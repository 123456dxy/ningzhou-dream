import { mfCreateRun, mfGetView, mfAct } from './engine.mjs';

// Simulation contract: initialization, RNG, actions, economy, effects, and rewards
// use engine.mjs unchanged. Only rendering/input are replaced. Policies receive
// mfGetView output only; no board import, raw RNG, unrevealed values, or oracle.
// Run: node prototypes/meridian-network-lab/balance-check.mjs --summary
// Add --trace SEED POLICY PILLS for one public-view/action trace. Default emits
// every run as JSONL. Fixed policy versions and ordered seeds make runs repeatable.
const Seeds = Array.from({ length: 32 }, (_, Index) => (101 + Index * 1543) >>> 0);
const Policies = ['random-legal-v1', 'gate-return-v1', 'visible-density-v1'];
const PillCounts = [0, 2, 6];
const MaxSteps = 500;

function mfPolicyRandom(Seed) {
  let Value = (Seed ^ 0x9e3779b9) >>> 0;
  return () => {
    Value ^= Value << 13; Value ^= Value >>> 17; Value ^= Value << 5;
    return (Value >>> 0) / 4294967296;
  };
}

function mfGraph(View) {
  const Graph = new Map(View.nodes.map(Node => [Node.id, []]));
  for (const Edge of View.edges) if (!Edge.locked) {
    Graph.get(Edge.from).push(Edge.to); Graph.get(Edge.to).push(Edge.from);
  }
  return Graph;
}

// Deterministic public-map route estimate. Unknown nodes use a fixed prior,
// never their hidden type, cost, chance, points, effect, or random outcome.
function mfPath(View, TargetId, OpenedOnly = false) {
  const Nodes = new Map(View.nodes.map(Node => [Node.id, Node]));
  const Graph = mfGraph(View); const Distance = new Map([[View.positionId, 0]]);
  const Previous = new Map(); const Pending = new Set(Nodes.keys());
  while (Pending.size) {
    const Current = [...Pending].filter(Id => Distance.has(Id))
      .sort((Left, Right) => Distance.get(Left) - Distance.get(Right) || Left.localeCompare(Right))[0];
    if (!Current) break;
    Pending.delete(Current);
    if (Current === TargetId) {
      const Result = []; let Id = Current;
      while (Id !== View.positionId) { Result.unshift(Id); Id = Previous.get(Id); }
      return { ids: Result, cost: Distance.get(Current) };
    }
    for (const NeighborId of Graph.get(Current)) {
      const Neighbor = Nodes.get(NeighborId);
      if (!Pending.has(NeighborId) || Neighbor.blocked || OpenedOnly && !Neighbor.opened && NeighborId !== TargetId) continue;
      const Cost = Neighbor.opened ? 1 : Neighbor.revealed ? Neighbor.cost / Math.max(0.01, Neighbor.successPercent / 100) : 8;
      const NextDistance = Distance.get(Current) + Cost;
      if (!Distance.has(NeighborId) || NextDistance < Distance.get(NeighborId)) {
        Distance.set(NeighborId, NextDistance); Previous.set(NeighborId, Current);
      }
    }
  }
  return null;
}

function mfRouteAction(View, Path) {
  if (!Path?.ids.length) return null;
  return View.choices.find(Choice => Choice.action.targetId === Path.ids[0] && Choice.quote.enabled)?.action ?? null;
}

function mfPick(View, Policy, Memory, Random) {
  const Legal = View.choices.filter(Choice => Choice.quote.enabled);
  const Seal = Legal.find(Choice => Choice.action.kind === 'seal');
  if (Seal && (Policy !== 'visible-density-v1' || View.rawScore >= 100)) return { action: Seal.action, reason: 'seal' };
  if (Policy === 'random-legal-v1') {
    const Candidates = Legal.filter(Choice => Choice.action.kind !== 'withdraw');
    return { action: Candidates[Math.floor(Random() * Candidates.length)]?.action ?? { kind: 'withdraw' }, reason: 'uniform legal non-withdraw action' };
  }
  const Nodes = new Map(View.nodes.map(Node => [Node.id, Node]));
  const Missing = new Set(View.gates.filter(Gate => !Gate.opened).map(Gate => Gate.id));
  const Failed = Nodes.get(View.lastFailedId);
  const Restore = Legal.find(Choice => Choice.action.kind === 'restore');
  if (Restore && Failed?.blocked && (Failed.gateId && Missing.has(Failed.gateId) || Memory.targetId === Failed.id)) {
    return { action: Restore.action, reason: 'restore exhausted useful target' };
  }
  if (Policy === 'gate-return-v1') {
    if (!Missing.size) return { action: mfRouteAction(View, mfPath(View, 'start', true)) ?? Seal?.action ?? { kind: 'withdraw' }, reason: 'return after three gates' };
    const Routes = View.nodes.filter(Node => Node.gateId && Missing.has(Node.gateId) && !Node.blocked)
      .map(Node => ({ id: Node.id, path: mfPath(View, Node.id) })).filter(Route => Route.path)
      .sort((Left, Right) => Left.path.cost - Right.path.cost || Left.id.localeCompare(Right.id));
    for (const Route of Routes) {
      const Action = mfRouteAction(View, Route.path);
      if (Action) return { action: Action, reason: `lowest estimated gate route ${Route.id}` };
    }
    return { action: { kind: 'withdraw' }, reason: 'no affordable first step toward missing gate' };
  }
  if (!Missing.size && View.rawScore >= 100) return { action: mfRouteAction(View, mfPath(View, 'start', true)) ?? Seal?.action ?? { kind: 'withdraw' }, reason: 'score goal reached; return' };
  const CurrentTarget = Nodes.get(Memory.targetId);
  if (CurrentTarget && !CurrentTarget.opened && !CurrentTarget.blocked) {
    const Action = mfRouteAction(View, mfPath(View, CurrentTarget.id, true));
    if (Action) return { action: Action, reason: `continue public target ${CurrentTarget.id}` };
  }
  Memory.targetId = null;
  const Candidates = [];
  for (const Node of View.nodes) {
    if (!Node.revealed || Node.opened || Node.blocked) continue;
    const Path = mfPath(View, Node.id, true); const Action = mfRouteAction(View, Path);
    if (!Action) continue;
    const Travel = Path.ids.length - 1;
    // Utility weights are policy preferences, not reimplementations of effects.
    const Utility = ({ gather: 5, boost: 1, peek: 0.5, link: 1 })[Node.kind] ?? 0;
    const GateValue = Node.gateId && Missing.has(Node.gateId) ? 12 : 0;
    const Value = (Node.points + Utility + GateValue) * Node.successPercent / 100 / (Node.cost + Travel);
    // After completing gates, reserve the already-open home route plus one hop
    // from a newly opened target. Before completion this policy accepts risk.
    const Home = mfPath(View, 'start', true)?.ids.length ?? Infinity;
    if (!Missing.size && View.qi < Travel + Node.cost + Home + Travel + 1) continue;
    Candidates.push({ id: Node.id, action: Action, value: Value });
  }
  Candidates.sort((Left, Right) => Right.value - Left.value || Left.id.localeCompare(Right.id));
  if (Candidates.length) {
    const Best = Candidates[0]; Memory.targetId = Best.id;
    return { action: Best.action, reason: `visible density ${Best.id}`, value: Best.value };
  }
  if (!Missing.size) return { action: mfRouteAction(View, mfPath(View, 'start', true)) ?? Seal?.action ?? { kind: 'withdraw' }, reason: 'no safe optional target; return' };
  return { action: { kind: 'withdraw' }, reason: 'no affordable visible frontier' };
}

function mfPlay(Seed, Policy, Pills, Trace = false) {
  let Run = mfCreateRun({ pills: Pills, core: true, talent: 'none' }, Seed);
  const Random = mfPolicyRandom(Seed); const Memory = {};
  const TraceRows = []; const Route = []; let Steps = 0; let Failures = 0; let Strikes = 0; let Restores = 0; let Reason = '';
  while (mfGetView(Run).status === 'active' && Steps < MaxSteps) {
    const View = mfGetView(Run); const Decision = mfPick(View, Policy, Memory, Random);
    if (!View.choices.some(Choice => Choice.quote.enabled && JSON.stringify(Choice.action) === JSON.stringify(Decision.action))) throw new Error(`Illegal policy action at ${Seed}/${Policy}/${Pills}`);
    const Next = mfAct(Run, Decision.action); const After = mfGetView(Next);
    if (!Number.isFinite(After.qi) || After.qi < 0 || After.score < 0 || After.score > 120) throw new Error(`Resource invariant at ${Seed}/${Policy}/${Pills}`);
    ++Steps; Reason = Decision.reason;
    if (Decision.action.kind === 'strike') { ++Strikes; if (After.positionId === View.positionId) ++Failures; }
    if (Decision.action.kind === 'restore') ++Restores;
    if (After.positionId !== View.positionId) Route.push(After.positionId);
    if (Trace) TraceRows.push({ revision: View.revision, positionId: View.positionId, qi: View.qi, score: View.score,
      legal: View.choices.filter(Choice => Choice.quote.enabled).map(Choice => Choice.action), ...Decision,
      after: { positionId: After.positionId, qi: After.qi, score: After.score, gates: After.gates.filter(Gate => Gate.opened).length } });
    Run = Next;
  }
  const View = mfGetView(Run);
  const Result = { balance: 'network-baseline-v1', policy: Policy, seed: Seed, pills: Pills, core: true, talent: 'none',
    outcome: View.status === 'success' ? 'win' : 'loss', status: View.status, gates: View.gates.filter(Gate => Gate.opened).length,
    score: View.score, rawScore: View.rawScore, qi: View.qi, baseHpGain: View.reward?.baseHpGain ?? 0,
    steps: Steps, strikes: Strikes, failures: Failures, restores: Restores, opened: View.nodes.filter(Node => Node.opened).length - 1,
    stopReason: Steps >= MaxSteps ? 'maxsteps' : Reason, route: Route };
  if (Trace) console.log(JSON.stringify({ ...Result, trace: TraceRows }, null, 2));
  return Result;
}

const TraceIndex = process.argv.indexOf('--trace');
if (TraceIndex >= 0) {
  const Seed = Number(process.argv[TraceIndex + 1]); const Policy = process.argv[TraceIndex + 2]; const Pills = Number(process.argv[TraceIndex + 3]);
  if (!Number.isInteger(Seed) || !Policies.includes(Policy) || !PillCounts.includes(Pills)) throw new Error('Usage: --trace SEED POLICY PILLS (0/2/6)');
  mfPlay(Seed, Policy, Pills, true);
} else {
  const Results = [];
  for (const Policy of Policies) for (const Pills of PillCounts) for (const Seed of Seeds) {
    const Result = mfPlay(Seed, Policy, Pills); Results.push(Result);
    if (!process.argv.includes('--summary')) console.log(JSON.stringify(Result));
  }
  const Groups = Policies.flatMap(Policy => PillCounts.map(Pills => {
    const Rows = Results.filter(Result => Result.policy === Policy && Result.pills === Pills);
    const Scores = Rows.map(Row => Row.score); const Wins = Rows.filter(Row => Row.outcome === 'win');
    return { policy: Policy, pills: Pills, n: Rows.length, wins: Wins.length, winRate: Wins.length / Rows.length,
      meanScore: Scores.reduce((Total, Score) => Total + Score, 0) / Rows.length, scoreRange: [Math.min(...Scores), Math.max(...Scores)],
      meanWinningScore: Wins.length ? Wins.reduce((Total, Row) => Total + Row.score, 0) / Wins.length : null,
      winningScoreRange: Wins.length ? [Math.min(...Wins.map(Row => Row.score)), Math.max(...Wins.map(Row => Row.score))] : null,
      capped120: Rows.filter(Row => Row.outcome === 'win' && Row.score === 120).length,
      hpWinners: Rows.filter(Row => Row.baseHpGain > 0).length, maxsteps: Rows.filter(Row => Row.stopReason === 'maxsteps').length,
      meanAwardedBaseHp: Rows.reduce((Total, Row) => Total + Row.baseHpGain, 0) / Rows.length,
      gatesHistogram: [0, 1, 2, 3].map(Count => Rows.filter(Row => Row.gates === Count).length),
      meanOpened: Rows.reduce((Total, Row) => Total + Row.opened, 0) / Rows.length,
      lossSeeds: Rows.filter(Row => Row.outcome === 'loss').map(Row => Row.seed) };
  }));
  const Paired = Policies.map(Policy => {
    const Low = Results.filter(Row => Row.policy === Policy && Row.pills === 0);
    const High = Results.filter(Row => Row.policy === Policy && Row.pills === 2);
    return { policy: Policy, pills: [0, 2], n: Seeds.length,
      changedRoutes: Low.filter((Row, Index) => JSON.stringify(Row.route) !== JSON.stringify(High[Index].route)).length,
      improvedScores: Low.filter((Row, Index) => Row.score < High[Index].score).length,
      worseScores: Low.filter((Row, Index) => Row.score > High[Index].score).length,
      meanAwardedHpDelta: High.reduce((Total, Row, Index) => Total + Row.baseHpGain - Low[Index].baseHpGain, 0) / Seeds.length,
      meanScoreDelta: High.reduce((Total, Row, Index) => Total + Row.score - Low[Index].score, 0) / Seeds.length };
  });
  console.log(JSON.stringify({ summary: true, balance: 'network-baseline-v1', seeds: Seeds, groups: Groups, paired: Paired,
    limitations: '32 paired seeds; fixed policies only; bot win rate is not human difficulty. Active/withdrawn scores are provisional and grant no HP.' }, null, 2));
}
