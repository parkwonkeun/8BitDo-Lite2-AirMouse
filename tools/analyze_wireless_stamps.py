"""Compare Lite 2 local enqueue stamps with Android kernel report arrival times."""
import argparse
import bisect
from collections import Counter
import json
import itertools
from pathlib import Path
import re
import statistics

EVENT = re.compile(r'\[\s*([\d.]+)\]\s+(\w{4}) (\w{4}) (\w{8})')

def read_reports(path, include_motion=False):
    axes = {}
    reports = []
    data = path.read_bytes()
    encoding = 'utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'
    for time, kind, code, raw in EVENT.findall(data.decode(encoding)):
        if kind == '0002':
            axes[int(code,16)] = int(raw,16) & 0xffff
        elif kind == '0000' and code == '0000':
            if 5 in axes:
                low, high = axes.get(2,0), axes.get(7,0)
                if low & high & 0x8000:
                    tag = axes[5]
                    report = (float(time),(low>>9)&15,(high>>9)&63,tag>>11,
                              tag&255,(low>>13)&3,(tag>>8)&7)
                    if include_motion:
                        report += (bool(axes.get(0) or axes.get(1)),)
                    reports.append(report)
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


PHASES = ('before_request', 'after_ack', 'after_failed_request', 'reserved')
SWITCH_MARGIN = 2.0


def r50_queue(pairs, endpoint):
    codes = [pair[endpoint][4] & 63 for pair in pairs]
    known = [code for code in codes if code < 63]
    return {'valid': len(known), 'overflow_or_unknown': len(codes) - len(known),
            'nonzero_known': sum(code != 0 for code in known),
            'four_byte_units': quantiles(known)}


def r50_phase_summary(runs, phase):
    # Keep nonadjacent runs independent. An unmarked report breaks a run.
    selected = [run for run in runs if run and run[0][4] >> 6 == phase]
    all_pairs = []
    comparable = []
    clusters = []
    periods = []
    excluded = Counter()
    for run in selected:
        long_groups = []
        for a, b in zip(run, run[1:]):
            gap = (b[0] - a[0]) * 1000
            all_pairs.append((a, b, gap))
            # All observable arrivals count for stutter, even if the modulo
            # enqueue clock cannot be resolved or an intervening sequence is lost.
            if gap > 35:
                if not long_groups or b[0]-long_groups[-1][-1] > .175:
                    long_groups.append([])
                long_groups[-1].append(b[0])
            if ((b[1] - a[1]) & 15) != 1:
                excluded['nonconsecutive_serial'] += 1
            elif not a[3] or a[3] != b[3]:
                excluded['invalid_or_changed_tick'] += 1
            elif gap >= min(160, 64*b[3]):
                excluded['host_gap_too_long_for_enqueue_comparison'] += 1
            else:
                local = ((b[2]-a[2]) & 63) * b[3]
                comparable.append((a, b, gap, local))
        repeated = [group[0] for group in long_groups if len(group) >= 2]
        clusters.extend(repeated)
        periods.extend(b-a for a, b in zip(repeated, repeated[1:]))
    samples = [r for run in selected for r in run]
    long = [pair for pair in comparable if pair[2] > 35]
    short = [pair for pair in comparable if pair[2] <= 35]
    coverage = sum(p[2] for p in all_pairs) / 1000
    arrivals = [p[2] for p in all_pairs]
    total_long = sum(gap > 35 for gap in arrivals)
    def per_minute(n):
        return round(60*n/coverage, 3) if coverage else None
    motion = [r[7] for r in samples if len(r) > 7]
    spans = [{'start_s':run[0][0], 'end_s':run[-1][0], 'reports':len(run)} for run in selected]
    span = samples[-1][0]-samples[0][0] if len(samples) >= 2 else 0
    return {'segments':spans, 'marked_reports':len(samples), 'covered_s':round(coverage, 4),
            'wall_span_s':round(span, 4), 'coverage_fraction':round(coverage/span, 4) if span else None,
            'clock_period_ms':dict(Counter(r[3] for r in samples)),
            'source_counts':dict(Counter(r[6] for r in samples)),
            'moving_xy_fraction':round(sum(motion)/len(motion), 3) if motion else None,
            'observed_arrival_pairs':len(all_pairs), 'comparable_enqueue_pairs':len(comparable),
            'comparable_fraction':round(len(comparable)/len(all_pairs), 4) if all_pairs else None,
            'excluded_from_enqueue_comparison':dict(excluded),
            'arrival_ms':quantiles(arrivals), 'long_host_gaps':total_long,
            'long_gaps_per_minute':per_minute(total_long), 'repeated_gap_clusters':len(clusters),
            'clusters_per_minute':per_minute(len(clusters)), 'cluster_spacing_s':quantiles(periods),
            'long_gaps_with_comparable_enqueue':len(long), 'enqueue_ms_at_long_gap':quantiles([p[3] for p in long]),
            'long_host_gaps_enqueue_at_most_20ms':sum(p[3] <= 20 for p in long),
            'long_host_gaps_pressure':sum(bool(p[1][5] & 1) for p in long),
            'long_host_gaps_rejection':sum(bool(p[1][5] & 2) for p in long),
            'queue_on_long_gap_start_report':r50_queue(long, 0),
            'queue_on_long_gap_end_report':r50_queue(long, 1),
            'queue_on_regular_gap_end_report':r50_queue(short, 1)}


