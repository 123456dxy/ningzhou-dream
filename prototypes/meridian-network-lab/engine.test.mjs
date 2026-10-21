import test from 'node:test';
import assert from 'node:assert/strict';
const Engine = await import('./engine.mjs').catch(() => ({}));

test('exposes a usable isolated network engine', () => {
  assert.equal(typeof Engine.mfCreateRun, 'function', 'new network engine implementation is required');
  const Run = Engine.mfCreateRun({}, 1);
  const View = Engine.mfGetView(Run);
  assert.equal(View.qi, 115);
  assert.equal(View.positionId, 'start');
  assert.equal(View.nodes.length, 36);
  assert.equal(View.gates.length, 3);
  assert.ok(View.edges.length > 40);
  assert.ok(View.nodes.some(Node => !Node.revealed));
  assert.equal(Engine.mfQuote(Run, { kind: 'strike', targetId: 'qihai' }).cost.qi, 5);
  assert.equal(View.settings.core, true);
});

test('north passage joins dantian to the central network and permits paid return without rescoring', () => {
  const Run = Engine.mfCreateRun({}, 1);
  const View = Engine.mfGetView(Run);
  const Neighbors = View.nodes.filter(Node => Node.isAdjacent).map(Node => Node.id).sort();
  assert.deepEqual(Neighbors, ['huiyin', 'qihai', 'shuidao', 'zhiyang']);
  assert.ok(View.edges.some(Edge => !Edge.locked
    && [Edge.from, Edge.to].includes('start') && [Edge.from, Edge.to].includes('zhiyang')));
  const North = View.nodes.find(Node => Node.id === 'zhiyang');
  const Start = View.nodes.find(Node => Node.id === 'start');
  assert.ok(North.y < Start.y && Math.abs(North.x - Start.x) < 5);
  const Quote = Engine.mfQuote(Run, { kind: 'strike', targetId: 'zhiyang' });
  assert.equal(Quote.enabled, true);
  assert.equal(Quote.cost.qi, 4);
  const Entered = Engine.mfAct(Run, { kind: 'strike', targetId: 'zhiyang' });
  assert.equal(Engine.mfGetView(Entered).positionId, 'zhiyang');
  assert.equal(Engine.mfGetView(Entered).qi, View.qi - Quote.cost.qi);
  assert.equal(Engine.mfQuote(Entered, { kind: 'strike', targetId: 'tianzhu' }).enabled, true);
  const Back = Engine.mfAct(Entered, { kind: 'move', targetId: 'start' });
  assert.equal(Engine.mfGetView(Back).positionId, 'start');
  assert.equal(Engine.mfGetView(Back).qi, Engine.mfGetView(Entered).qi - 1);
  assert.equal(Engine.mfGetView(Back).rawScore, Engine.mfGetView(Entered).rawScore);
  assert.equal(Engine.mfGetView(Run).positionId, 'start');
});

function mfNode(Run, Id) {
  return Engine.mfGetView(Run).nodes.find(Node => Node.id === Id);
}
function mfStrike(Run, Id) {
  return Engine.mfAct(Run, { kind: 'strike', targetId: Id });
}
function mfWalk(Run, Ids) {
  for (const Id of Ids) {
    const Action = { kind: mfNode(Run, Id).opened ? 'move' : 'strike', targetId: Id };
    let RetryCount = 0;
    while (Engine.mfGetView(Run).positionId !== Id) {
      const Quote = Engine.mfQuote(Run, Action);
      assert.ok(Quote.enabled, `${Id}: ${Quote.reason}`);
      Run = Engine.mfAct(Run, Action);
      assert.ok(++RetryCount < 4, `route did not reach ${Id}`);
    }
  }
  return Run;
}

test('rejects unknown settings, invalid seeds and malformed actions without changing the run', () => {
  for (const Settings of [null, [], { pills: -1 }, { pills: 7 }, { pills: 1.1 }, { pills: NaN }, { core: 1 }, { talent: 'bad' }, { unknown: true }]) {
    assert.throws(() => Engine.mfCreateRun(Settings, 1));
  }
  for (const Seed of [-1, 4294967296, NaN, 1.1, '1']) assert.throws(() => Engine.mfCreateRun({}, Seed));
  const Run = Engine.mfCreateRun({}, 1);
  const Before = JSON.stringify(Run);
  for (const Action of [null, [], {}, { kind: 'bad' }, { kind: 'strike' }, { kind: 'strike', targetId: 'bad' }, { kind: 'move', targetId: 1 }, { kind: 'restore', targetId: 'qihai' }, { kind: 'withdraw', cost: 1 }]) {
    assert.throws(() => Engine.mfQuote(Run, Action));
    assert.throws(() => Engine.mfAct(Run, Action));
    assert.equal(JSON.stringify(Run), Before);
  }
  assert.equal(Engine.mfQuote(Run, { kind: 'seal' }).enabled, false);
  assert.throws(() => Engine.mfAct(Run, { kind: 'seal' }));
  assert.equal(JSON.stringify(Run), Before);
});

