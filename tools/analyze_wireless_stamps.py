"""Compare r31's local enqueue stamps with Y700 kernel input timestamps."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import statistics

EVENT = re.compile(r'\[\s*([\d.]+)\]\s+(\w{4}) (\w{4}) (\w{8})')

def read_reports(path):
    axes = {}
    reports = []
    for time, kind, code, raw in EVENT.findall(path.read_text(encoding='utf-8')):
        if kind == '0002':
            axes[int(code,16)] = int(raw,16) & 0xffff
        elif kind == '0000' and code == '0000':
            if 5 in axes:
                low, high = axes.get(2,0), axes.get(7,0)
                if low & high & 0x8000:
                    tag = axes[5]
                    reports.append((float(time),(low>>9)&15,(high>>9)&63,tag>>11,
                                    tag&255,(low>>13)&3,(tag>>8)&7))
                else:
                    reports.append(None)
            axes = {}
    return reports

def quantiles(values):
    if not values:return {}
    ordered=sorted(values)
    return {name:round(ordered[round((len(ordered)-1)*q)],3)
            for name,q in [('p01',.01),('median',.5),('p95',.95),('p99',.99),('max',1)]}

def analyze(reports):
    stamped=[r for r in reports if r]
    if len(stamped)<3:raise ValueError('Not enough marked reports')
    pairs=[];skipped=Counter()
    for a,b in zip(reports,reports[1:]):
        if a is None or b is None:continue
        arrival=(b[0]-a[0])*1000
        seq=(b[1]-a[1])&15
        if seq!=1 or a[3]!=b[3] or not 0<arrival<min(160,64*b[3]):
            skipped[str(seq)]+=1
            continue
        pad=((b[2]-a[2])&63)*b[3]
        halfslots=(b[4]-a[4])&255
        pairs.append((b[0],arrival,pad,halfslots,b[5],b[6]))
    if not pairs:raise ValueError('No comparable consecutive mouse reports')
    host=sum(p[1] for p in pairs)
    if host<=0:raise ValueError('No elapsed host time')
    secondary_changes=len({p[4] for p in stamped})>1
    slow=[p for p in pairs if p[1]>35]
    groups=[]
    for p in slow:
        if not groups or p[0]-groups[-1][-1]>.24:groups.append([])
        groups[-1].append(p[0])
    starts=[g[0] for g in groups if len(g)>=2]
    tick_ratios=[]
    for i in range(0,len(pairs)-200,200):
        window=pairs[i:i+200]
        tick_ratios.append(sum(p[2] for p in window)/sum(p[1] for p in window))
    return dict(stamped_reports=len(stamped),seconds=round(stamped[-1][0]-stamped[0][0],2),
        comparable_pairs=len(pairs),unusable_sequence_or_time=dict(skipped),
        clock_period_ms=dict(Counter(p[3] for p in stamped)),
        src=dict(Counter(p[6] for p in stamped)),
        enqueue_clock_to_android_duration=round(sum(p[2] for p in pairs)/host,4),
        clock_ratio_windows=quantiles(tick_ratios),
        secondary_clock_distinct=len(set(p[4] for p in stamped)),
        halfslot_forward_ratio=round(sum(p[3] for p in pairs)*.3125/host,4) if secondary_changes else None,
        halfslot_backward_ratio=round(sum((-p[3])%256 for p in pairs)*.3125/host,4) if secondary_changes else None,
        host_ms=quantiles([p[1] for p in pairs]),enqueue_ms=quantiles([p[2] for p in pairs]),
        long_host_gaps=len(slow),enqueue_during_long=quantiles([p[2] for p in slow]),
        halfslot_ms_during_long=quantiles([p[3]*.3125 for p in slow]) if secondary_changes else None,
        long_with_enqueue_at_most_20ms=sum(p[2]<=20 for p in slow),
        long_with_enqueue_over_35ms=sum(p[2]>35 for p in slow),
        long_with_local_pressure=sum(bool(p[4]&1) for p in slow),
        long_with_enqueue_rejection=sum(bool(p[4]&2) for p in slow),
        pressure_flag_pairs=sum(bool(p[4]&1) for p in pairs),
        local_rejection_pairs=sum(bool(p[4]&2) for p in pairs),
        close_arrivals_with_enqueue_at_least_5ms=sum(p[1]<2 and p[2]>=5 for p in pairs),
        cluster_interval_s=quantiles([b-a for a,b in zip(starts,starts[1:])]))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    result=json.dumps(analyze(read_reports(args.trace)),indent=2)
    if args.output:args.output.write_text(result+'\n',encoding='utf-8')
    print(result)

