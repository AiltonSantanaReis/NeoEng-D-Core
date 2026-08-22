#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, os, subprocess, urllib.request
from pathlib import Path

BASE="3640394d902c92f620b463f1542e14ab47a10959"
ATTEMPT1="71d016850099a3b7e4d5b994c867fe106df12e10"
CANDIDATE={
 ".github/workflows/cs000d-documentation-finalization-correction.yml",
 ".github/workflows/cs000e-governance-transition-reconciliation.yml",
 ".github/workflows/cs000e-r2-governance-transition-reconciliation.yml",
 "scripts/verify_governance_transition_reconciliation.py",
 "scripts/verify_governance_transition_reconciliation_r2.py",
 "audit/GOVERNANCE_TRANSITION_STATE.json",
 "audit/SOURCE_OF_TRUTH_INDEX.json",
 "audit/EVOLUTION_AMENDMENTS.json",
 "docs/governance/CHANGESET_VALIDATION_POLICY_ACTIVATION.md",
 "docs/changesets/000E/CHANGESET.md",
 "audit/validation/CS000E/VALIDATION_PLAN.json",
 "audit/validation/CS000E/ATTEMPT_001_NONACCEPTANCE.json",
 "audit/validation/CS000E/VALIDATION_PLAN_R2.json",
 "audit/CURRENT_CHANGESET_VALIDATION.json",
}
CLOSURE=CANDIDATE|{"audit/validation/CS000E/VALIDATION_RESULT_R2.json"}

def load(p): return json.loads(Path(p).read_text(encoding="utf-8"))
def git(*a): return subprocess.run(["git",*a],text=True,capture_output=True,check=False)
def show(commit,path):
 p=git("show",f"{commit}:{path}")
 if p.returncode: raise AssertionError(f"cannot read {path}@{commit}")
 return p.stdout
def api(path):
 repo=os.environ.get("GITHUB_REPOSITORY","AiltonSantanaReis/NeoEng-D-Core")
 h={"Accept":"application/vnd.github+json","User-Agent":"neoeng-cs000e-r2"}
 if os.environ.get("GITHUB_TOKEN"): h["Authorization"]=f"Bearer {os.environ['GITHUB_TOKEN']}"
 req=urllib.request.Request(f"https://api.github.com/repos/{repo}{path}",headers=h)
 with urllib.request.urlopen(req,timeout=20) as r: return json.loads(r.read().decode())

def on_keys(text):
 lines=text.splitlines(); i=lines.index("on:")+1; keys=set()
 for line in lines[i:]:
  if not line.strip() or line.lstrip().startswith("#"): continue
  if not line.startswith(" "): break
  if line.startswith("  ") and not line.startswith("    ") and line.strip().endswith(":"): keys.add(line.strip()[:-1])
 return keys
def suffix(text):
 marker="\npermissions:\n"; i=text.find(marker)
 assert i>=0
 return text[i+1:]

def check_core():
 s=load("audit/GOVERNANCE_TRANSITION_STATE.json")
 assert s["legacy_cs016e"]["status"]=="SUPERSEDED_UNACCEPTED"
 assert s["legacy_cs016e"]["accepted"] is False
 assert s["prospective_authority"]["regime_id"]=="CHANGESET_VALIDATION"
 src=load("audit/SOURCE_OF_TRUTH_INDEX.json")
 p=src["precedence"]; assert p.index("audit/GOVERNANCE_TRANSITION_STATE.json") < p.index("audit/GOVERNANCE_ROOT_OF_TRUST.json")
 am=load("audit/EVOLUTION_AMENDMENTS.json"); old=json.loads(show(BASE,"audit/EVOLUTION_AMENDMENTS.json"))
 assert am["amendments"][:4]==old["amendments"][:4]
 e=am["amendments"][4]; assert e["status"]=="superseded" and e["accepted_source_commit"] is None and e["evidence_manifest"] is None