test('query and quote do not mutate RNG and public views conceal distant details', () => {
  const Run = Engine.mfCreateRun({}, 1);
  const Before = JSON.stringify(Run);
  const View = Engine.mfGetView(Run);
  const Hidden = View.nodes.find(Node => Node.id === 'fengfu');
  assert.equal(Hidden.revealed, false);
  assert.equal(Hidden.label, '未明穴');
  for (const Key of ['kind', 'cost', 'points', 'successPercent', 'maxAttempts', 'effectText', 'gateId']) assert.equal(Key in Hidden, false, Key);
  assert.equal(View.nodes.filter(Node => Node.kind === 'gate').length, 6);
  assert.equal(Engine.mfQuote(Run, { kind: 'strike', targetId: 'fengfu' }).enabled, false);
  for (let Index = 0; Index < 10; ++Index) {
    Engine.mfGetView(Run);
    Engine.mfQuote(Run, { kind: 'strike', targetId: 'qihai' });
  }
  assert.equal(JSON.stringify(Run), Before);
  assert.doesNotMatch(JSON.stringify(View), /"(?:seed|randomState|roll)"/);
  assert.deepEqual(Engine.mfGetView(mfStrike(Run, 'qihai')), Engine.mfGetView(mfStrike(Engine.mfCreateRun({}, 1), 'qihai')));
});

test('a successful strike clones input, charges quote and applies each effect once', () => {
  const Run = Engine.mfCreateRun({}, 1);
  const Before = JSON.stringify(Run);
  const Quote = Engine.mfQuote(Run, { kind: 'strike', targetId: 'qihai' });
  const Next = mfStrike(Run, 'qihai');
  assert.equal(JSON.stringify(Run), Before);
  assert.notEqual(Next, Run);
  assert.equal(Engine.mfGetView(Next).positionId, 'qihai');
  assert.equal(Engine.mfGetView(Next).qi, Quote.outcomes.success.qi);
  assert.equal(Engine.mfGetView(Next).rawScore, 3);
  assert.deepEqual(Engine.mfGetView(Next).buff, { bonusPercent: 20, targetId: null });
  assert.equal(Engine.mfQuote(Next, { kind: 'strike', targetId: 'qihai' }).enabled, false);
  const Back = Engine.mfAct(Next, { kind: 'move', targetId: 'start' });
  const Return = Engine.mfAct(Back, { kind: 'move', targetId: 'qihai' });
  assert.equal(Engine.mfGetView(Return).qi, 108);
  assert.equal(Engine.mfGetView(Return).rawScore, 3);
  assert.equal(Engine.mfGetView(Return).returnCost, 1);
});

test('failures spend qi and attempts without moving or scoring; restore refills only attempts', () => {
  let Run = Engine.mfCreateRun({}, 1462);
  const FailureQuote = Engine.mfQuote(Run, { kind: 'strike', targetId: 'qihai' }).outcomes.failure;
  Run = mfStrike(Run, 'qihai');
  assert.equal(Engine.mfGetView(Run).qi, FailureQuote.qi);
  assert.equal(Engine.mfGetView(Run).positionId, 'start');
  assert.equal(Engine.mfGetView(Run).rawScore, 0);
  assert.equal(mfNode(Run, 'qihai').attemptsLeft, 1);
  Run = mfStrike(Run, 'qihai');
  assert.equal(mfNode(Run, 'qihai').blocked, true);
  assert.equal(Engine.mfQuote(Run, { kind: 'strike', targetId: 'qihai' }).enabled, false);
  assert.equal(Engine.mfQuote(Run, { kind: 'restore' }).enabled, true);
  const BeforeQi = Engine.mfGetView(Run).qi;
  Run = Engine.mfAct(Run, { kind: 'restore' });
  assert.equal(Engine.mfGetView(Run).qi, BeforeQi);
  assert.equal(mfNode(Run, 'qihai').attemptsLeft, 2);
  assert.equal(mfNode(Run, 'qihai').blocked, false);
  assert.equal(Engine.mfGetView(Run).coreAvailable, false);
  assert.equal(Engine.mfQuote(Run, { kind: 'restore' }).enabled, false);
});

