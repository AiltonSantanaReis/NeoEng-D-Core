from pathlib import Path
import argparse, hashlib, sys
root = Path(__file__).resolve().parents[1]
manifest = root / 'MANIFEST.sha256'
exclude_parts = {'build', '.deps', '.git'}
exclude_files = {'MANIFEST.sha256'}
def rows():
    out=[]
    for p in sorted(root.rglob('*')):
        if not p.is_file() or any(part in exclude_parts for part in p.relative_to(root).parts) or p.name in exclude_files:
            continue
        rel=p.relative_to(root).as_posix()
        out.append(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {rel}")
    return out
parser=argparse.ArgumentParser()
parser.add_argument('--check', action='store_true')
args=parser.parse_args()
content='\n'.join(rows())+'\n'
if args.check:
    if not manifest.exists() or manifest.read_text(encoding='utf-8') != content:
        print('MANIFEST.sha256 ausente ou divergente')
        sys.exit(1)
    print('OK: MANIFEST.sha256 confere')
else:
    manifest.write_text(content, encoding='utf-8')
    print(f'gravado: {manifest}')
