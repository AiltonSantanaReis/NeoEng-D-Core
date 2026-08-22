#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, os, subprocess, urllib.request
from pathlib import Path

BASE = "3640394d902c92f620b463f1542e14ab47a10959"
CANDIDATE = {
    ".github/workflows/cs000e-governance-transition-reconciliation.yml",
    "scripts/verify_governance_transition_reconciliation.py",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/SOURCE_OF_TRUTH_INDEX.json",
    "audit/EVOLUTION_AMENDMENTS.json",
    "docs/governance/CHANGESET_VALIDATION_POLICY_ACTIVATION.md",
    "docs/changesets/000E/CHANGESET.md",
    "audit/validation/CS000E/VALIDATION_PLAN.json",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
}
CLOSURE = CANDIDATE | {"audit/validation/CS000E/VALIDATION_RESULT.json"}

def load(path): return json.loads(Path(path).read_text(encoding="utf-8"))
def git(*args): return subprocess.run(["git", *args], text=True, capture_output=True, check=False)
def prior_json(path):
    p=git("show", f"{BASE}:{path}")
    if p.returncode != 0: raise SystemExit(f"cannot read base file {path}")
    return json.loads(p.stdout)
def api(path):
    repo=os.environ.get("GITHUB_REPOSITORY","AiltonSantanaReis/NeoEng-D-Core")
    headers={"Accept":"application/vnd.github+json","User-Agent":"neoeng-cs000e"}
    if os.environ.get("GITHUB_TOKEN"): headers["Authorization"]=f"Bearer {os.environ['GITHUB_TOKEN']}"
    req=urllib.request.Request(f"https://api.github.com/repos/{repo}{path}",headers=headers)
    with urllib.request.urlopen(req,timeout=20) as r: return json.loads(r.read().decode("utf-8"))

def check_machine():
    s=load("audit/GOVERNANCE_TRANSITION_STATE.json")
    assert s["schema"]=="neoeng.dcore.governance-transition-state.v1" and s["base_sha"]==BASE
    assert s["prospective_authority"]["regime_id"]=="CHANGESET_VALIDATION"
    assert s["prospective_authority"]["required_branch_check"]=="Trusted ChangeSet validation gate"
    assert s["prospective_authority"]["required_branch_check_app_id"]==15368
    l=s["legacy_cs016e"]
    assert l["status"]=="SUPERSEDED_UNACCEPTED" and l["accepted"] is False
    assert l["accepted_source_commit"] is None and l["evidence_manifest"] is None
    assert l["may_be_reclassified_as_accepted"] is False and l["preserve_bytes"] is True

def check_source():
    s=load("audit/SOURCE_OF_TRUTH_INDEX.json"); p=s["precedence"]
    t=p.index("audit/GOVERNANCE_TRANSITION_STATE.json"); policy=p.index("audit/CHANGESET_VALIDATION_POLICY.json"); legacy=p.index("audit/GOVERNANCE_ROOT_OF_TRUST.json")
    assert t < legacy and policy < legacy
    r=s["active_evolution_program"]["prospective_governance_regime"]
    assert r["regime_id"]=="CHANGESET_VALIDATION" and r["required_branch_check"]=="Trusted ChangeSet validation gate" and r["required_branch_check_app_id"]==15368
    assert s["active_evolution_program"]["legacy_root_status"]=="CS016E_SUPERSEDED_UNACCEPTED_FOR_PROSPECTIVE_CHANGESET_AUTHORIZATION"

def check_cs016e():
    old=prior_json("audit/EVOLUTION_AMENDMENTS.json"); new=load("audit/EVOLUTION_AMENDMENTS.json")
    assert new["amendments"][:4] == old["amendments"][:4]
    olde=old["amendments"][4]; newe=new["amendments"][4]
    assert olde["changeset"]=="CS016E" and olde["status"]=="in_progress" and newe["status"]=="superseded"
    for k in ("title","required_before_stage","amendment_document","deviation_record","accepted_source_commit","evidence_manifest","validation_history"): assert newe[k] == olde[k]
    assert newe["accepted_source_commit"] is None and newe["evidence_manifest"] is None and newe["superseded_by"]=="CS000E"
    chain=prior_json("audit/GOVERNANCE_ACCEPTANCE_CHAIN.json"); assert chain["entries"][-1]["changeset"]=="CS016D"
    for path in ("audit/GOVERNANCE_ROOT_OF_TRUST.json","audit/GOVERNANCE_ACCEPTANCE_CHAIN.json"): assert git("diff","--quiet",BASE,"HEAD","--",path).returncode == 0

def check_live():
    expected={27:"d20a40d0cd0952d653965b34b67ec30e9ba1b42f",28:"ffe41397783d1550d39d7c1c0ea445e69d2750bd",29:"c7c608ba04edb74303a971bdcdf4ca60d1b44119",30:"d092ac56290d76dddf51982549a98234f038f3ee"}
    for num,sha in expected.items():
        pr=api(f"/pulls/{num}"); assert pr.get("merged_at") and pr.get("merge_commit_sha")==sha
    branch=api("/branches/main"); assert branch.get("protected") is True
    checks=branch.get("protection",{}).get("required_status_checks",{}).get("checks",[])
    assert checks == [{"context":"Trusted ChangeSet validation gate","app_id":15368}], checks

def check_scope():
    p=git("diff","--name-only",f"{BASE}...HEAD"); assert p.returncode==0
    changed={x for x in p.stdout.splitlines() if x}; assert changed in (CANDIDATE,CLOSURE), f"unexpected scope: {sorted(changed)}"
    d=load("audit/CURRENT_CHANGESET_VALIDATION.json"); assert d["plan_path"]=="audit/validation/CS000E/VALIDATION_PLAN.json"
    if changed==CANDIDATE: assert "result_path" not in d
    else: assert d.get("result_path")=="audit/validation/CS000E/VALIDATION_RESULT.json"

def check_non_effects():
    p=git("diff","--name-only",f"{BASE}...HEAD"); changed=[x for x in p.stdout.splitlines() if x]
    forbidden=("src/","include/","tests/","cmake/","apps/","modules/","tools/")
    assert not any(x.startswith(forbidden) or x=="CMakeLists.txt" for x in changed)
    for path in ("audit/EVOLUTION_ROADMAP.json","audit/PRODUCT_CLAIMS_LEDGER.json","audit/RELEASE_ASSURANCE_POLICY.json","audit/FINAL_ACCEPTANCE_POLICY.json"): assert git("diff","--quiet",BASE,"HEAD","--",path).returncode==0

def self_test():
    assert "Trusted ChangeSet validation gate" != "Trusted governance root gate"
    assert CANDIDATE < CLOSURE and len(CLOSURE-CANDIDATE)==1

CHECKS={"machine-state":check_machine,"source-of-truth":check_source,"cs016e":check_cs016e,"live-evidence":check_live,"scope":check_scope,"non-effects":check_non_effects}
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--self-test",action="store_true"); ap.add_argument("--check",choices=sorted(CHECKS)); a=ap.parse_args()
    try:
        if a.self_test: self_test()
        elif a.check: CHECKS[a.check]()
        else:
            for f in CHECKS.values(): f()
    except (AssertionError,KeyError,ValueError) as e:
        print(f"CS000E RECONCILIATION: REJECT — {a.check or 'all'} — {e}"); return 1
    print(f"CS000E RECONCILIATION: ACCEPT — {a.check or 'all'}"); return 0
if __name__=="__main__": raise SystemExit(main())