test('boost binds only to actual strike and survives failed retries and restore', () => {
  let Run = mfStrike(Engine.mfCreateRun({}, 63), 'shuidao');
  assert.equal(Engine.mfQuote(Run, { kind: 'strike', targetId: 'weidao' }).successPercent, 95);
  assert.equal(Engine.mfGetView(Run).buff.targetId, null);
  Run = mfStrike(Run, 'weidao');
  assert.deepEqual(Engine.mfGetView(Run).buff, { bonusPercent: 20, targetId: 'weidao' });
  assert.equal(Engine.mfQuote(Run, { kind: 'strike', targetId: 'fuliu' }).successPercent, 85);
  Run = mfStrike(Run, 'weidao');
  Run = Engine.mfAct(Run, { kind: 'restore' });
  assert.equal(Engine.mfQuote(Run, { kind: 'strike', targetId: 'weidao' }).successPercent, 95);
  Run = mfStrike(Run, 'fuliu');
  assert.equal(Engine.mfGetView(Run).buff, null);
  assert.equal(Engine.mfQuote(Run, { kind: 'restore' }).enabled, false);
});

test('peek reveals two hops and successful strikes reveal adjacent details', () => {
  let Run = mfStrike(Engine.mfCreateRun({ pills: 6 }, 1), 'qihai');
  Run = mfStrike(Run, 'daheng');
  assert.equal(mfNode(Run, 'shenque').revealed, true);
  assert.equal(Engine.mfGetView(Run).edges.find(Edge => Edge.shortcutId === 'lowerBridge').locked, false);
  Run = Engine.mfAct(Run, { kind: 'move', targetId: 'qihai' });
  Run = mfWalk(Run, ['guanyuan', 'shimen']);
  assert.equal(mfNode(Run, 'fujie').revealed, true);
});

test('gather refunds twelve qi without applying an initial-qi cap', () => {
  const Run = mfStrike(Engine.mfCreateRun({}, 1), 'huiyin');
  assert.equal(Engine.mfGetView(Run).qi, 122);
  assert.equal(Engine.mfGetView(Run).rawScore, 2);
});

test('talents alter authoritative quote and preparation inputs stay in range', () => {
  const Dao = Engine.mfCreateRun({ pills: 0, core: false, talent: 'daoBody' }, 1);
  assert.equal(Engine.mfGetView(Dao).qi, 100);
  assert.equal(Engine.mfGetView(Dao).coreAvailable, false);
  assert.equal(Engine.mfQuote(Dao, { kind: 'strike', targetId: 'qihai' }).cost.qi, 4);
  const Steady = Engine.mfCreateRun({ talent: 'steady' }, 1);
  assert.equal(mfNode(Steady, 'renWest').successPercent, 80);
  assert.equal(mfNode(Steady, 'renWest').maxAttempts, 3);
  assert.equal(Engine.mfGetView(Engine.mfCreateRun({ pills: 6 }, 1)).qi, 190);
});

test('three distinct gate regions and returning to dantian are required before one final seal', () => {
  let Run = Engine.mfCreateRun({ pills: 6, talent: 'steady' }, 1);
  Run = mfWalk(Run, ['qihai', 'daheng', 'shenque', 'renEast']);
  assert.equal(Engine.mfGetView(Run).gates.find(Gate => Gate.id === 'ren').opened, true);
  assert.equal(Engine.mfQuote(Run, { kind: 'seal' }).enabled, false);
  Run = mfWalk(Run, ['zhangmen', 'duWest', 'baihui', 'duEast', 'shenting', 'tianzhu', 'chongWest']);
  assert.equal(Engine.mfGetView(Run).gates.every(Gate => Gate.opened), true);
  assert.equal(Engine.mfQuote(Run, { kind: 'seal' }).enabled, false);
  Run = mfWalk(Run, ['tianzhu', 'shenting', 'duEast', 'baihui', 'duWest', 'zhangmen', 'renEast', 'shenque', 'daheng', 'qihai', 'start']);
  const Before = Engine.mfGetView(Run);
  const Quote = Engine.mfQuote(Run, { kind: 'seal' });
  assert.equal(Quote.enabled, true);
  assert.equal(Quote.after.status, 'success');
  Run = Engine.mfAct(Run, { kind: 'seal' });
  const After = Engine.mfGetView(Run);
  assert.equal(After.qi, Before.qi);
  assert.equal(After.rawScore, Before.rawScore + 20);
  assert.equal(After.score, Math.min(120, Before.rawScore + 20));
  assert.deepEqual(After.reward, { baseHpGain: Math.max(0, After.score - 100), battleHpEquivalent: Math.round(Math.max(0, After.score - 100) * 24) / 10 });
  assert.equal(After.status, 'success');
  assert.equal(Engine.mfQuote(Run, { kind: 'seal' }).enabled, false);
  assert.equal(Engine.mfQuote(Run, { kind: 'move', targetId: 'qihai' }).enabled, false);
});

