// Summarize production probe observations. Candidate products are projections,
// never substituted for real combat outcomes or advertised as win-rate results.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';

const input = resolve(process.argv[2]);
const runs = readFileSync(input, 'utf8').trim().split(/\r?\n/).filter(Boolean).map(line => JSON.parse(line));
assert.ok(runs.length > 0);
const stats = values => {
  const sorted = values.filter(Number.isFinite).sort((a, b) => a - b);
  const quantile = fraction => sorted[Math.floor((sorted.length - 1) * fraction)] ?? null;
  return { n: sorted.length, mean: sorted.length ? sorted.reduce((a, b) => a + b, 0) / sorted.length : null,
    p10: quantile(.1), p50: quantile(.5), p90: quantile(.9), max: sorted.at(-1) ?? null };
};
const groups = new Map(), outcomes = new Map();
for (const run of runs) {
  const supportArtifact = run.supportArtifact || 'none';
  const outcomeKey = `${run.build}|${run.tier}|${run.policy}|${run.stage}|${supportArtifact}`;
  const result = outcomes.get(outcomeKey) || { build: run.build, tier: run.tier, policy: run.policy, stage: run.stage, supportArtifact, runs: 0, wins: 0, losses: 0, censored: 0 };
  result.runs++; result[run.outcome === 'win' ? 'wins' : run.outcome === 'loss' ? 'losses' : 'censored']++; outcomes.set(outcomeKey, result);
  for (const sample of run.samples) {
    const key = `${outcomeKey}|${sample.turn}`;
    const group = groups.get(key) || { build: run.build, tier: run.tier, policy: run.policy, stage: run.stage, supportArtifact, round: sample.turn, possible: 0, observed: [] };
    group.possible++;
    if (sample.observed) group.observed.push({ ...sample, seed: run.seed, opponent: run.opponent });
    groups.set(key, group);
  }
}
const summaryGroups = [...groups.values()].map(group => {
  const { observed, ...identity } = group;
  const values = field => stats(observed.map(sample => sample[field]));
  const projection = multiplier => stats(observed.map(sample => sample.productionStatusAmount * multiplier));
  const fraction = multiplier => stats(observed.map(sample => sample.productionStatusAmount * multiplier / sample.directArtifactRawDamage));
  const tableIndex = group.stage / 3;
  const oldMultiplier = group.build === 'wood-poison' ? [1, 2, 5, 15, 45][tableIndex] : [1, 1.25, 1.5, 1.75, 2][tableIndex];
  const conservativeMultiplier = group.build === 'wood-poison' ? [1, 2, 4, 10, 25][tableIndex] : [.5, .75, 1, 1.25, 1.5][tableIndex];
  return { ...identity, observedCount: observed.length, zeroStatusCount: observed.filter(sample => !sample.productionStatusAmount).length,
    statusAmount: values('productionStatusAmount'), poisonLayers: values('poisonLayers'), poisonTick: values('poisonTickCache'),
    burnPool: values('burnPool'), maxEnemyHp: values('enemyMaxHp'), nonConsumingSpellMaxRaw: values('nonConsumingSpellMaxRaw'),
    productionStatusRawDamage: values('productionStatusRawDamage'), fixedDirectRawDamage: observed[0]?.directArtifactRawDamage ?? null,
    candidates: Object.fromEntries([.5, .75, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4, 5, 10, 15, 25, 45].map(multiplier => [multiplier, { damage: projection(multiplier), versusFixedDirect: fraction(multiplier) }])),
    tableProjections: Object.fromEntries([['original', oldMultiplier], ['conservative', conservativeMultiplier]].map(([name, multiplier]) => [name, {
      multiplier, damage: projection(multiplier), versusFixedDirect: fraction(multiplier)
    }])),
    peakExample: [...observed].sort((a, b) => b.productionStatusAmount - a.productionStatusAmount)[0] ?? null };
});
const normalized = runs.map(({ elapsedMicroseconds, ...run }) => run);
const digest = createHash('sha256').update(JSON.stringify(normalized)).digest('hex');
const damageSummaries = [...outcomes.values()].map(identity => {
  const matching = runs.filter(run => ['build', 'tier', 'policy', 'stage'].every(key => run[key] === identity[key]) && (run.supportArtifact || 'none') === identity.supportArtifact);
  return { ...identity, turns: stats(matching.map(run => run.turns)), peakPoisonLayers: stats(matching.map(run => run.peakPoisonLayers)),
    peakBurnPool: stats(matching.map(run => run.peakBurnPool)),
    poison72: { reached: matching.filter(run => run.firstPoison72Turn).length, possible: matching.filter(run => 'firstPoison72Turn' in run).length, firstTurn: stats(matching.map(run => run.firstPoison72Turn)) },
    poison90: { reached: matching.filter(run => run.firstPoison90Turn).length, possible: matching.filter(run => 'firstPoison90Turn' in run).length, firstTurn: stats(matching.map(run => run.firstPoison90Turn)) },
    observedNonterminalDirectHealthDamage: stats(matching.map(run => run.observedSpellHealthDamageTotalExcludingTerminal)),
    observedNonterminalDotDamage: stats(matching.map(run => run.actualLoggedDotTotalExcludingTerminal)),
    unobservableTerminalDotCount: matching.reduce((sum, run) => sum + run.unobservableTerminalDotCount, 0) };
});
const report = { question: 'Does a status-preserving strike at 1-3 times observed poison layers or burn tick-pool yield a bounded addition relative to fixed direct artifacts and real learned spells?',
  input, runCount: runs.length, reproducibilityDigestExcludingElapsed: digest,
  probeExecutableSha256: createHash('sha256').update(readFileSync(join(dirname(input), 'probe.exe'))).digest('hex'),
  inputJsonlSha256: createHash('sha256').update(readFileSync(input)).digest('hex'),
  seeds: [...new Set(runs.map(run => run.seed))], outcomes: [...outcomes.values()], damageSummaries, groups: summaryGroups,
  limitations: ['Initial and advanced are real realm-filtered BattleLab recommendation snapshots with level5 available manuals and recommended Dao nodes; not new-life or naturally progressed player profiles.',
    'Both policies use only public previews and natural draws; Preserve deliberately holds status consuming spells and is a stacking opportunity policy, not optimal play.',
    'Candidate damage products are arithmetic projections of observed state without equipping or casting a candidate attack artifact, so these are not candidate win-rate comparisons. Optional support fixtures equip only their real same-grade recipe pearl before a fresh production battleStart; the no-support restart is asserted identical to BattleLab opening.',
    'Round samples are after actions before end; natural early endings are censored, never filled with zeros.',
    'Natural DoT totals count production log entries only; terminal clearing can make the final tick unobservable.',
    'Learned-spell previews need not be currently held or affordable and are comparison references, not declared legal next actions.'] };
