const Elements = ['gold', 'wood', 'water', 'fire', 'earth'];
const CompassOrder = ['wood', 'fire', 'earth', 'gold', 'water'];
const ElementTides = [['tidal', '自然灵潮（数量起伏）'], ['tidalSmooth', '稳灵阵（数量平滑）']];
const ElementNames = { gold: '金', wood: '木', water: '水', fire: '火', earth: '土' };
const Modes = { elements: '五行凝练', meridians: '三脉筑基' };
const Definitions = {
    elements: [
        ['basic', '基础凝液', '点选一系，凝气为液'], ['same', '同源凝练', '同系灵气深度凝练'],
        ['generate', '相生合炼', '选相生起点，自动取两系'], ['five', '五行合一', '点击罗盘中央，五行合一'],
        ['convert', '调息换气', '先点来源，再点目标'], ['gather', '聚气待发', '同系投入，下轮随机返气'],
    ],
    meridians: [
        ['safe', '安全通脉', '稳稳通过当前经脉'], ['nurture', '温养经脉', '多留时机，积累底蕴'], ['refine', '深炼经脉', '投入更多时机，深炼根基'],
    ],
};

const State = { Mode: location.hash === '#meridians' ? 'meridians' : 'elements', Session: sessionStorage.getItem('zhuji-lab-session'),
    Views: {}, Drafts: {}, Kind: null, Resolved: null, Quote: null, Busy: false, QuoteSerial: 0,
    ShownResults: new Set(), ToastTimer: null, QuotePending: false, PendingRequest: null, ConnectionBlocked: false,
    ConvertSource: null, QuoteRevision: null, Keep: {}, KeepRevision: null, Confirming: false, LastRitualKind: null, Payment: null,
    Feedback: null, FeedbackTimer: null };
