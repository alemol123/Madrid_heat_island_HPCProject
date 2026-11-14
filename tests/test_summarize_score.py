#!/usr/bin/env python3
"""Test tools/summarize_score.py, including the comma inside 'dynamic,4'. Run: python3 tests/test_summarize_score.py"""
import os, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import summarize_score as ss

HEAD = "tag,threads,schedule,nx,ny,candidates,radius,selected,t_read_s,t_score_s,t_select_s,t_write_s,t_total_s\n"


def line(tag, t, sch, score, sel):
    tot = 0.02 + score + sel + 0.02
    return f"{tag},{t},{sch},4096,4096,7445841,40,5000,0.02,{score},{sel},0.02,{tot}\n"


def test_parse_and_summary():
    txt = HEAD
    for r, k in ((1, 0), (2, 0.1), (3, 0.2)):
        txt += line(f"a1{r}", 1, "dynamic,4", 5.2 + k, 0.85) + line(f"a32{r}", 32, "dynamic,4", 0.2 + k / 10, 0.85)
        txt += line(f"s{r}", 32, "static", 0.21, 0.85)
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "s.csv"); open(p, "w").write(txt)
        rows = ss.parse(p)
    assert len(rows) == 9 and rows[0]["schedule"] == "dynamic,4" and rows[2]["schedule"] == "static"
    out = ss.summarize(rows)
    d32 = next(o for o in out if o["schedule"] == "dynamic,4" and o["threads"] == 32)
    assert d32["reps"] == 3 and abs(d32["t_score"] - 0.21) < 1e-9
    assert abs(d32["speedup_score"] - 5.3 / 0.21) < 1e-6
    am = ss.amdahl(out)
    assert abs(am["max_speedup"] - 1 / (0.89 / (0.89 + 5.3))) < 1e-6


if __name__ == "__main__":
    test_parse_and_summary(); print("ok test_parse_and_summary")