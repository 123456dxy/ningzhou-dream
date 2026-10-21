const esc = value => String(value ?? '').replace(/[&<>"']/g,
  character => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[character]));

// The backend supplies all eligibility, schedule, healing and reward values.
// Classic and mobile share this view; their existing command queue owns writes.
export function renderAscensionTournament(state, {mobile = false} = {}) {
  const tournament = state?.ascensionTournament;
  if (!tournament) return '';
  const rounds = Array.isArray(tournament.rounds) ? tournament.rounds : [];
  const progress = tournament.attempted
    ? `本届已胜 ${esc(tournament.completedRounds)} / ${esc(tournament.roundCount)} 场`
    : tournament.isTournamentYear ? '本届开放报名' : `下届：第 ${esc(tournament.nextYear)} 年 · ${esc(tournament.nextAge)} 岁`;
  const command = mobile ? 'data-command="ascension-tournament/enter" data-body="{}"'
    : 'data-action="ascension-tournament"';
  return `<section class="ascension-tournament card" aria-label="升仙大会">
    <div class="ascension-tournament-heading"><div><p class="ascension-tournament-eyebrow">六宗登仙擂</p>
      <h2>升仙大会</h2></div><span class="tag">${progress}</span></div>
    <p>${esc(tournament.rules)}</p>
    <ol class="ascension-tournament-rounds">${rounds.map(round => `<li class="${round.complete ? 'complete' : ''}">
      <span class="ascension-tournament-round-number">${round.complete ? '✓' : esc(round.round)}</span>
      <span>${esc(round.name)}${round.sectName ? `<small>${esc(round.sectName)}</small>` : ''}</span></li>`).join('')}</ol>
    <p class="ascension-tournament-reward">四战全胜：${esc(tournament.rewardStones)} 灵石 · ${esc(tournament.rewardPillName)} ×${esc(tournament.rewardPillCount)}</p>
    ${tournament.reason ? `<p class="meta" role="status">${esc(tournament.reason)}</p>` : ''}
    ${tournament.active ? '<p class="ascension-tournament-active" role="status">四战连擂进行中，请在斗法界面继续。</p>'
      : `<button type="button" class="primary btn" ${command}${tournament.enabled ? '' : ' disabled'}>报名登擂 · 四场连战</button>`}
  </section>`;
}