test('withdraw terminates without granting a reward and no low-qi action ends automatically', () => {
  const Run = Engine.mfAct(Engine.mfCreateRun({}, 1), { kind: 'withdraw' });
  assert.equal(Engine.mfGetView(Run).status, 'withdrawn');
  assert.equal(Engine.mfGetView(Run).reward, null);
  assert.equal(Engine.mfQuote(Run, { kind: 'withdraw' }).enabled, false);
});


test('seal confirmation includes complete authoritative score and reward; failed events point at target', () => {
  const Failure = Engine.mfGetView(mfStrike(Engine.mfCreateRun({}, 1462), 'qihai'));
  assert.equal(Failure.lastEvent.to, 'qihai');
  let Run = Engine.mfCreateRun({ pills: 6, talent: 'steady' }, 1);
  Run = mfWalk(Run, ['qihai', 'daheng', 'shenque', 'renEast', 'zhangmen', 'duWest', 'baihui', 'duEast', 'shenting', 'tianzhu', 'chongWest', 'tianzhu', 'zhangmen', 'shenque', 'daheng', 'qihai', 'start']);
  const Quote = Engine.mfQuote(Run, { kind: 'seal' });
  assert.deepEqual(Quote.after, Quote.outcomes.success);
  assert.equal(Quote.after.score, Engine.mfGetView(Run).rawScore + 20);
  assert.ok(Object.hasOwn(Quote.after.reward, 'baseHpGain'));
  assert.ok(Quote.changes.every(Change => typeof Change === 'string'));
});

test('unrevealed nodes expose only geometry, connectivity and public state', () => {
  const Hidden = Engine.mfGetView(Engine.mfCreateRun({}, 1)).nodes.find(Node => Node.id === 'fengfu');
  assert.deepEqual(Object.keys(Hidden).sort(), ['id', 'x', 'y', 'label', 'revealed', 'opened', 'blocked', 'adjacent'].sort());
});

test('restore and shortcut quotes include displayable authoritative descriptions', () => {
  let Run = mfStrike(Engine.mfCreateRun({}, 1462), 'qihai');
  assert.ok(Engine.mfQuote(Run, { kind: 'restore' }).changes.every(Change => typeof Change === 'string'));
  Run = mfStrike(Engine.mfCreateRun({}, 1), 'qihai');
  const Quote = Engine.mfQuote(Run, { kind: 'strike', targetId: 'daheng' });
  assert.ok(Quote.changes.length > 0);
  assert.ok(Quote.changes.every(Change => typeof Change === 'string'));
});

test('100-percent strikes still consume exactly one roll; moves, restores and quotes consume none', () => {
  let Run = Engine.mfCreateRun({ core: true }, 0);
  Run = mfStrike(Run, 'qihai');
  assert.equal(Engine.mfQuote(Run, { kind: 'strike', targetId: 'tianshu' }).successPercent, 100);
  Run = mfStrike(Run, 'tianshu');
  Run = Engine.mfAct(Run, { kind: 'move', targetId: 'qihai' });
  Run = Engine.mfAct(Run, { kind: 'move', targetId: 'tianshu' });
  for (let Index = 0; Index < 25; ++Index) {
    Engine.mfGetView(Run);
    Engine.mfQuote(Run, { kind: 'strike', targetId: 'renWest' });
  }
  Run = mfStrike(Run, 'renWest');
  assert.equal(Engine.mfGetView(Run).lastEvent.success, false);
  assert.equal(Engine.mfGetView(Run).positionId, 'tianshu');
  assert.equal(mfNode(Run, 'renWest').attemptsLeft, 2);
  Run = Engine.mfAct(Run, { kind: 'restore' });
  Run = mfStrike(Run, 'renWest');
  assert.equal(Engine.mfGetView(Run).lastEvent.success, true);
  assert.equal(Engine.mfGetView(Run).positionId, 'renWest');
});

test('moving after a failure removes restore eligibility, while quoting other targets does not', () => {
  let Run = mfStrike(Engine.mfCreateRun({}, 0), 'qihai');
  Run = mfStrike(Run, 'tianshu');
  Run = mfStrike(Run, 'renWest');
  assert.equal(Engine.mfGetView(Run).lastEvent.success, false);
  Engine.mfQuote(Run, { kind: 'strike', targetId: 'daheng' });
  assert.equal(Engine.mfQuote(Run, { kind: 'restore' }).enabled, true);
  Run = Engine.mfAct(Run, { kind: 'move', targetId: 'qihai' });
  assert.equal(Engine.mfQuote(Run, { kind: 'restore' }).enabled, false);
});