def analyze_r50(reports):
    stamps = [r for r in reports if r is not None]
    if len(stamps) < 3:
        raise ValueError('Not enough marked r50 reports')
    boundaries = []
    for a, b in zip(stamps, stamps[1:]):
        if b[0] < a[0]:
            raise ValueError('Out-of-order report times: use one capture from one Android clock')
        if a[4] >> 6 != b[4] >> 6:
            boundaries.append((a[0]-SWITCH_MARGIN, b[0]+SWITCH_MARGIN))
    starts = [lo for lo, _ in boundaries]
    skipped = Counter()
    runs = []
    run = []
    for r in reports:
        reason = None
        if r is None:
            reason = 'unmarked'
        else:
            boundary = bisect.bisect_right(starts, r[0])-1
            if boundary >= 0 and r[0] <= boundaries[boundary][1]:
                reason = 'transition_margin'
            elif r[4] >> 6 == 3:
                reason = 'reserved_phase'
        if reason:
            skipped[reason] += 1
            if run:
                runs.append(run)
                run = []
        else:
            run.append(r)
    if run:
        runs.append(run)
    summaries = {name:r50_phase_summary(runs, phase) for phase, name in enumerate(PHASES[:3])}
    before, after = summaries['before_request'], summaries['after_ack']
    reasons = []
    observed_order = [phase for phase, _ in itertools.groupby(r[4] >> 6 for r in stamps)]
    if observed_order != [0, 1]:
        reasons.append('Trace must contain one baseline followed by one acknowledged intervention')
    for name, section in [('before_request', before), ('after_ack', after)]:
        if section['covered_s'] < 10:
            reasons.append(name + ': fewer than 10 observed seconds')
        if (section['comparable_fraction'] or 0) < .9:
            reasons.append(name + ': fewer than 90% comparable enqueue pairs')
        if (section['coverage_fraction'] or 0) < .9:
            reasons.append(name + ': less than 90% of observed phase span has marked adjacent frames')
    if before['clock_period_ms'] and after['clock_period_ms'] and set(before['clock_period_ms']) != set(after['clock_period_ms']):
        reasons.append('Different recorded scheduler periods')
    if before['source_counts'] and after['source_counts']:
        source_diff = sum(abs(before['source_counts'].get(s,0)/before['marked_reports']
                              - after['source_counts'].get(s,0)/after['marked_reports'])
                          for s in before['source_counts'].keys() | after['source_counts'].keys()) / 2
        if source_diff > .25:
            reasons.append('Pointer-source proportions differ by more than 25 percentage points')
    move_a, move_b = before['moving_xy_fraction'], after['moving_xy_fraction']
    if move_a is not None and move_b is not None and abs(move_a-move_b) > .25:
        reasons.append('Moving-frame proportions differ by more than 25 percentage points')
    return {'format':'r50_explicit', 'phase_markers':dict(Counter(PHASES[r[4] >> 6] for r in stamps)),
            'excluded_reports':dict(skipped), 'transition_margin_s':SWITCH_MARGIN,
            'transition_intervals_s':boundaries, 'phases':summaries,
            'acknowledged_comparison':{'sufficient_coverage_and_comparability':not reasons, 'reasons':reasons,
                   'baseline_repeated_clusters_present':before['repeated_gap_clusters'] >= 2},
            'queue_note':'Pre-scan occupancy of the shared MCU-to-radio IPC ring before the current HID enqueue. Codes 0..62 mean floor(pending bytes / 4); zero may contain 0..3 bytes. Code 63 means >=252 bytes or unavailable. Neither RF transmit time nor host queue depth is measured.',
            'rate_note':'Rates use seconds observed between adjacent marked reports in the same phase. They exclude unmarked and quarantined intervals. Long host gaps include unresolved local stamps. A failed request is reported separately.'}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--r50',action='store_true', help='Decode diagnostic phase and pending queue on r50 firmware')
    args=parser.parse_args()
    reports=read_reports(args.trace, include_motion=args.r50)
    result=json.dumps(analyze_r50(reports) if args.r50 else analyze(reports),indent=2)
    if args.output:args.output.write_text(result+'\n',encoding='utf-8')
    print(result)

