"""将真实C++离线赛报告整理成可审阅的Markdown与CSV，不参与战斗规则。"""
import argparse
import csv
import json
from pathlib import Path


# 用途：输出本轮真实诊断表；流程：离线验收；返回：有中断/非法/差异时非零，不把未完成运行表述为通过。
def mfMain():
    Parser = argparse.ArgumentParser()
    Parser.add_argument("input", type=Path)
    Parser.add_argument("--markdown", type=Path, default=Path("docs/BATTLE_BALANCE_RESULTS_2026_10_15.md"))
    Parser.add_argument("--matrix-csv", type=Path, default=Path("docs/battle_balance_matrix_2026_10_15.csv"))
    Parser.add_argument("--ranking-csv", type=Path, default=Path("docs/battle_swiss_rankings_2026_10_15.csv"))
    Parser.add_argument("--historical-note", default="")
    Arguments = Parser.parse_args()
    Report = json.loads(Arguments.input.read_text(encoding="utf-8"))
    Checks = Report["checks"]
    Invalid = any(Checks[Key] for Key in ("diagnosticAborts", "illegalActions", "directPreviewMismatches", "incomingPreviewMismatches", "forbiddenBeastMatches")) or Checks["games"] != Checks["legitimateGames"]
    BucketNames = {"ordinary": "普通弟子", "complete": "完整传承", "beast": "妖兽"}
    StageNames = {Item["stage"]: Item.get("realm", f"境界{Item['stage']}") for Item in Report["uniformNpcMatrices"]}
    StageNames.update({Item["stage"]: Item["realm"] for Item in Report["tournaments"]})
    Lines = ["# 本地斗法数值验收 · 2026-10-15", "", "本表来自真实C++规则、固定AI政策和隔离内存牌局，不是玩家人口胜率。只对列出的构建快照负责。", "",
             f"构建：`{Report['build']}`。", "", f"库SHA256：`{Report['librarySha256']}`。", "",
             f"政策：`{Report['policyVersion']}`。", ""]
    if Arguments.historical_note:
        Lines += ["历史快照：" + Arguments.historical_note, ""]
    Lines += ["## 均匀真实NPC矩阵", "",
             "六宗普通/完整各对五种妖兽，各180局；同宗完整对普通36局。三种子31/73/137、双腿控序。纯胜率=胜/总局数，含平得分率=(胜+0.5×平)/总局数。", "",
             "| 境界 | 普通对兽 胜/平/负 | 纯胜率 | 完整对兽 胜/平/负 | 纯胜率 | 完整对同宗普通 胜/平/负 | 纯胜率 / 含平得分率 |", "| --- | --- | ---: | --- | ---: | --- | ---: |"]
    MatrixRows = []
    for Matrix in Report["uniformNpcMatrices"]:
        Outcomes = {}
        for Row in Matrix["bucketHeadToHead"]:
            Key = (Row["leftBucket"], Row["rightBucket"])
            Outcomes[Key] = Row
            MatrixRows.append({"境界": StageNames[Matrix["stage"]], "stage": Matrix["stage"], "左组": BucketNames[Key[0]], "右组": BucketNames[Key[1]],
                               "左胜": Row["leftWins"], "平": Row["draws"], "右胜": Row["rightWins"], "局数": Row["games"],
                               "左胜率": f"{Row['leftWinFraction']:.6f}", "左得分率": f"{Row['leftScoreFraction']:.6f}"})
        Ordinary = Outcomes["ordinary", "beast"]
        Complete = Outcomes["complete", "beast"]
        CrossTier = Outcomes["complete", "ordinary"]
        Lines.append(f"| {StageNames[Matrix['stage']]} | {Ordinary['leftWins']}/{Ordinary['draws']}/{Ordinary['rightWins']} | {Ordinary['leftWinFraction']:.1%} | {Complete['leftWins']}/{Complete['draws']}/{Complete['rightWins']} | {Complete['leftWinFraction']:.1%} | {CrossTier['leftWins']}/{CrossTier['draws']}/{CrossTier['rightWins']} | {CrossTier['leftWinFraction']:.1%} / {CrossTier['leftScoreFraction']:.1%} |")
    Lines += ["", "这里的‘接近’是跨宗门整体比较，仍保留流派相克与先后手差异；炼气普通弟子略有优势。不能从整体统计推断任意完整模板必胜任意普通模板。", "",
              "## 实际检查", "", f"共{Checks['games']}局，完整结算{Checks['legitimateGames']}局；真实24轮平局{Checks['true24RoundDraws']}局，互亡平局{Checks['mutualDeathDraws']}局。", "",
              f"逐张伤害预览检查{Checks['directPreviewChecks']}次，差异{Checks['directPreviewMismatches']}；可直接比较的敌方来伤检查{Checks['incomingPreviewChecks']}次，差异{Checks['incomingPreviewMismatches']}。", "",
              f"非法动作{Checks['illegalActions']}、诊断中断{Checks['diagnosticAborts']}、禁配兽兽{Checks['forbiddenBeastMatches']}、重赛{Checks['repeatedMatches']}。", "",
              f"回合净HP含后续静养的{Checks['postHealNetHpDeferredChecks']}帧单独标明，不误判为意图差异；普通PvE终局清理Battle的{Checks['pveTerminalUnobservable']}帧不伪造精确终击观察值。", "",
              "验证结论：" + ("存在阻断，不得作为通过结果。" if Invalid else "已执行检查没有发现非法动作、预览差异或未完成局。"), "",
              "## 瑞士赛展示", "", "每境界17名、七轮；每轮一名修士空轮0分，妖兽始终参赛，故场数不同。此榜保留过程供体验参考，实力梯度判断使用上面的均匀矩阵。", ""]
    RankingRows = []
    for Tournament in Report["tournaments"]:
        Lines += [f"### {Tournament['realm']}", "", "| 名次 | 构筑 | 组别 | 积分 | Buchholz | 胜/平/负 | 空轮 |", "| ---: | --- | --- | ---: | ---: | --- | ---: |"]
        for Row in Tournament["ranking"]:
            Lines.append(f"| {Row['rank']} | {Row['name']} | {BucketNames[Row['bucket']]} | {Row['points']:g} | {Row['buchholz']:g} | {Row['wins']}/{Row['draws']}/{Row['losses']} | {Row['byes']} |")
            RankingRows.append({"境界": Tournament["realm"], "stage": Tournament["stage"], "名次": Row["rank"], "ID": Row["id"], "构筑": Row["name"],
                                "组别": BucketNames[Row["bucket"]], "积分": Row["points"], "Buchholz": Row["buchholz"], "胜": Row["wins"], "平": Row["draws"], "负": Row["losses"], "空轮": Row["byes"]})
        Lines.append("")
    Lines += ["## 边界", "", Report["limitations"], "", "详细配装、每次配对/种子/动作和八流派Lab补充保存在临时JSON；原始报告不加入正式游戏或Git。复跑与诊断口径见[BATTLE_SWISS_DIAGNOSTIC.md](BATTLE_SWISS_DIAGNOSTIC.md)。", ""]
    Arguments.markdown.parent.mkdir(parents=True, exist_ok=True)
    Arguments.markdown.write_text("\n".join(Lines), encoding="utf-8")
    for PathName, Rows in ((Arguments.matrix_csv, MatrixRows), (Arguments.ranking_csv, RankingRows)):
        PathName.parent.mkdir(parents=True, exist_ok=True)
        with PathName.open("w", encoding="utf-8-sig", newline="") as Stream:
            Writer = csv.DictWriter(Stream, fieldnames=list(Rows[0]))
            Writer.writeheader()
            Writer.writerows(Rows)
    print(json.dumps({"games": Checks["games"], "blocked": Invalid, "markdown": str(Arguments.markdown), "matrix_rows": len(MatrixRows), "ranking_rows": len(RankingRows)}, ensure_ascii=False))
    return 2 if Invalid else 0


if __name__ == "__main__":
    raise SystemExit(mfMain())
