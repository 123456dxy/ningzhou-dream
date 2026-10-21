"""把 JS 版的数据表机械地转换成 C++ 头文件。

数据来源是 tools/data-dump.json（由 tools/dump-data.mjs 用 Node 直接从
JS 版模块导出的 JSON），所以 C++ 版和 JS 版的数据一定一致，不会因为
手工重打而出现偏差。

用法：先 `node tools/dump-data.mjs > tools/data-dump.json`，再运行本脚本。
"""
from pathlib import Path
import argparse
import json

ROOT = Path(__file__).resolve().parent.parent
DUMP = ROOT / 'tools' / 'data-dump.json'
OUT = ROOT / 'data'
OVERRIDES = OUT / 'reference_overrides.json'


def apply_overrides(dump, society, path):
    """本地规则只改生成输入；未知表、ID、字段及重复目标均拒绝。"""
    document = json.loads(path.read_text(encoding='utf-8'))
    if set(document) != {'schemaVersion', 'changes'} or document['schemaVersion'] != 1 or not isinstance(document['changes'], list):
        raise ValueError('本地数据覆盖文件结构或版本无效')
    seen = set()
    for change in document['changes']:
        if not isinstance(change, dict) or set(change) not in ({'table', 'id', 'set'}, {'table', 'id', 'remove'}):
            raise ValueError('覆盖项必须是table/id/set或table/id/remove')
        table, identifier = change['table'], change['id']
        if not isinstance(table, str) or not isinstance(identifier, str) or not identifier:
            raise ValueError('覆盖表名与ID必须是非空字符串')
        target = society.get(table, dump.get(table))
        if not isinstance(target, list):
            raise ValueError(f'未知或不支持覆盖的数据表: {table}')
        key = (table, identifier)
        if key in seen:
            raise ValueError(f'重复覆盖目标: {table}/{identifier}')
        seen.add(key)
        matches = [row for row in target if isinstance(row, dict) and row.get('id') == identifier]
        if len(matches) != 1:
            raise ValueError(f'覆盖目标不存在或原表ID重复: {table}/{identifier}')
        row = matches[0]
        if 'remove' in change:
            if change['remove'] is not True:
                raise ValueError('remove只接受true')
            target.remove(row)
        else:
            updates = change['set']
            if not isinstance(updates, dict) or not updates or 'id' in updates or set(updates) - set(row):
                raise ValueError(f'覆盖含未知字段或试图修改稳定ID: {table}/{identifier}')
            for field, value in updates.items():
                original = row[field]
                if original is not None and type(value) is not type(original):
                    raise ValueError(f'覆盖字段类型不一致: {table}/{identifier}/{field}')
            row.update(updates)


def cpp_str(s) -> str:
    """C++ 字符串字面量；源文件是 UTF-8，中文直接写进去。"""
    s = '' if s is None else str(s)
    s = s.replace('\\', '\\\\').replace('"', '\\"')
    s = s.replace('\n', '\\n').replace('\r', '').replace('\t', '\\t')
    return '"' + s + '"'


def cpp_int(v) -> str:
    if v is None:
        return '0'
    if isinstance(v, bool):
        return '1' if v else '0'
    if isinstance(v, float):
        return str(int(v))
    return str(v)


def cpp_num(v) -> str:
    if v is None:
        return '0.0'
    if isinstance(v, bool):
        return '1.0' if v else '0.0'
    return repr(float(v))


class Struct:
    """累积一个 struct 定义和它的静态数据表。"""

    def __init__(self, name, doc=''):
        self.name = name
        self.doc = doc
        self.fields = []      # (cpp 字段名, 类型)
        self.rows = []

    def field(self, cpp_name, ctype):
        self.fields.append((cpp_name, ctype))
        return self

    def add(self, **values):
        row = []
        for fname, ftype in self.fields:
            v = values.get(fname)
            if ftype == 'string':
                row.append(cpp_str(v if v is not None else ''))
            elif ftype == 'int':
                row.append(cpp_int(v))
            elif ftype == 'double':
                row.append(cpp_num(v))
            else:
                raise SystemExit(f'未知类型 {ftype}')
        self.rows.append(row)
        return self

    def render(self) -> str:
        L = []
        if self.doc:
            L.append(f'// {self.doc}')
        L.append(f'struct {self.name} {{')
        for fname, ftype in self.fields:
            L.append(f'    {ftype} {fname} = {{}};')
        L.append('};')
        L.append('')
        L.append(f'inline const vector<{self.name}>& {self.name}_TABLE() {{')
        L.append(f'    static const vector<{self.name}> data = {{')
        for row in self.rows:
            L.append('        {' + ', '.join(row) + '},')
        L.append('    };')
        L.append('    return data;')
        L.append('}')
        return '\n'.join(L)


