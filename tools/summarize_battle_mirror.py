"""从组内真实互打报告输出六宗矩阵，明确区分纯胜率与含平局得分率。"""
import argparse
import csv
import json
from pathlib import Path


# 用途：整理同组互打900局与既有跨组真实胜平负；流程：离线数值评审；返回：有阻断时非零。
def mfMain():
    Parser = argparse.ArgumentParser()
    Parser.add_argument("input", type=Path)
    Parser.add_argument("--cross-tier", type=Path)
    Parser.add_argument("--markdown", type=Path, default=Path("docs/BATTLE_MIRROR_RESULTS_2026_10_15.md"))
    Parser.add_argument("--cells-csv", type=Path, default=Path("docs/battle_mirror_cells_2026_10_15.csv"))
    Parser.add_argument("--summary-csv", type=Path, default=Path("docs/battle_mirror_summary_2026_10_15.csv"))
    Parser.add_argument("--historical-note", default="")
    Arguments = Parser.parse_args()
    Report = json.loads(Arguments.input.read_text(encoding="utf-8"))
    Checks = Report["checks"]
    Blocked = any(Checks[Key] for Key in ("diagnosticAborts", "illegalActions", "directPreviewMismatches", "incomingPreviewMismatches")) or Checks["games"] != Checks["legitimateGames"]
    Names = {"jinhong": "金虹", "xinghe": "星河", "zhushan": "竹山", "lihuo": "离火", "huachen": "化尘", "xuandao": "玄道"}
    Buckets = {"ordinary": "普通", "complete": "完整"}
    Lines = ["# 六宗同组互打 · 2026-10-15", "", "使用真实NPC构筑，不用Lab建议代替；普通组和完整组各自对其余五宗，不含自身。各格三种子×交换座位双腿=6局，各宗每境界30局。", "",
             f"构建：`{Report['build']}`；库SHA256：`{Report['librarySha256']}`。", "",
             "纯胜率=胜/总局数；得分率=(胜+0.5×平)/总局数。同组整体得分率必为50%，不能把这个恒等式当平衡结论；应看各宗分布和具体相克。", ""]
    if Arguments.historical_note:
        Lines += ["历史快照：" + Arguments.historical_note, ""]
    Peaks = [(Matrix, Row) for Matrix in Report["sameTierMatrices"] for Row in Matrix["summary"] if Row["scoreFraction"] >= .8]
    if Peaks:
        Lines += ["## 阶段性强势观察", "", "下面仅列本次同组30局中含平得分率达到80%的观察项。单对手格只有6局；流派克制与阶段峰值不等于全境界碾压，也不据此强制将各宗拉到五成。", "",
                  "| 境界 | 组别 | 宗门 | 胜/平/负 | 纯胜率 | 含平得分率 |", "| --- | --- | --- | --- | ---: | ---: |"]
        for Matrix, Row in Peaks:
            Actor = Names[Row["id"].split(":")[0]]
            Lines.append(f"| {Matrix['realm']} | {Buckets[Matrix['bucket']]} | {Actor} | {Row['wins']}/{Row['draws']}/{Row['losses']} | {Row['winFraction']:.1%} | {Row['scoreFraction']:.1%} |")
        Lines.append("")
    if Arguments.cross_tier:
        Cross = json.loads(Arguments.cross_tier.read_text(encoding="utf-8"))
        Lines += ["## 完整对同宗普通的真实胜平负", "", "| 境界 | 完整胜/平/负 | 纯胜率 | 含平得分率 |", "| --- | --- | ---: | ---: |"]
        RealmNames = {Item["stage"]: Item["realm"] for Item in Cross["tournaments"]}
        for Matrix in Cross["uniformNpcMatrices"]:
            Row = next(Item for Item in Matrix["bucketHeadToHead"] if Item["leftBucket"] == "complete" and Item["rightBucket"] == "ordinary")
            Lines.append(f"| {RealmNames[Matrix['stage']]} | {Row['leftWins']}/{Row['draws']}/{Row['rightWins']} | {Row['leftWinFraction']:.1%} | {Row['leftScoreFraction']:.1%} |")
        Lines.append("")
    Cells, Summary = [], []
    for Matrix in Report["sameTierMatrices"]:
        Bucket = Buckets[Matrix["bucket"]]
        Lines += [f"## {Matrix['realm']} · {Bucket}组", "", "每格：行宗门的胜/平/负 · 含平得分率。", "",
                  "| 行对列 | " + " | ".join(Names[Item["id"].split(":")[0]] for Item in Matrix["participants"]) + " |",
                  "| --- | " + " | ".join(["---:"] * 6) + " |"]
        for Row in Matrix["matrix"]:
            Actor = Names[Row["id"].split(":")[0]]
            Values = []
            for Cell in Row["opponents"]:
                if Cell is None:
                    Values.append("—")
                    continue
                Opponent = Names[Cell["opponent"].split(":")[0]]
                Values.append(f"{Cell['wins']}/{Cell['draws']}/{Cell['losses']} · {Cell['scoreFraction']:.1%}")
                Cells.append({"境界": Matrix["realm"], "stage": Matrix["stage"], "组": Bucket, "行宗门": Actor, "列宗门": Opponent,
                              "胜": Cell["wins"], "平": Cell["draws"], "负": Cell["losses"], "局数": Cell["games"],
                              "纯胜率": f"{Cell['winFraction']:.6f}", "含平得分率": f"{Cell['scoreFraction']:.6f}"})
            Lines.append("| " + Actor + " | " + " | ".join(Values) + " |")
        Lines += ["", "| 宗门 | 胜/平/负 | 纯胜率 | 含平得分率 |", "| --- | --- | ---: | ---: |"]
        for Row in sorted(Matrix["summary"], key=lambda Item: Item["scoreFraction"], reverse=True):
            Actor = Names[Row["id"].split(":")[0]]
            Lines.append(f"| {Actor} | {Row['wins']}/{Row['draws']}/{Row['losses']} | {Row['winFraction']:.1%} | {Row['scoreFraction']:.1%} |")
            Summary.append({"境界": Matrix["realm"], "stage": Matrix["stage"], "组": Bucket, "宗门": Actor,
                            "胜": Row["wins"], "平": Row["draws"], "负": Row["losses"], "局数": Row["games"],
                            "纯胜率": f"{Row['winFraction']:.6f}", "含平得分率": f"{Row['scoreFraction']:.6f}"})
        Lines.append("")
    Lines += ["## 检查与边界", "", f"实际{Checks['games']}局，完整结算{Checks['legitimateGames']}局；非法{Checks['illegalActions']}、中断{Checks['diagnosticAborts']}、直接预览差异{Checks['directPreviewMismatches']}、来伤预览差异{Checks['incomingPreviewMismatches']}。", "",
              f"逐张直接预览检查{Checks['directPreviewChecks']}次、可比较来伤检查{Checks['incomingPreviewChecks']}次；真实24轮平局{Checks['true24RoundDraws']}局，互亡平局{Checks['mutualDeathDraws']}局。", "",
              "验证结论：" + ("存在阻断。" if Blocked else "本轮已执行检查通过。"), "", Report["limitations"], ""]
    Arguments.markdown.parent.mkdir(parents=True, exist_ok=True)
    Arguments.markdown.write_text("\n".join(Lines), encoding="utf-8")
    for Output, Rows in ((Arguments.cells_csv, Cells), (Arguments.summary_csv, Summary)):
        Output.parent.mkdir(parents=True, exist_ok=True)
        with Output.open("w", encoding="utf-8-sig", newline="") as Stream:
            Writer = csv.DictWriter(Stream, fieldnames=list(Rows[0])); Writer.writeheader(); Writer.writerows(Rows)
    print(json.dumps({"games": Checks["games"], "blocked": Blocked, "cells": len(Cells), "summary": len(Summary)}, ensure_ascii=False))
    return 2 if Blocked else 0


if __name__ == "__main__":
    raise SystemExit(mfMain())