def check_attempt1():
 r=load("audit/validation/CS000E/ATTEMPT_001_NONACCEPTANCE.json")
 assert r["source_sha"]==ATTEMPT1 and r["run_id"]==32544869579 and r["acceptance_decision"]=="NOT_ACCEPTED"
 assert r["rerun_attempt1"] is False and r["preserved_external_failure"]["run_id"]==32544869512
 a=api("/actions/runs/32544869579"); assert a["head_sha"]==ATTEMPT1 and a["conclusion"]=="success"
 f=api("/actions/runs/32544869512"); assert f["head_sha"]==ATTEMPT1 and f["conclusion"]=="failure"

def check_retirement():
 targets=[(".github/workflows/cs000d-documentation-finalization-correction.yml",BASE),(".github/workflows/cs000e-governance-transition-reconciliation.yml",ATTEMPT1)]
 for path,ref in targets:
  cur=Path(path).read_text(encoding="utf-8"); prior=show(ref,path)
  assert on_keys(cur)=={"workflow_dispatch"}, (path,on_keys(cur))
  assert cur.splitlines()[0]==prior.splitlines()[0]
  assert suffix(cur)==suffix(prior), f"body changed: {path}"

def check_live():
 expected={27:"d20a40d0cd0952d653965b34b67ec30e9ba1b42f",28:"ffe41397783d1550d39d7c1c0ea445e69d2750bd",29:"c7c608ba04edb74303a971bdcdf4ca60d1b44119",30:"d092ac56290d76dddf51982549a98234f038f3ee"}
 for n,s in expected.items():
  p=api(f"/pulls/{n}"); assert p.get("merged_at") and p["merge_commit_sha"]==s
 b=api("/branches/main"); assert b.get("protected") is True
 checks=b.get("protection",{}).get("required_status_checks",{}).get("checks",[])
 assert checks==[{"context":"Trusted ChangeSet validation gate","app_id":15368}], checks

def check_scope():
 p=git("diff","--name-only",f"{BASE}...HEAD"); assert p.returncode==0
 c={x for x in p.stdout.splitlines() if x}; assert c in (CANDIDATE,CLOSURE), sorted(c)
 d=load("audit/CURRENT_CHANGESET_VALIDATION.json")
 assert d["plan_path"]=="audit/validation/CS000E/VALIDATION_PLAN_R2.json"
 if c==CANDIDATE: assert "result_path" not in d
 else: assert d.get("result_path")=="audit/validation/CS000E/VALIDATION_RESULT_R2.json"

def check_non_effects():
 for p in ("audit/EVOLUTION_ROADMAP.json","audit/PRODUCT_CLAIMS_LEDGER.json","audit/RELEASE_ASSURANCE_POLICY.json","audit/FINAL_ACCEPTANCE_POLICY.json","audit/GOVERNANCE_ROOT_OF_TRUST.json","audit/GOVERNANCE_ACCEPTANCE_CHAIN.json"):
  assert git("diff","--quiet",BASE,"HEAD","--",p).returncode==0
 changed=CANDIDATE
 assert not any(x.startswith(("src/","include/","tests/","cmake/","apps/","modules/","tools/")) or x=="CMakeLists.txt" for x in changed)

def self_test():
 assert CANDIDATE < CLOSURE
 assert on_keys("name: x\non:\n  workflow_dispatch:\n\npermissions:\n  contents: read\n")=={"workflow_dispatch"}

CHECKS={"core":check_core,"attempt1":check_attempt1,"retirement":check_retirement,"live":check_live,"scope":check_scope,"non-effects":check_non_effects}
def main():
 a=argparse.ArgumentParser(); a.add_argument("--self-test",action="store_true"); a.add_argument("--check",choices=sorted(CHECKS)); x=a.parse_args()
 try:
  if x.self_test:self_test()
  elif x.check:CHECKS[x.check]()
  else:
   for f in CHECKS.values():f()
 except (AssertionError,KeyError,ValueError) as e:
  print(f"CS000E R2: REJECT — {x.check or 'all'} — {e}"); return 1
 print(f"CS000E R2: ACCEPT — {x.check or 'all'}"); return 0
if __name__=="__main__":raise SystemExit(main())