HEADER = '''// 本文件由 tools/extract-data.py 自动生成，请勿手工修改。
// 数据由 Node 直接从 JS 版模块导出，保证两版一致。
#pragma once

#include <string>
#include <vector>

#include "core/base.h"

namespace nz::data {

using std::string;
using std::vector;

'''


def main(output_path=None, check_only=False, overrides_path=None):
    dump = json.loads(DUMP.read_text(encoding='utf-8'))
    # 支线与悬赏单独从一个 dump 文件读取
    soc_path = ROOT / 'tools' / 'society-dump.json'
    soc = json.loads(soc_path.read_text(encoding='utf-8'))
    apply_overrides(dump, soc, overrides_path or OVERRIDES)
    OUT.mkdir(parents=True, exist_ok=True)

    parts = []

    # ---------- 药材 ----------
    herbs = Struct('HerbDef', f'药材（JS 版 ART_HERBS，{len(dump["herbs"])} 种）')
    (herbs.field('id', 'string').field('name', 'string').field('grade', 'int')
          .field('temperature', 'int').field('main', 'string')
          .field('secondary', 'string').field('guide', 'string')
          .field('strength', 'int').field('sourcePrice', 'int')
          .field('price', 'int')
          .field('sourceLocation', 'string'))
    for h in dump['herbs']:
        herbs.add(id=h.get('id'), name=h.get('name'), grade=h.get('grade'),
                  temperature=h.get('temperature'), main=h.get('main'),
                  secondary=h.get('secondary'), guide=h.get('guide'),
                  strength=h.get('strength'), sourcePrice=h.get('sourcePrice'),
                  price=h.get('price'),
                  sourceLocation=h.get('sourceLocation'))
    parts.append(herbs.render())

    # ---------- 丹方 ----------
    recipes = Struct('RecipeDef',
                     f'丹方（JS 版 ART_RECIPES，{len(dump["recipes"])} 张）。ingredients 保存为 "药材id:数量;..." 字符串')
    (recipes.field('id', 'string').field('name', 'string').field('grade', 'int')
            .field('tolerance', 'int').field('sourceEffect', 'string')
            .field('kind', 'string').field('value', 'int')
            .field('dao', 'string').field('poison', 'int')
            .field('ingredients', 'string').field('effect', 'string'))
    for r in dump['recipes']:
        ings = r.get('ingredients') or []
        ing_s = ';'.join(
            f"{i.get('id', i.get('herb', ''))}:{i.get('count', 1)}"
            if isinstance(i, dict) else str(i)
            for i in ings)
        recipes.add(id=r.get('id'), name=r.get('name'), grade=r.get('grade'),
                    tolerance=r.get('tolerance'),
                    sourceEffect=r.get('sourceEffect'), kind=r.get('kind'),
                    value=r.get('value'), dao=r.get('dao'),
                    poison=r.get('poison'), ingredients=ing_s,
                    effect=r.get('effect'))
    parts.append(recipes.render())

    # ---------- 丹炉 ----------
    furnaces = Struct('FurnaceDef', '丹炉（JS 版 ART_FURNACES，7 座）')
    (furnaces.field('id', 'string').field('name', 'string').field('grade', 'int')
             .field('capacity', 'int').field('price', 'int'))
    for f in dump['furnaces']:
        furnaces.add(id=f.get('id'), name=f.get('name'), grade=f.get('grade'),
                     capacity=f.get('capacity'), price=f.get('price'))
    parts.append(furnaces.render())

    # ---------- 功法（拓展部分）----------
    manuals = Struct('ManualDef',
                     '功法（JS 版 BUILD_MANUALS 拓展条目）。mods 保存为 "键=值;..." 字符串')
    (manuals.field('id', 'string').field('name', 'string').field('dao', 'string')
            .field('stage', 'int').field('source', 'string')
            .field('price', 'int').field('effect', 'string').field('mods', 'string'))
    for m in dump['buildManuals']:
        # 除了已知字段，其余数值字段都当成功法加成
        known = {'id', 'name', 'dao', 'stage', 'source', 'price', 'effect'}
        mods = ';'.join(f'{k}={v}' for k, v in m.items()
                        if k not in known and isinstance(v, (int, float))
                        and not isinstance(v, bool))
        manuals.add(id=m.get('id'), name=m.get('name'), dao=m.get('dao'),
                    stage=m.get('stage'), source=m.get('source'),
                    price=m.get('price'), effect=m.get('effect'), mods=mods)
    parts.append(manuals.render())

    # ---------- 神通（拓展部分）----------
    spells = Struct('SpellDef', '神通（JS 版 BUILD_SPELLS 拓展条目）')
    (spells.field('id', 'string').field('name', 'string').field('dao', 'string')
           .field('cost', 'int').field('kind', 'string').field('scale', 'double')
           .field('stage', 'int').field('cooldown', 'int').field('desc', 'string'))
    for s in dump['buildSpells']:
        spells.add(id=s.get('id'), name=s.get('name'), dao=s.get('dao'),
                   cost=s.get('cost'), kind=s.get('kind'), scale=s.get('scale'),
                   stage=s.get('stage'), cooldown=s.get('cooldown'),
                   desc=s.get('desc'))
    parts.append(spells.render())

    # ---------- 打法攻略 ----------
    guides = Struct('GuideDef', '打法攻略（JS 版 BUILD_GUIDES）')
    (guides.field('name', 'string').field('manuals', 'string')
           .field('spells', 'string').field('text', 'string'))
    for g in dump['buildGuides']:
        guides.add(name=g.get('name'),
                   manuals=','.join(g.get('manuals') or []),
                   spells=','.join(g.get('spells') or []),
                   text=g.get('text'))
    parts.append(guides.render())

    # ---------- 炼器材料 ----------
    materials = Struct('MaterialDef', '炼器材料（JS 版 ART_MATERIALS，12 种）')
    (materials.field('id', 'string').field('name', 'string')
              .field('grade', 'int').field('polarity', 'string')
              .field('element', 'string').field('price', 'int')
              .field('dimensions', 'string'))
    for m in dump['materials']:
        materials.add(id=m.get('id'), name=m.get('name'), grade=m.get('grade'),
                      polarity=m.get('polarity'), element=m.get('element'),
                      price=m.get('price'),
                      # 五维压成 "160,300,0,100,220"
                      dimensions=','.join(str(x) for x in (m.get('dimensions') or [])))
    parts.append(materials.render())

    # ---------- 器型 ----------
    shapes = Struct('ShapeDef', '器型（JS 版 ART_SHAPES，5 种）')
    (shapes.field('id', 'string').field('name', 'string').field('slot', 'string'))
    for s in dump['shapes']:
        shapes.add(id=s.get('id'), name=s.get('name'), slot=s.get('slot'))
    parts.append(shapes.render())

    # ---------- 限时机缘 ----------
    quests = Struct('QuestDef',
                    '限时机缘（JS 版 ART_QUESTS，12 项）。requires 为前置机缘 id，空表示无')
    (quests.field('id', 'string').field('name', 'string').field('start', 'int')
           .field('end', 'int').field('stage', 'int').field('maxStage', 'int')
           .field('location', 'string').field('desc', 'string')
           .field('reward', 'string').field('price', 'int')
           .field('visits', 'int').field('requiresQuest', 'string')
           .field('battle', 'int'))
    for q in dump['quests']:
        quests.add(id=q.get('id'), name=q.get('name'), start=q.get('start'),
                   end=q.get('end'), stage=q.get('stage') or 0,
                   maxStage=q.get('maxStage') if q.get('maxStage') is not None else 12,
                   location=q.get('location'), desc=q.get('desc'),
                   reward=q.get('reward'), price=q.get('price'),
                   visits=q.get('visits') or 1,
                   requiresQuest=q.get('requires') or '',
                   battle=1 if q.get('battle') else 0)
    parts.append(quests.render())

    # ---------- 成就 ----------
    achs = Struct('AchievementDef', '成就（JS 版 ART_ACHIEVEMENTS，14 项）')
    (achs.field('id', 'string').field('key', 'string').field('target', 'int')
         .field('name', 'string').field('points', 'int'))
    for a in dump['achievements']:
        achs.add(id=a.get('id'), key=a.get('key'), target=a.get('target'),
                 name=a.get('name'), points=a.get('points'))
    parts.append(achs.render())

    # ---------- 基础功法（含每层系数）----------
    base_manuals = Struct('BaseManualDef',
                          '基础功法 14 部（arts.mjs 中显式写出的，含每层系数）')
    (base_manuals.field('id', 'string').field('name', 'string')
                 .field('dao', 'string').field('stage', 'int')
                 .field('price', 'int').field('effect', 'string')
                 .field('coefficients', 'string'))
    for m in dump['manuals']:
        known = {'id', 'name', 'dao', 'stage', 'price', 'effect'}
        coeffs = ';'.join(f'{k}={v}' for k, v in m.items()
                          if k not in known and isinstance(v, (int, float))
                          and not isinstance(v, bool))
        base_manuals.add(id=m.get('id'), name=m.get('name'), dao=m.get('dao'),
                         stage=m.get('stage'), price=m.get('price'),
                         effect=m.get('effect'), coefficients=coeffs)
    parts.append(base_manuals.render())

    # ---------- 基础神通 12 种 ----------
    base_spells = Struct('BaseSpellDef',
                         '基础神通 12 种（arts.mjs 中显式写出的）')
    (base_spells.field('id', 'string').field('name', 'string')
                .field('dao', 'string').field('cost', 'int')
                .field('kind', 'string').field('scale', 'double')
                .field('stage', 'int').field('cooldown', 'int')
                .field('desc', 'string'))
    # ART_SPELLS 前 12 个是基础，后面是拓展；按 id 去重后取全部
    seen_spell = set()
    for s in dump['spells']:
        if s.get('id') in seen_spell:
            continue
        seen_spell.add(s.get('id'))
        base_spells.add(id=s.get('id'), name=s.get('name'), dao=s.get('dao'),
                        cost=s.get('cost'), kind=s.get('kind'),
                        scale=s.get('scale'), stage=s.get('stage'),
                        cooldown=s.get('cooldown') or 0,
                        desc=s.get('desc') or '')
    parts.append(base_spells.render())

    # ---------- 完整天赋表（Demo06 扩到 39 项）----------
    talents = Struct('TalentRow',
                     '完整天赋表（JS 版 sim.mjs 的 TALENTS，39 项）。'
                     'stat 为加成的属性名，空表示无')
    (talents.field('id', 'string').field('name', 'string').field('tag', 'string')
            .field('cost', 'int').field('desc', 'string')
            .field('stat', 'string').field('value', 'int'))
    for t in dump['talents']:
        talents.add(id=t.get('id'), name=t.get('name'), tag=t.get('tag'),
                    cost=t.get('cost'), desc=t.get('desc'),
                    stat=t.get('stat') or '', value=t.get('value') or 0)
    parts.append(talents.render())

    # ---------- 五项开局天赋（发放实际物品）----------
    birth = Struct('BirthTalentDef',
                   '五项开局天赋（adventure.mjs 的 BIRTH_TALENTS）。'
                   '选到这些天赋时建号会发放法器/丹炉/材料')
    (birth.field('id', 'string').field('name', 'string').field('tag', 'string')
          .field('cost', 'int').field('desc', 'string'))
    for t in dump['birthTalents']:
        birth.add(id=t.get('id'), name=t.get('name'), tag=t.get('tag'),
                  cost=t.get('cost'), desc=t.get('desc'))
    parts.append(birth.render())

    # ---------- 起源文本 ----------
    origins = Struct('OriginDef', '天赋起源文本（ADV_ORIGINS，含五项新天赋）')
    (origins.field('id', 'string').field('text', 'string'))
    for k, v in dump['advOrigins'].items():
        origins.add(id=k, text=v)
    parts.append(origins.render())

    # ---------- 游历遇事类型 ----------
    kinds = Struct('AdvKindDef', '游历遇事类型（ADV_KINDS，6 类）')
    kinds.field('id', 'string')
    for k in dump['advKinds']:
        kinds.add(id=k)
    parts.append(kinds.render())

    # ---------- 七重剑道境界 ----------
    sw = Struct('SwordRealmDef',
                '七重剑道境界（sword.mjs 的 SWORD_REALMS）；'
                '剑意达到 minValue 即进入该重')
    (sw.field('minValue', 'int').field('name', 'string'))
    for r in dump['swordRealms']:
        sw.add(minValue=r.get('min'), name=r.get('name'))
    parts.append(sw.render())

    # ---------- 原创支线（8 段，含 5 段隐藏）----------
    # requires 是 JS 函数，这里另外生成一个「条件编号」字段，
    # C++ 侧按编号实现等价判断（见 society.cpp 的 storyRequiresMet）。
    story_cond = {
        'letter': 1,        # 成年（age>=18）
        'rootpool': 2,      # 筑基以上且土道经验>=30
        'rootecho': 3,      # 筑基以上且带特定灵根天赋
        'oldblade': 4,      # 斗法胜场>=3
        'mentorwill': 5,    # 有师尊且授业次数>=3
        'sharedmoon': 6,    # 有道侣且相伴>=90天
        'toxinMercy': 7,    # 炼丹成功>=5 且属竹山宗
        'seaLantern': 8,    # 金丹以上且水道、阵道经验各>=100
    }
    stories = Struct('StoryDef',
                     '原创支线（8 段，含 5 段隐藏）。conditionId 对应'
                     ' society.cpp 里的前置条件判断')
    (stories.field('id', 'string').field('name', 'string')
            .field('location', 'string').field('days', 'int')
            .field('hidden', 'int').field('conditionId', 'int')
            .field('desc', 'string').field('opening', 'string')
            .field('opt1Name', 'string').field('opt1Effect', 'string')
            .field('opt2Name', 'string').field('opt2Effect', 'string'))
    for s in soc['stories']:
        opts = s.get('options') or []
        o1 = opts[0] if len(opts) > 0 else {'name': '', 'effect': ''}
        o2 = opts[1] if len(opts) > 1 else {'name': '', 'effect': ''}
        stories.add(id=s.get('id'), name=s.get('name'),
                    location=s.get('location') or '',
                    days=s.get('days'), hidden=1 if s.get('hidden') else 0,
                    conditionId=story_cond.get(s.get('id'), 0),
                    desc=s.get('desc'), opening=s.get('opening'),
                    opt1Name=o1.get('name'), opt1Effect=o1.get('effect'),
                    opt2Name=o2.get('name'), opt2Effect=o2.get('effect'))
    parts.append(stories.render())

    # ---------- 野外悬赏 ----------
    wild = Struct('WildTaskDef', '野外悬赏（SOC_WILD_TASKS，3 类）')
    (wild.field('id', 'string').field('name', 'string').field('days', 'int')
        .field('fee', 'int').field('style', 'string').field('desc', 'string'))
    for t in soc['wildTasks']:
        wild.add(id=t.get('id'), name=t.get('name'), days=t.get('days'),
                 fee=t.get('fee'), style=t.get('style'), desc=t.get('desc'))
    parts.append(wild.render())

    body = HEADER + '\n\n'.join(parts) + '\n\n} // namespace nz::data\n'
    target = Path(output_path) if output_path else OUT / 'reference_tables.h'
    encoded = body.replace('\n', '\r\n').encode('utf-8')
    if check_only:
        if target.read_bytes() != encoded:
            raise SystemExit(f'生成表与输入不一致: {target}')
        print(f'逐字节一致: {target}')
        return
    target.write_text(body, encoding='utf-8', newline='\r\n')
    print(f'已写出 {target}')
    for st in (herbs, recipes, furnaces, manuals, spells, guides, materials,
               shapes, quests, achs, base_manuals, base_spells, talents,
               birth, origins, kinds, sw, stories, wild):
        print(f'  {st.name}: {len(st.rows)}')
    print(f'  文件 {len(body):,} 字符')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--overrides', type=Path)
    parser.add_argument('--check', action='store_true')
    arguments = parser.parse_args()
    main(arguments.output, arguments.check, arguments.overrides)