test('insufficient qi keeps the run active and permits a just-failed restore', () => {
  let Run = mfWalk(Engine.mfCreateRun({}, 0), ['qihai', 'tianshu']);
  while (Engine.mfGetView(Run).qi > 8) {
    Run = Engine.mfAct(Run, { kind: 'move', targetId: Engine.mfGetView(Run).positionId === 'tianshu' ? 'qihai' : 'tianshu' });
  }
  Run = mfStrike(Run, 'renWest');
  assert.equal(Engine.mfGetView(Run).lastEvent.success, false);
  assert.equal(Engine.mfGetView(Run).qi, 0);
  assert.equal(Engine.mfGetView(Run).status, 'active');
  assert.equal(Engine.mfQuote(Run, { kind: 'move', targetId: 'qihai' }).enabled, false);
  assert.equal(Engine.mfQuote(Run, { kind: 'restore' }).enabled, true);
  Run = Engine.mfAct(Run, { kind: 'restore' });
  assert.equal(Engine.mfGetView(Run).qi, 0);
  assert.equal(Engine.mfGetView(Run).status, 'active');
  assert.equal(Engine.mfQuote(Run, { kind: 'withdraw' }).enabled, true);
});

test('public board has alternate gates, loops and empty portrait space', () => {
  const View = Engine.mfGetView(Engine.mfCreateRun({}, 1));
  for (const Node of View.nodes) {
    assert.ok(Node.x >= 6 && Node.x <= 94 && Node.y >= 8 && Node.y <= 92);
    assert.equal(Node.x > 44 && Node.x < 56 && Node.y > 40 && Node.y < 60, false);
  }
  for (const Region of ['ren', 'du', 'chong']) assert.ok(View.nodes.filter(Node => Node.gateId === Region).length >= 2);
  assert.equal(View.edges.filter(Edge => Edge.locked).length, 2);
  const Reached = new Set(['start']);
  while (true) {
    const Before = Reached.size;
    for (const Edge of View.edges.filter(Edge => !Edge.locked)) {
      if (Reached.has(Edge.from) || Reached.has(Edge.to)) { Reached.add(Edge.from); Reached.add(Edge.to); }
    }
    if (Reached.size === Before) break;
  }
  assert.equal(Reached.size, 36);
  assert.ok(View.edges.filter(Edge => !Edge.locked).length > View.nodes.length);
});

test('mutating public snapshots cannot corrupt the stored run or nested history', () => {
  const Run = Engine.mfCreateRun({}, 1);
  const Before = JSON.stringify(Run);
  const View = Engine.mfGetView(Run);
  View.settings.pills = 6;
  View.nodes[0].opened = false;
  View.history[0].text = 'changed';
  View.choices[0].action.targetId = 'bad';
  const Quote = Engine.mfQuote(Run, { kind: 'strike', targetId: 'qihai' });
  Quote.outcomes.success.qi = -100;
  assert.equal(JSON.stringify(Run), Before);
});

test('an explored three-region route seals at the 120 cap and grants exactly twenty base HP', () => {
  let Run = Engine.mfCreateRun({ pills: 6, talent: 'steady' }, 1);
  Run = mfWalk(Run, ['qihai', 'daheng', 'shenque', 'renEast', 'zhangmen', 'duWest', 'baihui', 'duEast', 'shenting', 'tianzhu', 'chongWest', 'tianzhu', 'yintang', 'dazhui', 'fengfu', 'riyue', 'chongEast', 'jingmen', 'weidao', 'yongquan', 'sanyinjiao', 'guanyuan', 'shimen', 'zhongji', 'renWest', 'renEast', 'shenque', 'daheng', 'qihai', 'start']);
  const Quote = Engine.mfQuote(Run, { kind: 'seal' });
  assert.equal(Quote.after.rawScore, 152);
  assert.equal(Quote.after.score, 120);
  assert.deepEqual(Quote.after.reward, { baseHpGain: 20, battleHpEquivalent: 48 });
  const View = Engine.mfGetView(Engine.mfAct(Run, { kind: 'seal' }));
  assert.equal(View.rawScore, 152);
  assert.equal(View.score, 120);
  assert.equal(View.qi, 73);
  assert.deepEqual(View.reward, { baseHpGain: 20, battleHpEquivalent: 48 });
});
