# 布局修复记录

你反馈「布局感觉好奇怪」——**是我改坏的**。下面是具体错在哪、怎么修的。

---

## 根因：我把原版的布局类用错了

原版 `styles.css` 里有一整套现成的布局类。我做新版面时**没读懂它们的语义就拿来用**，
导致结构和原版设计不符。

### 错误 1：`.chronicle-top` 当成外层容器

```css
/* 原版定义 */
.chronicle-top {
  padding: 19px 22px 14px;
  border-bottom: 1px solid #ebebdf;
  display: flex;
  align-items: center;
  justify-content: space-between;
}
```

它是**面板内部的标题栏**（左边标题、右边计数），原版用法：

```html
<section class="chronicle-panel">
  <div class="chronicle-top"><h3>岁月行卷</h3><span class="count">…</span></div>
  <div class="timeline">…</div>
</section>
```

我却拿它当两个面板的外层包裹：

```html
<!-- 我写的（错） -->
<div class="chronicle-top">
  <div class="chronicle-panel">…</div>
  <div class="chronicle-panel">…</div>
</div>
```

结果：一个带下边框的 flex 行里塞了两个面板，间距和边框都错位。

### 错误 2：`.chronicle-layout` 塞了三个子元素

```css
.chronicle-layout {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 254px;   /* 主区 + 窄侧栏 */
  gap: 22px;
}
```

原版是**两列**：左边主内容（自适应），右边 254px 侧栏。

我往里塞了三个子元素，第三个被挤进 254px 那一列并换行：

```
┌──────────────────────┬────────┐
│  主区                │ 侧栏   │
├──────────────────────┴────────┤
│  第三个元素被压成 254px ← 就是这里看着别扭
└───────────────────────────────┘
```

### 错误 3：漏了每页的 `.section-head`

原版每个页面都以 `heading()` 开头：

```js
function heading(title, description, tagText = '') {
  return `<div class="section-head">
    <div><h2>${title}</h2><p>${description}</p></div>
    ${tagText ? `<span class="tag">${tagText}</span>` : ''}
  </div>`;
}
```

我的页面直接就是内容，没有标题块，所以每页看起来都很突兀。

### 错误 4：重复造轮子

原版侧栏**已经有**这套类：

| 类 | 用途 |
| --- | --- |
| `.plan-panel` | 右侧栏容器（浅绿底、内边距） |
| `.plan-options` / `.plan-option` / `.plan-symbol` | 月度计划按钮列表 |
| `.year-controls` | 时间推进按钮 |
| `.hint-box` | 侧栏底部提示 |
| `.intro` | 侧栏小字说明 |

我自己另写了一套 `.time-btns` / `.chronicle-panel` 当侧栏用，风格和原版不一致。

### 错误 5：杜撰了不存在的类名

| 我用的 | 原版有没有 | 结果 |
| --- | --- | --- |
| `.combat-vs` | ✗ 没有 | 斗法中间那列没样式（原版中间列是 `.combat-intent`） |
| `.panel-note` | ✗ 没有 | 说明文字没样式 |
| `.dao-node-row` / `.dao-name` / `.dao-level` / `.dao-exp` / `.dao-nodes` | ✗ 没有 | 十二大道那一块完全没有排版 |
| `.chronicle-list` | ✗ 没有 | 纪事列表没样式（原版用 `.timeline`） |

### 错误 6：盒中盒

在 `.plan-panel`（自带边框和内边距）里又嵌了 `.chronicle-panel`（也自带边框和内边距），
两层框套在一起。原版侧栏是 `<h3>` + 内容直接排。

---

## 修法

### 新增 `web/shell.mjs`：把骨架抽出来

所有页面统一走这几个函数，保证结构和原版一致：

```js
heading(title, desc, tag)      // → .section-head
panel(title, count, body)      // → section.chronicle-panel > .chronicle-top
layout(main, aside)            // → .chronicle-layout（严格两个子元素）
asideBlock(title, content)     // → 侧栏小节（h3 + 内容，不套 panel）
timeline(logs)                 // → .timeline > .log-entry
yearControls()                 // → .year-controls
hintBox(lines)                 // → .hint-box
```

### 重写全部页面

六个页签（纪事 / 舆图 / 洞府 / 知交 / 机缘 / 坊市）和斗法界面
全部按原版结构重写：

```html
<div class="section-head">…</div>            ← 页面标题
<div class="chronicle-layout">
  <section class="chronicle-panel">          ← 主区
    <div class="chronicle-top">…</div>       ← 面板内标题栏
    <div class="timeline">…</div>
  </section>
  <aside class="plan-panel">                 ← 254px 侧栏
    <h3>本月的打算</h3>
    <div class="plan-options">…</div>
    <div class="year-controls">…</div>
    <div class="hint-box">…</div>
  </aside>
</div>
```

斗法也照原版 `cards-ui.mjs` 重写：

```html
<div class="card-combat">
  <header class="combat-round">…</header>
  <div class="combat-field">                 ← 三列 1fr 170px 1fr
    <section class="combat-actor">自身</section>
    <div class="combat-intent">敌方意图</div>  ← 中间列（我原来错写成 combat-vs）
    <section class="combat-actor combat-enemy">对手</section>
  </div>
  <div class="combat-flow">
    <div class="energy-orb">灵力</div>
    <p>…</p><button class="primary end-turn">结束回合 →</button>
  </div>
  <div class="combat-hand-label">…</div>
  <div class="combat-hand">…手牌…</div>
  <footer class="combat-bottom">…</footer>
</div>
```

### 补齐缺失的 CSS

原版没有、但我确实需要的类，在 `enhance.css` 里补了定义：

`.panel-note` `.dao-node` `.dao-node-btn` `.dao-name` `.dao-level`
`.dao-exp` `.dao-nodes` `.empty-hand` `.filters button` `.plan-option`
`.year-controls button` `.combat-intent` 内部排版 `.energy-orb` 等。

---

## 加了自动检查，防止再犯

新增 `tools/check-layout.mjs`，把这类错误变成可检测的：

```powershell
node tools\check-layout.mjs
```

它检查：

1. `.chronicle-top` 是否只由 `panel()` 生成（位置正确）
2. `layout()` 有没有用 `+` 拼接导致 3 个子元素
3. 六个页面是否都有 `heading()`
4. 用到的 141 个类名是否都有 CSS 定义
5. 有没有用原版不存在的类当布局容器

当前结果：

```
=== 布局类使用检查 ===
1) .chronicle-top 出现 1 次
  ✓ 由 panel() 统一生成，位置正确（面板内标题栏）
2) .chronicle-layout 的子元素数量
  ✓ 没有把多个面板拼进第一个参数
3) 每页是否都有 heading()
  ✓ 六个页面全部有
4) 用到的类是否有 CSS 定义
  ✓ 141 个类名都有定义
=== 结构检查通过 ===
```

---

## 说明

**这次是我该早点做的事**：动手做新版面之前，应该先读一遍原版 `styles.css`
把每个布局类的语义搞清楚，而不是看到名字差不多就拿来用。
原版那套 CSS 是配套的，用对了自然就整齐。

改动只涉及前端（`web/` 目录），C++ 规则层没动，717 项自测仍然全部通过。
