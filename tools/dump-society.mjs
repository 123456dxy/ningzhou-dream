// 导出 society.mjs 的原创支线与野外悬赏（含 requires 谓词的求值结果）。
//
// SOC_STORIES 的 requires 是函数，不能直接序列化，所以这里为每个故事
// 记录它的「条件描述」，并在若干代表性玩家状态下求值，供 C++ 版对照。
import { SOC_STORIES, SOC_WILD_TASKS } from '../../michangsheng-demo/society.mjs';

// 列出每条故事的静态字段
const stories = SOC_STORIES.map(q => ({
  id: q.id,
  name: q.name,
  location: q.location,
  days: q.days,
  hidden: !!q.hidden,
  desc: q.desc,
  opening: q.opening,
  // options 形如 [['选项名','效果'], ...]
  options: (q.options || []).map(o => ({ name: o[0], effect: o[1] })),
  texts: q.texts || [],
  // requires 的函数源码，便于人工核对 C++ 里的等价实现
  requiresSource: typeof q.requires === 'function'
    ? q.requires.toString().replace(/\s+/g, ' ') : null,
}));

const wildTasks = SOC_WILD_TASKS.map(t => ({
  id: t.id, name: t.name, days: t.days, fee: t.fee,
  style: t.style, desc: t.desc,
}));

process.stdout.write(JSON.stringify({ stories, wildTasks }, null, 1));