const mfById = Id => document.getElementById(Id);
const mfEscape = Value => String(Value ?? '').replace(/[&<>"']/g, Character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[Character]));
const mfView = () => State.Views[State.Mode];
const mfTokens = View => Elements.flatMap(Element => Array(View.resources?.[Element] || 0).fill(Element));
const mfHpGain = Value => Number.isFinite(Value) ? Value.toLocaleString('zh-CN', { maximumFractionDigits: 1 }) : '待核算';
const DesktopLayout = matchMedia('(min-width:901px)');
DesktopLayout.addEventListener('change', Event => { mfById('preparation').open = Event.matches; });

function mfToast(Message) {
    mfById('toast').textContent = Message;
    mfById('toast').classList.add('show');
    clearTimeout(State.ToastTimer);
    State.ToastTimer = setTimeout(() => mfById('toast').classList.remove('show'), 4500);
}

async function mfRequest(Path, Body) {
    const Controller = new AbortController();
    const Timer = setTimeout(() => Controller.abort(), 12000);
    try {
        const Response = await fetch(`/api/${Path}`, { method: Body === undefined ? 'GET' : 'POST',
            headers: { 'Content-Type': 'application/json', ...(State.Session ? { 'x-lab-session': State.Session } : {}) },
            ...(Body === undefined ? {} : { body: JSON.stringify(Body) }), signal: Controller.signal });
        const Data = await Response.json();
        if (Data.views) mfAccept(Data);
        if (!Response.ok) { const Failure = new Error(Data.error || '连接中断，请重试。'); Failure.status = Response.status; throw Failure; }
        return Data;
    } catch (Failure) {
        if (Failure.name === 'AbortError') throw new Error('响应超时，可重试同一动作或刷新恢复进度。');
        throw Failure;
    } finally { clearTimeout(Timer); }
}

function mfAccept(Data) {
    State.Session = Data.sessionId;
    sessionStorage.setItem('zhuji-lab-session', State.Session);
    for (const Mode of Object.keys(Modes)) {
        if (!State.Views[Mode] || Data.views[Mode].revision >= State.Views[Mode].revision) State.Views[Mode] = Data.views[Mode];
    }
    for (const Mode of Object.keys(Modes)) {
        const Settings = Data.views[Mode].settings;
        State.Drafts[Mode] ||= { pills: Settings.pills, core: Settings.core, talent: Settings.talent, tide: Settings.tide };
        if (Mode === 'elements' && !ElementTides.some(([Id]) => Id === State.Drafts[Mode].tide)) State.Drafts[Mode].tide = 'tidal';
    }
}

function mfDraft() { return State.Drafts[State.Mode]; }
function mfReadDraft() {
    const Draft = mfDraft();
    if (!Draft) return;
    Draft.talent = mfById('talent').value;
    Draft.core = mfById('core').checked;
    if (State.Mode === 'elements') Draft.tide = mfById('tide').value;
    if (mfView()) mfRenderDraftNotice(mfView());
}

function mfRenderDraftNotice(View) {
    const IsElements = State.Mode === 'elements';
    const IsOldRun = IsElements && !ElementTides.some(([Id]) => Id === View.settings.tide);
    const IsDirty = (IsElements ? ['pills', 'core', 'talent', 'tide'] : ['pills', 'core', 'talent']).some(Key => mfDraft()[Key] !== View.settings[Key]);
    mfById('draft-note').classList.toggle('is-old-run', IsOldRun);
    mfById('draft-note').classList.toggle('is-dirty', IsDirty);
    mfById('draft-note').textContent = IsOldRun ? '当前局仍为旧灵潮，草稿已选自然灵潮；重新入关才生效。' : IsElements
        ? IsDirty ? '准备已更改，重新入关才生效；当前轮灵气不重抽。' : '进入每轮时抽气；刷新与改草稿不重抽当前轮。新准备需重新入关。'
        : IsDirty ? '准备已更改，重新入关才生效。准备步数最多12，超出部分不再增加收益。' : '准备步数最多12，超出部分不再增加收益；调整后重新入关生效。';
}

function mfRenderPreparation(View) {
    const Draft = mfDraft();
    const IsElements = State.Mode === 'elements';
    mfById('talent').value = Draft.talent;
    for (const Option of mfById('talent').options) {
        Option.disabled = IsElements ? Option.value === 'steady' : Elements.includes(Option.value);
    }
    mfById('pill-count').textContent = Draft.pills;
    mfById('core').checked = Draft.core;
    mfById('tide-field').classList.toggle('is-hidden', !IsElements);
    if (IsElements) {
        mfById('tide').innerHTML = ElementTides.map(([Id, Name]) => `<option value="${Id}">${Name}</option>`).join('');
        mfById('tide').value = Draft.tide;
    }
    mfById('pill-note').textContent = IsElements ? '每颗入关随机抽2点五行，可重复，最多6颗。入关时抽定，刷新不重抽。' : '基础9步；每颗筑基丹增加1步，最多6颗。';
    const PillSummary = mfById('pill-draw-summary');
    const PillResources = View.pillResources;
    const PillComposition = PillResources ? CompassOrder.filter(Element => PillResources[Element] > 0).map(Element => `${ElementNames[Element]}${PillResources[Element]}`).join(' · ') : '';
    PillSummary.textContent = IsElements ? `本局筑基丹抽气：${!View.settings.pills ? '未用丹' : PillComposition || '旧局未记录，重新入关后显示'}。`
        : `本局准备：${View.journey?.preparation?.hint || (View.settings.pills ? `${View.settings.pills}颗筑基丹，已计入准备步数` : '未用丹')}。`;
    PillSummary.title = IsElements && View.pillDraws?.length ? `本局实际抽取顺序：${View.pillDraws.map(Element => ElementNames[Element]).join(' · ')}` : '';
    mfById('core-note').textContent = IsElements ? '按当前方案提供额外灵气' : '准备步数＋1；先天道体另＋1';
    mfRenderDraftNotice(View);
    const Probabilities = View.preview.elementProbabilities;
    const HasProbabilities = Probabilities && Elements.every(Element => Number.isFinite(Probabilities[Element]));
    const EqualProbabilities = HasProbabilities && Elements.every(Element => Probabilities[Element] === Probabilities[Elements[0]]);
    const ProbabilityText = HasProbabilities ? EqualProbabilities ? `五行各${Math.round(Probabilities[Elements[0]] * 100)}%`
        : CompassOrder.map(Element => `${ElementNames[Element]}${Math.round(Probabilities[Element] * 100)}%`).join(' · ') : '五行概率由服务端提供';
    mfById('element-probabilities').classList.toggle('is-hidden', !IsElements);
    mfById('element-probabilities').textContent = `灵潮只影响数量，${ProbabilityText}。每轮进入时抽气，改准备须重新入关。`;
    mfById('rules').innerHTML = IsElements
        ? '<p>先选术式，再点五行罗盘直接施展；悬停或键盘聚焦查看后端实际报价。相生术选起点，自动取两系；五行合一点中央。</p><p>灵潮只影响数量，五行等概率。实际进入下一轮时才随机抽气，不预告未来组成；改草稿、切页和刷新不重抽当前轮。</p><p>聚气支付同系灵气，下轮随机返气，可抽回原系；数量以实际报价为准。罗盘仅显示现有量，随机储备显示总数。换气先点来源再点目标，取消不扣气。</p><p>超过100分，每多1分永久增加1点基础生命，斗法等效为基础增量的2.4倍。品质名称是标签，实际奖励以服务端报价为准。</p><p>凝基定型、最后一轮结束和收功离关需明确确认；成功后根基永久确定。</p>'
        : '<p>依次通过任脉、督脉、冲脉，共9基础步。进入当前段才揭晓温养与深炼的额外收益，未来段只显示名称。</p><p>每段在安全、温养、深炼三路中选择。按钮的耗步、额外凝练和拒绝原因均来自真实报价；必须为后段保留安全通脉所需步数。</p><p>每颗筑基丹＋1步，灵核＋1步，先天道体＋1步；道心坚毅首次温养少耗1步，以实际报价为准。</p><p>三段完成自动结基，无需另留封基步。进行中的分数是预计底蕴，成功后永久确定；超过100分的生命奖励与方案一相同。</p>';
    for (const Control of mfById('preparation').querySelectorAll('button,input,select')) Control.disabled = State.Busy || State.ConnectionBlocked;
    for (const Option of mfById('talent').options) Option.disabled = IsElements ? Option.value === 'steady' : Elements.includes(Option.value);
}

function mfCompassChoices() { return mfView()?.compass?.choices || []; }
function mfCompassChoice(Element) {
    return mfCompassChoices().find(Choice => Choice.kind === State.Kind &&
        (State.Kind === 'convert' ? Choice.source === State.ConvertSource && Choice.target === Element : Choice.source === Element));
}

function mfPreviewChoice(Choice) {
    if (State.Busy || State.QuotePending || State.ConnectionBlocked || !Choice || mfView()?.status !== 'active') return;
    State.Quote = Choice.quote;
    State.Payment = Choice.payment || null;
    State.QuoteRevision = mfView().revision;
    State.Resolved = null;
    State.Confirming = false;
    mfRenderQuote();
}

function mfRenderCompass(View, Active) {
    const HasChoices = Array.isArray(View.compass?.choices);
    const Feedback = State.Feedback?.revision === View.revision && State.Feedback.mode === State.Mode ? State.Feedback : null;
    mfById('five-compass').innerHTML = `<svg class="compass-lines" viewBox="0 0 400 250" aria-hidden="true"><ellipse cx="200" cy="126" rx="142" ry="92"/><path d="M200 28 335 94 284 202 116 202 65 94Z"/><text x="279" y="43">生</text><text x="335" y="163">生</text><text x="197" y="225">生</text><text x="62" y="163">生</text><text x="112" y="43">生</text></svg>` + CompassOrder.map(Element => {
        const SourceChoices = State.Kind === 'convert' && !State.ConvertSource
            ? mfCompassChoices().filter(Choice => Choice.kind === 'convert' && Choice.source === Element) : null;
        const Choice = SourceChoices ? SourceChoices.find(Item => Item.quote.enabled) || SourceChoices[0] : mfCompassChoice(Element);
        const IsCancel = State.Kind === 'convert' && State.ConvertSource === Element;
        const Enabled = IsCancel || (SourceChoices ? SourceChoices.some(Item => Item.quote.enabled) : Boolean(Choice?.quote.enabled));
        const Reason = !HasChoices ? '当前服务未提供五行报价，请更新沙盘服务后重新连接。' : !State.Kind ? '先在右侧选择术式。' : State.Kind === 'five' ? '五行合一请点罗盘中央。' : IsCancel ? '再次点来源，取消此次换气。' : Choice?.quote.reason || (State.Kind === 'convert' && !State.ConvertSource ? '选择转出五行。' : '此五行不适用于当前术式。');
        const Hint = IsCancel ? '暂存来源 · 未扣气' : State.Kind === 'convert' && !State.ConvertSource ? '先定来源，再选目标' : Choice?.hint || (State.Kind === 'five' ? '请点中央' : '先选择术式');
        const Classes = [IsCancel ? 'is-source' : '', Active && !State.Busy && Enabled ? 'is-ready' : '',
            Feedback?.elements[Element] ? 'is-impact' : '', Feedback?.elements[Element] === 'gain' ? 'is-gaining' : ''].filter(Boolean).join(' ');
        const Lock = State.Kind && !Enabled ? '<svg class="compass-lock" viewBox="0 0 12 14" aria-hidden="true"><rect x="2" y="6" width="8" height="6" rx="1"/><path d="M3.5 6V4a2.5 2.5 0 0 1 5 0v2"/></svg>' : '';
        return `<button class="compass-node ${Classes}" data-compass="${Element}" data-element="${Element}" aria-disabled="${!Enabled}" aria-pressed="${IsCancel}" aria-label="${ElementNames[Element]}气，现有${View.resources[Element]}。${mfEscape(Hint)}。${mfEscape(Enabled ? IsCancel ? Reason : '点击施展当前术式' : Reason)}" title="${mfEscape(Reason)}" ${!Active || State.Busy ? 'disabled' : ''}>${Lock}<strong>${ElementNames[Element]}</strong><span>现有 <b>${View.resources[Element]}</b></span><small class="compass-hint">${mfEscape(Hint)}</small></button>`;
    }).join('');
    const Five = mfCompassChoices().find(Choice => Choice.kind === 'five');
    mfById('five-compass').insertAdjacentHTML('beforeend', State.ConvertSource
        ? `<button class="compass-center is-conversion" data-compass="cancel" title="取消暂存，不扣灵气。" ${!Active || State.Busy ? 'disabled' : ''}><strong>${ElementNames[State.ConvertSource]}气暂存</strong><span>→ ？</span><small>未扣气 · 点此取消</small></button>`
        : `<button class="compass-center ${State.Kind === 'five' && Five?.quote.enabled && Active && !State.Busy ? 'is-ready' : ''} ${Feedback?.score ? 'is-score-impact' : ''}" data-compass="five" aria-disabled="${State.Kind !== 'five' || !Five?.quote.enabled}" title="${mfEscape(State.Kind !== 'five' ? '先选择五行合一术式。' : Five?.quote.reason || '五行合一，点击直接施展。')}" ${!Active || State.Busy ? 'disabled' : ''}><strong>五行</strong><span>合一</span><small class="compass-hint">${State.Kind === 'five' ? mfEscape(Five?.hint || '') : ''}</small></button>`);
    mfById('compass-help').textContent = !HasChoices ? '当前服务未提供罗盘报价，请重新连接新版服务。' : State.Kind === 'convert'
        ? State.ConvertSource ? `已选${ElementNames[State.ConvertSource]}气转出；点目标才扣气，再点来源可取消。` : '调息换气：先点转出的五行，再点要补入的五行。'
        : State.Kind === 'five' ? `${Five?.hint || '五行合一'}；点中央直接施展。` : State.Kind ? '点五行直接施展；悬停或聚焦先看实际变化。' : '先在右侧选术式，再点五行；顺时针木 → 火 → 土 → 金 → 水。';
    mfById('cancel-conversion').classList.toggle('is-hidden', !State.ConvertSource);
}

function mfRenderCarry(View, Active) {
    const NeedsKeep = State.Mode === 'elements' && View.round < View.maxRounds && mfTokens(View).length > View.carryLimit;
    mfById('carry-panel').classList.toggle('is-hidden', !NeedsKeep);
    if (!NeedsKeep) return;
    if (State.KeepRevision !== View.revision) { State.Keep = Object.fromEntries(Elements.map(Element => [Element, 0])); State.KeepRevision = View.revision; }
    mfById('carry-note').textContent = `最多${View.carryLimit}枚，其余散去；未选即不保留。`;
    mfById('carry-inputs').innerHTML = CompassOrder.map(Element => `<label>${ElementNames[Element]}<input type="number" inputmode="numeric" min="0" max="${View.resources[Element]}" step="1" data-keep="${Element}" value="${State.Keep[Element] || 0}" aria-label="保留${ElementNames[Element]}气数量" ${!Active || State.Busy ? 'disabled' : ''}></label>`).join('');
}

function mfRenderJourney(View) {
    const Journey = View.journey;
    if (!Journey) { mfById('meridian-field').innerHTML = '<p class="empty-state">沙盘规则已更新，请重新连接或重新入关。</p>'; return; }
    mfById('meridian-field').innerHTML = `<div class="journey-map"><ol class="meridian-path">${Journey.segmentNames.map(Name => {
        const Completed = Journey.completed.find(Segment => Segment.name === Name);
        const IsCurrent = Journey.current?.name === Name;
        return `<li class="meridian-segment ${Completed ? 'is-done' : IsCurrent ? 'is-current' : 'is-unknown'}" ${IsCurrent ? 'aria-current="step"' : ''}><span class="meridian-seal" aria-hidden="true">${Completed ? '✓' : IsCurrent ? '◉' : '？'}</span><strong>${mfEscape(Name)}</strong><span>${Completed ? '已通' : IsCurrent ? '当前段' : '未揭晓'}</span><small>${Completed ? `${mfEscape(Completed.label)}<br>${Completed.costSteps}步 · 额外＋${Completed.extraScore}` : IsCurrent ? '三路可选<br>收益已显' : '进入该段<br>才知收益'}</small></li>`;
    }).join('')}</ol><p class="journey-observation">${Journey.current ? `当前${mfEscape(Journey.current.name)} · 后段至少需要 ${Journey.minFutureSteps} 步` : View.status === 'success' ? '三脉贯通，道基已定' : '本次修行已经结束'}</p></div>`;
}

function mfRenderActions(View, Active) {
    if (State.Mode === 'meridians') {
        const Routes = View.journey?.current?.routes || [];
        mfById('actions').innerHTML = Routes.length ? Routes.map(Route => `<button class="action-button route-button ${Route.kind === State.Kind ? 'is-selected' : ''}" data-kind="${Route.kind}" title="${mfEscape(Route.hint)}" aria-pressed="${Route.kind === State.Kind}" aria-disabled="${!Route.quote.enabled}" ${!Active || State.Busy ? 'disabled' : ''}><span class="route-top"><span class="action-name">${mfEscape(Route.label)}</span><strong class="route-cost">${Route.costSteps}步</strong></span><span class="action-effect">额外凝练＋${Route.extraScore}</span>${!Route.quote.enabled ? `<span class="action-reason">${mfEscape(Route.quote.reason)}</span>` : ''}</button>`).join('') : '<p class="empty-state">本次经脉修行已结束，可查看结算或重新入关。</p>';
        return;
    }
    mfById('actions').innerHTML = Definitions.elements.map(([Kind, Name, Description]) => {
        const Effect = Kind === 'five' ? mfCompassChoices().find(Choice => Choice.kind === 'five')?.hint || Description : Description;
        return `<button class="action-button ${Kind === State.Kind ? 'is-selected' : ''}" data-kind="${Kind}" title="${mfEscape(`${Name}：${Effect}`)}" aria-pressed="${Kind === State.Kind}" ${!Active || State.Busy ? 'disabled' : ''}><span class="action-name">${Name}</span><span class="action-effect">${mfEscape(Effect)}</span></button>`;
    }).join('');
}

function mfRender() {
    const View = mfView();
    if (!View) return;
    const IsElements = State.Mode === 'elements';
    const Active = View.status === 'active' && !State.ConnectionBlocked && (IsElements || Boolean(View.journey));
    document.body.dataset.labMode = State.Mode;
    document.body.setAttribute('aria-busy', String(State.Busy));
    for (const Tab of document.querySelectorAll('[data-mode]')) Tab.setAttribute('aria-selected', String(Tab.dataset.mode === State.Mode));
    mfById('mode-note').innerHTML = IsElements
        ? '<strong>新版 · 五行罗盘</strong><span>先择术，点系即施展。每轮随机来气，经营此生根基。</span>'
        : '<strong>方案二 · 三脉筑基</strong><span>任、督、冲逐段揭晓，分配有限步数，留下自己的根基。</span>';
    mfById('stage-kicker').textContent = IsElements ? '五气有序，凝液筑基' : '步步通脉，积养底蕴';
    mfById('stage-title').textContent = View.status !== 'active' ? ({ success: '道基已定', failed: '冲关未成', withdrawn: '收功归宁' }[View.status]) : IsElements ? '引气入鼎' : View.journey?.current?.name || '经脉初显';
    mfById('round-label').textContent = `第 ${View.round} / ${View.maxRounds} ${IsElements ? '轮' : '段'}`;
    mfById('round-track').innerHTML = Array.from({ length: View.maxRounds }, (_, Index) => `<span class="round-dot ${Index + 1 === View.round ? 'is-current' : Index + 1 < View.round ? 'is-done' : ''}">${Index + 1}</span>`).join('');
    mfById('status-strip').innerHTML = IsElements
        ? `<div class="status-item">可用灵气<strong>${mfTokens(View).length}</strong></div><div class="status-item">本轮操作<strong>${View.actionPoints} / 3</strong></div><div class="status-item">保留上限<strong>${View.carryLimit}</strong></div>`
        : `<div class="status-item">剩余步数<strong>${View.journey?.remainingSteps ?? '—'} / ${View.journey?.totalSteps ?? '—'}</strong></div><div class="status-item">已用步数<strong>${View.journey?.spentSteps ?? '—'}</strong></div><div class="status-item">后段至少<strong>${View.journey?.minFutureSteps ?? '—'}步</strong></div>`;
    mfById('five-compass').classList.toggle('is-hidden', !IsElements);
    mfById('compass-instruction').classList.toggle('is-hidden', !IsElements);
    mfById('meridian-field').classList.toggle('is-hidden', IsElements);
    mfById('ritual-scene').classList.toggle('meridian-scene', !IsElements);
    if (IsElements) mfRenderCompass(View, Active);
    if (!IsElements) mfRenderJourney(View);
    mfById('ritual-scene').querySelector('svg text').textContent = IsElements ? '聚气成液' : '三脉通玄';
    mfRenderCarry(View, Active);
    mfById('score-label').textContent = IsElements ? '道基凝练' : View.status === 'active' ? '预计底蕴' : '最终底蕴';
    mfById('score-value').textContent = `${View.score} 分`;
    const ScoreCap = View.preview.scoreCap;
    mfById('score-fill').style.width = `${Math.min(100, View.score / ScoreCap * 100)}%`;
    mfById('grade-stops').innerHTML = View.preview.grades.map(Grade => `<span class="${View.score >= Grade.score ? 'reached' : ''}">${Grade.score} ${mfEscape(Grade.grade)}</span>`).join('');
    const Achieved = [...View.preview.grades].reverse().find(Grade => View.score >= Grade.score);
    mfById('guide-note').textContent = !Active ? View.summary : IsElements
        ? `${Achieved ? `现在定型：${Achieved.grade}根基，基础生命＋${mfHpGain(View.preview.sealReward?.baseHpGain)}，斗法等效＋${mfHpGain(View.preview.sealReward?.battleHpEquivalent)}。` : `凝练达到${View.preview.grades[0].score}分即可筑基。`} ${View.actionPoints ? '点罗盘施术，品质由实际操作决定。' : '本轮调控已尽，检查余气后进入下一轮。'}`
        : `已通 ${View.journey?.completed.length ?? 0} / ${View.maxRounds} 段，当前预计${Achieved?.grade || '平实'}根基。预计基础生命＋${mfHpGain(View.preview.sealReward?.baseHpGain)}，斗法等效＋${mfHpGain(View.preview.sealReward?.battleHpEquivalent)}；三段全部通过后才永久结算。`;
    if (!IsElements && !View.journey) mfById('guide-note').textContent = '沙盘规则已更新，请重新连接或重新入关；新版经脉到齐前暂停路线操作。';
    mfById('next-draw-note').classList.toggle('is-hidden', !IsElements);
    const Probabilities = View.preview.elementProbabilities;
    const DrawProbability = Probabilities && Number.isFinite(Probabilities[Elements[0]]) && Elements.every(Element => Probabilities[Element] === Probabilities[Elements[0]])
        ? `五行各 ${Math.round(Probabilities[Elements[0]] * 100)}%` : '五行概率见准备说明';
    const PendingDrawCount = View.preview.pendingDrawCount ?? View.compass?.pendingRandomCount ?? 0;
    mfById('next-draw-note').textContent = View.status === 'active' && View.preview.nextDrawCount > 0
        ? `下一轮随机共抽 ${View.preview.nextDrawCount} 点${PendingDrawCount ? `（含聚气额外随机 ${PendingDrawCount} 点）` : ''}，${DrawProbability}；进入该轮后揭晓。` : '当前已是最后一轮，或本次修行已经结束。';
    mfById('history-count').textContent = View.history.length;
    mfById('history-list').innerHTML = [...View.history].reverse().slice(0, 40).map(Entry => `<li><strong>第${Entry.round}轮</strong> ${mfEscape(Entry.text)}</li>`).join('');
    mfById('action-instruction').textContent = IsElements ? '① 选择术式　② 点罗盘五行直接施展' : '选择本段路线，查看真实报价后确认。';
    mfById('action-heading').textContent = IsElements ? '施展秘术' : '本段三路';
    mfRenderActions(View, Active);
    mfById('round-controls').classList.toggle('is-hidden', !IsElements);
    mfById('seal').classList.toggle('is-hidden', !IsElements);
    mfById('settlement-note').textContent = IsElements ? '达标即成功，无最终概率判定。凝基后品质永久确定，本沙盘可随时重开体验。' : '三段通过即自动结基，无最终概率。当前分数仅为预计底蕴，成功后永久确定。';
    mfById('next').classList.toggle('is-hidden', !IsElements);
    mfById('next').textContent = View.round === View.maxRounds ? '结束本轮 · 定型' : '结束本轮 →';
    for (const Id of ['next', 'seal', 'withdraw']) mfById(Id).disabled = !Active || State.Busy;
    mfRenderPreparation(View);
    mfRenderQuote();
}

function mfRenderQuote() {
    const View = mfView();
    const Commit = mfById('commit');
    if (State.QuoteRevision !== View?.revision) { State.Quote = null; State.Resolved = null; State.Confirming = false; }
    Commit.classList.toggle('is-hidden', State.Mode === 'elements' && !State.Confirming && !State.ConnectionBlocked);
    const Route = State.Mode === 'meridians' ? View.journey?.current?.routes.find(Item => Item.kind === State.Kind) : null;
    const Name = Route?.label || Definitions[State.Mode].find(([Kind]) => Kind === State.Kind)?.[1] || ({ next: View?.round === View?.maxRounds ? '本次筑基定型' : '进入下一轮', seal: '凝基定型', withdraw: '收功离关' }[State.Kind]) || (State.Mode === 'elements' ? '选择术式' : '选择本段路线');
    if (State.QuotePending) {
        mfById('quote-panel').innerHTML = '<div class="quote-title">预见此招</div><p>正在推演……</p>';
    } else if (State.Quote) {
        const Quote = State.Quote;
        const Payment = State.Payment ? CompassOrder.filter(Element => State.Payment[Element]).map(Element => `${ElementNames[Element]}${State.Payment[Element]}`).join(' · ') : '';
        const Reward = Quote.after?.status === 'success' ? Quote.after.reward : null;
        const RewardText = Reward ? `<p class="quote-reward">永久基础生命＋${mfHpGain(Reward.baseHpGain)} · 斗法等效＋${mfHpGain(Reward.battleHpEquivalent)}</p>` : '';
        mfById('quote-panel').innerHTML = `<div class="quote-title">${mfEscape(Name)} · 实际预览</div>${Route?.hint ? `<p class="route-quote-hint">${mfEscape(Route.hint)}</p>` : ''}${Payment ? `<p class="quote-payment">耗气 ${Payment}</p>` : ''}${Quote.enabled ? `<div class="quote-changes">${Quote.changes.map(Change => `<p>${mfEscape(Change)}</p>`).join('')}</div>${RewardText}${Quote.after.status === 'failed' ? '<p class="action-reason">此操作将导致本次冲关失败。</p>' : ''}` : `<p class="action-reason">${mfEscape(Quote.reason)}</p>`}`;
    } else mfById('quote-panel').innerHTML = `<div class="quote-title">预见此招</div><p>${State.Mode === 'elements' ? State.ConvertSource ? '来源暂存于界面，尚未扣气。请点目标五行，或取消。' : State.Kind ? '悬停或聚焦罗盘五行查看实际报价，点击直接施展。' : '先选择术式，再查看罗盘各系报价。' : '选择本段路线，查看扣步和额外凝练后确认。未来段收益尚未揭晓。'}</p>`;
    if (State.ConnectionBlocked) {
        Commit.textContent = State.Busy ? '正在恢复……' : '重新连接 · 恢复此局';
        Commit.disabled = State.Busy;
        return;
    }
    const OverflowKeep = State.Mode === 'elements' && State.Kind === 'next' && View.round < View.maxRounds && mfTokens(View).length > View.carryLimit;
    Commit.textContent = State.Busy ? '正在施展……' : State.Quote?.enabled
        ? State.Kind === 'seal' || State.Quote.after.status === 'success' ? '确认永久定型'
            : State.Quote.after.status === 'failed' ? '确认结束本次冲关'
                : State.Kind === 'withdraw' ? '确认收功' : State.Kind === 'next' ? OverflowKeep ? '确认保留并结束本轮' : '确认结束本轮' : `${State.Mode === 'meridians' ? '确认' : '施展'} · ${Name}`
        : Name;
    Commit.disabled = State.Busy || State.QuotePending || !State.Quote?.enabled || !View || View.status !== 'active' || (State.Mode === 'meridians' && !View.journey);
}

async function mfSelectAction(Kind) {
    if (State.Busy || State.ConnectionBlocked || mfView()?.status !== 'active') return;
    const View = mfView();
    const Mode = State.Mode;
    if (Mode === 'meridians' && !View.journey) { mfToast('沙盘规则已更新，请重新连接或重新入关。'); return; }
    const Route = Mode === 'meridians' ? View.journey?.current?.routes.find(Item => Item.kind === Kind) : null;
    if (Route) {
        mfClearAction(); State.Kind = Kind; State.Quote = Route.quote; State.Resolved = Route.action;
        State.QuoteRevision = View.revision; State.Confirming = true; mfRender(); return;
    }
    const IsRitual = Mode === 'elements' && Definitions.elements.some(([Id]) => Id === Kind);
    if (IsRitual) { mfClearAction(); State.Kind = Kind; State.LastRitualKind = Kind; mfRender(); return; }
    if (State.QuotePending) return;
    State.Kind = Kind;
    State.ConvertSource = null;
    State.Quote = null;
    State.Resolved = null;
    State.Payment = null;
    State.QuotePending = true;
    State.QuoteRevision = View.revision;
    const OverflowKeep = Mode === 'elements' && Kind === 'next' && View.round < View.maxRounds && mfTokens(View).length > View.carryLimit;
    State.Confirming = Mode !== 'elements' || Kind !== 'next' || View.round === View.maxRounds || OverflowKeep;
    const Serial = ++State.QuoteSerial;
    const Action = { kind: Kind };
    if (Mode === 'elements' && Kind === 'next' && View.round < View.maxRounds && mfTokens(View).length > View.carryLimit) {
        const Counts = Elements.map(Element => Number(State.Keep[Element] || 0));
        if (Counts.some((Count, Index) => !Number.isInteger(Count) || Count < 0 || Count > View.resources[Elements[Index]]) || Counts.reduce((Sum, Count) => Sum + Count, 0) > View.carryLimit) {
            State.QuotePending = false; State.Quote = { enabled: false, reason: `保留数量须为现有范围内的整数，总数不超过${View.carryLimit}。` }; mfRender(); return;
        }
        Action.keep = Elements.flatMap((Element, Index) => Array(Counts[Index]).fill(Element));
    }
    mfRender();
    try {
        const Result = await mfRequest('quote', { mode: Mode, revision: View.revision, action: Action,
            autoPick: false });
        if (Serial !== State.QuoteSerial || Mode !== State.Mode || mfView().revision !== View.revision) return;
        State.Quote = Result.quote;
        State.Resolved = Result.action;
        if (Mode === 'elements' && Kind === 'next' && View.round < View.maxRounds && !OverflowKeep && Result.quote.enabled) {
            State.QuotePending = false;
            await mfMutate('action', { action: Result.action });
        }
    } catch (Failure) {
        if (Serial === State.QuoteSerial && Mode === State.Mode) State.Quote = { enabled: false, reason: Failure.message };
    } finally {
        if (Serial === State.QuoteSerial && Mode === State.Mode) { State.QuotePending = false; mfRender(); }
    }
}

function mfClearAction() {
    State.QuoteSerial++;
    State.Kind = null;
    State.Resolved = null;
    State.Quote = null;
    State.QuotePending = false;
    State.QuoteRevision = null;
    State.ConvertSource = null;
    State.Confirming = false;
    State.Payment = null;
}

function mfRecordFeedback(Before, After, Action) {
    if (After.mode !== 'elements' || After.revision <= Before.revision) return;
    const Changed = Object.fromEntries(Elements.filter(Element => Before.resources[Element] !== After.resources[Element])
        .map(Element => [Element, After.resources[Element] > Before.resources[Element] ? 'gain' : 'spend']));
    const ActualDraw = Action.kind === 'next' && After.round > Before.round ? After.tide.find(Entry => Entry.round === After.round) : null;
    if (ActualDraw) for (const Element of Elements) if (ActualDraw.resources[Element] > 0) Changed[Element] = 'gain';
    if (!Object.keys(Changed).length && After.score === Before.score) return;
    const Feedback = { mode: 'elements', revision: After.revision, elements: Changed, score: After.score > Before.score };
    State.Feedback = Feedback;
    State.FeedbackTimer = setTimeout(() => {
        if (State.Feedback !== Feedback) return;
        State.Feedback = null;
        for (const Node of document.querySelectorAll('.is-impact,.is-gaining,.is-score-impact')) Node.classList.remove('is-impact', 'is-gaining', 'is-score-impact');
    }, 850);
}

async function mfMutate(Path, Content) {
    if (State.Busy || State.ConnectionBlocked) return;
    const Mode = State.Mode;
    const Before = State.Views[Mode];
    clearTimeout(State.FeedbackTimer); State.Feedback = null;
    const RetainedKind = Path === 'action' && Mode === 'elements' ? State.LastRitualKind : null;
    if (Path === 'start') State.LastRitualKind = null;
    const Request = { mode: Mode, revision: mfView().revision, ...Content, requestId: crypto.randomUUID() };
    State.PendingRequest = { Path, Request };
    State.Busy = true;
    State.QuoteSerial++;
    mfRender();
    try {
        await mfRequest(Path, Request);
        if (Path === 'action') mfRecordFeedback(Before, State.Views[Mode], Content.action);
        State.PendingRequest = null;
        mfClearAction();
        State.Kind = RetainedKind;
        const After = State.Views[Mode];
        const ActualDraw = Path === 'action' && Mode === 'elements' && Content.action.kind === 'next' && After.round > Before.round
            ? After.tide.find(Entry => Entry.round === After.round) : null;
        mfToast(Path === 'start' ? '已按当前准备重新入关。' : ActualDraw
            ? `第${After.round}轮实际入气：${CompassOrder.filter(Element => ActualDraw.resources[Element] > 0).map(Element => `${ElementNames[Element]}${ActualDraw.resources[Element]}`).join(' · ')}。` : '术式已结算。');
        if (State.Views[Mode].status !== 'active') mfShowResult(Mode);
    } catch (Failure) {
        mfToast(Failure.message);
        // Reconcile uncertain commits before allowing another mutation.
        try { await mfRequest('state'); State.PendingRequest = null; mfClearAction(); State.Kind = RetainedKind; }
        catch { State.ConnectionBlocked = true; mfToast('连接尚未恢复，操作已锁定。点击重新连接后继续此局。'); }
    } finally { State.Busy = false; mfRender(); }
}

function mfShowResult(Mode) {
    const View = State.Views[Mode];
    const Key = `${Mode}:${View.revision}:${View.status}`;
    if (State.ShownResults.has(Key)) return;
    State.ShownResults.add(Key);
    mfById('result-kicker').textContent = `${Modes[Mode]} · 本局结果`;
    mfById('result-title').textContent = { success: '筑基已成', failed: '冲关未成', withdrawn: '收功归宁' }[View.status];
    mfById('result-grade').textContent = View.status === 'success' ? `${View.grade}根基` : View.status === 'failed' ? '暂别仙关' : '留待来日';
    mfById('result-description').textContent = View.summary;
    mfById('result-stats').classList.add('is-flat-reward');
    mfById('result-stats').innerHTML = `<div class="result-score">${Mode === 'elements' ? '最终凝练' : '最终底蕴'}<strong>${View.score}</strong></div><div>永久基础生命<strong>＋${mfHpGain(View.reward?.baseHpGain)}</strong></div><div>斗法等效生命<strong>＋${mfHpGain(View.reward?.battleHpEquivalent)}</strong></div>`;
    mfById('result-backdrop').classList.remove('is-hidden');
    mfById('result-close').focus();
}

function mfSwitch(Mode) {
    if (State.Busy) return;
    mfReadDraft();
    State.Mode = Mode;
    history.replaceState(null, '', `#${Mode}`);
    mfClearAction();
    mfById('result-backdrop').classList.add('is-hidden');
    mfRender();
}

function mfPreviewCompass(Element) {
    if (State.Mode !== 'elements' || State.Busy || State.QuotePending || State.Confirming || !Definitions.elements.some(([Id]) => Id === State.Kind)) return;
    if (Element === 'cancel') { mfPreviewChoice({ quote: { enabled: false, reason: '点中央取消暂存，灵气不会扣除。' } }); return; }
    if (Element === 'five') {
        const Choice = State.Kind === 'five' ? mfCompassChoices().find(Item => Item.kind === 'five') : null;
        mfPreviewChoice(Choice || { quote: { enabled: false, reason: '先选择五行合一术式，再点中央施展。' } });
        return;
    }
    if (State.Kind === 'convert' && !State.ConvertSource) {
        const Choices = mfCompassChoices().filter(Choice => Choice.kind === 'convert' && Choice.source === Element);
        mfPreviewChoice({ quote: { enabled: false, reason: Choices.some(Choice => Choice.quote.enabled)
            ? `点${ElementNames[Element]}气暂定来源，再点目标查看实际报价；选择来源不扣气。` : Choices[0]?.quote.reason || '这系灵气当前不能转出。' } });
        return;
    }
    if (State.ConvertSource === Element) { mfPreviewChoice({ quote: { enabled: false, reason: '再次点来源，取消换气；此前没有扣气。' } }); return; }
    mfPreviewChoice(mfCompassChoice(Element) || { quote: { enabled: false, reason: '此五行不适用于当前术式。' } });
}

async function mfClickCompass(Element) {
    if (State.Busy || State.QuotePending || State.ConnectionBlocked || mfView()?.status !== 'active') return;
    if (Element === 'cancel') { State.ConvertSource = null; State.Quote = null; State.Resolved = null; mfRender(); return; }
    if (!State.Kind) { mfToast('先在施术区选择一种术式。'); return; }
    if (State.Kind === 'convert' && Element !== 'five') {
        if (State.ConvertSource === Element) { State.ConvertSource = null; State.Quote = null; mfRender(); return; }
        if (!State.ConvertSource) {
            const Sources = mfCompassChoices().filter(Choice => Choice.kind === 'convert' && Choice.source === Element);
            if (!Sources.some(Choice => Choice.quote.enabled)) { mfPreviewCompass(Element); mfToast(Sources[0]?.quote.reason || '这系灵气当前不能转出。'); return; }
            State.ConvertSource = Element; State.Quote = null; State.Resolved = null; mfRender();
            document.querySelector(`[data-compass="${Element}"]`)?.focus({ preventScroll: true });
            return;
        }
    }
    const Choice = Element === 'five' ? State.Kind === 'five' && mfCompassChoices().find(Item => Item.kind === 'five') : mfCompassChoice(Element);
    if (!Choice?.quote.enabled) { mfPreviewCompass(Element); mfToast(Choice?.quote.reason || '请按当前术式选择适用的五行。'); return; }
    await mfMutate('action', { action: Choice.action });
}

document.addEventListener('click', Event => {
    const Button = Event.target.closest('button');
    if (!Button || Button.disabled || State.Busy) return;
    if (Event.detail > 1 && (Button.dataset.compass || ['next', 'commit', 'restart', 'result-restart'].includes(Button.id))) return;
    if (State.ConnectionBlocked && Button.id !== 'commit') return;
    if (Button.dataset.mode) return mfSwitch(Button.dataset.mode);
    if (Button.dataset.step) { mfReadDraft(); mfDraft().pills = Math.max(0, Math.min(6, mfDraft().pills + Number(Button.dataset.step))); mfById('pill-count').textContent = mfDraft().pills; mfRenderDraftNotice(mfView()); return; }
    if (Button.dataset.preset) {
        State.Drafts[State.Mode] = { pills: Button.dataset.preset === 'prepared' ? State.Mode === 'elements' ? 2 : 1 : 0,
            core: Button.dataset.preset === 'prepared', talent: 'none', ...(State.Mode === 'elements' ? { tide: 'tidal' } : {}) };
        mfRender(); return;
    }
    if (Button.dataset.compass) return void mfClickCompass(Button.dataset.compass);
    if (Button.dataset.kind) return void mfSelectAction(Button.dataset.kind);
    if (Button.id === 'cancel-conversion') { State.ConvertSource = null; State.Quote = null; State.Resolved = null; mfRender(); return; }
    if (Button.id === 'restart' || Button.id === 'result-restart') {
        mfReadDraft(); mfById('result-backdrop').classList.add('is-hidden');
        const Settings = { ...mfDraft() };
        if (State.Mode === 'meridians') delete Settings.tide;
        return void mfMutate('start', { settings: Settings });
    }
    if (Button.id === 'commit' && State.ConnectionBlocked) return void mfRecover();
    if (Button.id === 'commit' && State.Resolved && State.Quote?.enabled) return void mfMutate('action', { action: State.Resolved });
    if (['next', 'seal', 'withdraw'].includes(Button.id)) return void mfSelectAction(Button.id);
    if (Button.id === 'result-close') mfById('result-backdrop').classList.add('is-hidden');
    if (Button.id === 'result-other') mfSwitch(State.Mode === 'elements' ? 'meridians' : 'elements');
    if (Button.dataset.jump) mfById(Button.dataset.jump).scrollIntoView({ behavior: 'smooth', block: 'start' });
});
document.addEventListener('pointerover', Event => { const Button = Event.target.closest('[data-compass]'); if (Button && !Button.contains(Event.relatedTarget)) mfPreviewCompass(Button.dataset.compass); });
document.addEventListener('focusin', Event => { const Button = Event.target.closest('[data-compass]'); if (Button) mfPreviewCompass(Button.dataset.compass); });
document.addEventListener('input', Event => {
    const Element = Event.target.dataset.keep;
    if (!Element || State.Busy) return;
    State.Keep[Element] = Event.target.value;
    State.QuoteSerial++; State.QuotePending = false; State.Quote = null; State.Resolved = null; State.Confirming = false;
    mfRenderQuote();
});
for (const Id of ['talent', 'core', 'tide']) mfById(Id).addEventListener('change', mfReadDraft);
document.addEventListener('keydown', Event => { if (Event.key === 'Escape') mfById('result-backdrop').classList.add('is-hidden'); });
window.addEventListener('hashchange', () => {
    const Mode = location.hash === '#meridians' ? 'meridians' : 'elements';
    if (Mode !== State.Mode) mfSwitch(Mode);
});

async function mfConnect() {
    try {
        if (State.Session) {
            try { await mfRequest('state'); }
            catch (Failure) {
                if (Failure.status !== 401) throw Failure;
                State.Session = null; State.Views = {}; await mfRequest('session', {});
            }
        } else await mfRequest('session', {});
        mfRender();
        mfById('preparation').open = DesktopLayout.matches;
        return true;
    } catch (Failure) {
        State.ConnectionBlocked = true;
        mfById('mode-note').textContent = '连接未完成，原局保留。请点击施术区的重新连接。';
        mfById('commit').textContent = '重新连接 · 恢复此局';
        mfById('commit').disabled = false;
        mfToast(Failure.message);
        return false;
    }
}

async function mfRecover() {
    if (State.Busy) return;
    State.Busy = true;
    try {
        const Connected = await mfConnect();
        if (Connected && Object.keys(State.Views).length) {
            State.ConnectionBlocked = false; State.PendingRequest = null; mfClearAction();
        }
    } finally { State.Busy = false; if (mfView()) mfRender(); }
}
void mfConnect();