if (process.argv[3]) {
  const paired = readFileSync(resolve(process.argv[3]), 'utf8').trim().split(/\r?\n/).filter(Boolean).map(line => JSON.parse(line)).map(({ elapsedMicroseconds, ...run }) => run);
  report.pairedRerunExact = JSON.stringify(normalized) === JSON.stringify(paired);
  assert.ok(report.pairedRerunExact, 'Same binary, ordered seeds, policy and input must exactly reproduce every trace/state/outcome');
}
writeFileSync(join(dirname(input), 'summary.json'), JSON.stringify(report, null, 2));
const compact = summaryGroups.filter(group => group.policy === 'VisibleStackPreserveV1').map(group => ({
  build: group.build, tier: group.tier, stage: group.stage, supportArtifact: group.supportArtifact, round: group.round, observed: `${group.observedCount}/${group.possible}`,
  statusP50: group.statusAmount.p50, statusP90: group.statusAmount.p90,
  poisonTickP50: group.poisonTick.p50, direct: group.fixedDirectRawDamage,
  x1VsDirectP50: group.candidates[1].versusFixedDirect.p50, x2VsDirectP50: group.candidates[2].versusFixedDirect.p50,
  x3VsDirectP50: group.candidates[3].versusFixedDirect.p50, maxSpellP50: group.nonConsumingSpellMaxRaw.p50,
  originalTable: group.tableProjections.original, conservativeTable: group.tableProjections.conservative
}));
console.log(JSON.stringify({ runCount: runs.length, reproducibilityDigestExcludingElapsed: digest, pairedRerunExact: report.pairedRerunExact, compact }, null, 2));
