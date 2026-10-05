"""Create a local, zoomable sampled-CPU flame graph from export-cpu-stacks.

Use the image rundown from the same ETL. Optional labels are verified Ghidra
address ranges, not nearest-symbol guesses. Width is matched sample count,
not elapsed frame time. Raw addresses remain available for further analysis.
"""
import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path


def build(folded, images, pid, ranges):
    modules = set()
    with images.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.reader(stream, skipinitialspace=True):
            if len(row) < 12 or row[2].strip() != "Image":
                continue
            owner = re.search(r"\(\s*(\d+)\)\s*$", row[4])
            if not owner or int(owner[1]) not in (pid, 0):
                continue
            base, end = int(row[5], 16), int(row[6], 16)
            if int(owner[1]) == 0 and base < 0xFFFF000000000000:
                continue
            name = row[10].replace("\\", "/").rsplit("/", 1)[-1]
            modules.add((base, end, name))

    def label(ip):
        matches = [(base, name) for base, end, name in modules if base <= ip < end]
        if len(matches) != 1:
            return f"unmapped@{ip:016X}"
        base, name = matches[0]
        rva = ip - base
        if name.lower() == "gtaiv.exe":
            va = rva + 0x400000
            for item in ranges:
                if int(item["begin"], 16) <= va < int(item["end"], 16):
                    return f'{item["name"]} [{item["begin"]}]'
            return f"GTAIV@{va:08X}"
        return f"{name}+0x{rva:X}"

    root = {"name": "matched CPU samples", "value": 0, "children": {}}
    inclusive, exclusive = Counter(), Counter()
    for line in folded.read_text(encoding="utf-8-sig").splitlines():
        addresses, count = line.rsplit(" ", 1)
        count = int(count)
        if count <= 0:
            raise ValueError("Non-positive sample count")
        names = [label(int(ip, 16)) for ip in addresses.split(";")]
        # Consecutive IPs inside the same verified function are one frame.
        names = [name for i, name in enumerate(names) if i == 0 or name != names[i-1]]
        inclusive.update({name: count for name in set(names)})
        exclusive[names[-1]] += count
        node = root
        node["value"] += count
        for name in names:
            node = node["children"].setdefault(name, {"name": name, "value": 0, "children": {}})
            node["value"] += count

    def compact(node):
        node["children"] = [compact(child) for child in sorted(node["children"].values(), key=lambda c: -c["value"])]
        return node

    stats = {"samples": root["value"], "inclusive": inclusive.most_common(), "exclusive": exclusive.most_common()}
    return compact(root), stats


PAGE = '''<!doctype html><meta charset="utf-8"><title>Sampled CPU flame graph</title>
<style>body{font:15px system-ui;background:#10151e;color:#e8eef6;margin:24px}button,input{padding:8px}svg{width:100%;background:#172030}text{font:12px monospace;pointer-events:none}rect{cursor:pointer;stroke:#10151e;stroke-width:.5}table{border-collapse:collapse}td,th{padding:6px;text-align:left;border-bottom:1px solid #344151}#tip{min-height:45px}</style>
<h1>Sampled render-thread CPU</h1><p id="meta"></p>
<p>Width = matched CPU samples, not milliseconds or frame latency. Bottom = outer callers; top = sampled leaf.
Click a frame to zoom. Labels describe only verified ranges; GTAIV@ addresses are Ghidra VAs.
Partial/unwound stacks and samples without matching stack records limit coverage. No nearest-function guesses.</p>
<button id="reset">Reset zoom</button> <input id="search" placeholder="Highlight name or address"><p id="tip"></p><svg id="graph"></svg>
<h2>Hot functions and addresses</h2><p>Inclusive includes descendants and cannot be added across nested functions. Exclusive counts sampled leaves only.</p><table id="stats"></table>
<script>
const tree=__TREE__, stats=__STATS__, source=__SOURCE__;
document.querySelector('#meta').textContent=source+' — '+stats.samples.toLocaleString()+' matched samples';
const svg=document.querySelector('svg'),tip=document.querySelector('#tip'); let view=tree;
function color(s){let h=0;for(let c of s)h=(h*31+c.charCodeAt(0))>>>0;return `hsl(${15+h%45} 72% ${48+h%15}%)`}
function render(){svg.replaceChildren();let nodes=[],max=0;function walk(n,x,w,d){nodes.push({n,x,w,d});max=Math.max(max,d);let at=x;for(let c of n.children){let cw=w*c.value/n.value;walk(c,at,cw,d+1);at+=cw}}walk(view,0,1200,0);svg.setAttribute('viewBox',`0 0 1200 ${(max+1)*24}`);
let q=document.querySelector('input').value.toLowerCase();for(let {n,x,w,d} of nodes){if(w<.3)continue;let y=(max-d)*24,g=document.createElementNS('http://www.w3.org/2000/svg','g'),r=document.createElementNS(g.namespaceURI,'rect');for(let [k,v] of Object.entries({x,y,width:w,height:23,fill:q&&n.name.toLowerCase().includes(q)?'#d87bff':color(n.name)}))r.setAttribute(k,v);g.append(r);if(w>35){let t=document.createElementNS(g.namespaceURI,'text');t.setAttribute('x',x+3);t.setAttribute('y',y+16);t.textContent=n.name.slice(0,Math.floor(w/7));g.append(t)}g.onmouseenter=()=>tip.textContent=n.name+' — '+n.value+' samples ('+(100*n.value/tree.value).toFixed(2)+'% of all matched samples)';g.onclick=()=>{view=n;render()};svg.append(g)}}
document.querySelector('#reset').onclick=()=>{view=tree;render()};document.querySelector('input').oninput=render;render();
let table=document.querySelector('#stats'),head=table.insertRow();for(let name of ['Function/range/address','Inclusive samples','Exclusive samples','Exclusive %'])head.insertCell().textContent=name;
let inc=new Map(stats.inclusive);for(let [name,hits] of stats.exclusive.slice(0,60)){let row=table.insertRow();for(let value of [name,inc.get(name),hits,(100*hits/stats.samples).toFixed(2)])row.insertCell().textContent=value}
</script>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folded", type=Path)
    parser.add_argument("images", type=Path)
    parser.add_argument("pid", type=int)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ranges", type=Path)
    args = parser.parse_args()
    ranges = json.loads(args.ranges.read_text(encoding="utf-8-sig")) if args.ranges else []
    tree, stats = build(args.folded, args.images, args.pid, ranges)
    page = PAGE.replace("__TREE__", json.dumps(tree).replace("<", "\\u003c"))
    page = page.replace("__STATS__", json.dumps(stats).replace("<", "\\u003c"))
    page = page.replace("__SOURCE__", json.dumps(str(args.folded)).replace("<", "\\u003c"))
    args.output.write_text(page, encoding="utf-8")
    args.output.with_suffix(".json").write_text(json.dumps(stats, indent=2), encoding="utf-8")
    print(f"{stats['samples']} matched samples; wrote {args.output}")
    for name, hits in stats["exclusive"][:15]:
        print(f"{hits:6} {100*hits/stats['samples']:6.2f}% {name}")


if __name__ == "__main__":
    main()
