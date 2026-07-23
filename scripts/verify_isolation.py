from pathlib import Path
import hashlib, json, re, sys
root=Path(__file__).resolve().parents[1]
errors=[]
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()
expected=json.loads((root/'audit/YEAR1_EXPECTED_SOURCE_FILES.json').read_text(encoding='utf-8'))
for row in expected:
    p=root/row['path']
    if not p.is_file(): errors.append(f"ausente: {row['path']}")
    elif sha(p)!=row['source_sha256']: errors.append(f"hash divergente da origem: {row['path']}")
for rel in ('include','src','apps','tests','CMakeLists.txt'):
    p=root/rel
    items=[p] if p.is_file() else list(p.rglob('*'))
    for item in items:
        if not item.is_file(): continue
        low=item.relative_to(root).as_posix().lower()
        if '/render/' in '/'+low or Path(low).name.startswith('y2_'):
            errors.append(f'arquivo Y2/render presente: {low}')
        if item.suffix.lower() in {'.cpp','.hpp','.h','.cmake','.txt'} or item.name=='CMakeLists.txt':
            text=item.read_text(encoding='utf-8',errors='replace').lower()
            for token in ('neoeng/render','neoeng_render','neoeng_y2','y2_o1_'):
                if token in text: errors.append(f"referência proibida {token!r}: {low}")
cmake=(root/'CMakeLists.txt').read_text(encoding='utf-8')
for app in sorted((root/'apps').glob('*.cpp')):
    rel=app.relative_to(root).as_posix()
    if rel not in cmake: errors.append(f'fonte app sem cobertura CMake: {rel}')
for src in sorted((root/'src').glob('*.cpp')):
    rel=src.relative_to(root).as_posix()
    if rel not in cmake: errors.append(f'fonte core sem cobertura CMake: {rel}')
required=[
 'docs/original/Plano_Deep_Tech_NeoEng_Ano1_Completo.pdf',
 'evidence/original-year1-artifacts',
 'docs/records/YEAR1_COMPLETION_DECISION_V0_29.md',
 'docs/records/YEAR1_FREEZE_CANDIDATE_CONTRACT_V0_28.md',
 'docs/records/YEAR1_PROVISIONAL_ACCEPTANCE_V0_28.md']
for rel in required:
    if not (root/rel).exists(): errors.append(f'evidência obrigatória ausente: {rel}')
if not any((root/'evidence/original-year1-artifacts').rglob('*')):
    errors.append('evidência original do Ano 1 vazia')

# Active project identity checks (constructed to avoid self-matching literal legacy names).
identity_files=[
 root/'CMakeLists.txt', root/'CMakePresets.json', root/'README.md', root/'RUN_WINDOWS.cmd',
 root/'SOURCE_PROVENANCE.json', root/'PACKAGE_VALIDATION.json', root/'vcpkg.json',
 root/'scripts/windows/build.ps1', root/'scripts/windows/run-dcore.ps1', root/'scripts/windows/README.md'
]
def _legacy(codepoints):
    return ''.join(chr(value) for value in codepoints)
legacy_tokens=[
    _legacy([78,101,111,69,110,103,89,101,97,114,49,67,111,114,101]),
    _legacy([78,101,111,69,110,103,58,58,89,101,97,114,49,67,111,114,101]),
    _legacy([110,101,111,101,110,103,45,121,101,97,114,49,45,99,111,114,101]),
    _legacy([78,101,111,69,110,103,45,89,101,97,114,49,45,67,111,114,101]),
    _legacy([78,101,111,69,110,103,32,89,101,97,114,32,49,32,67,111,114,101]),
    _legacy([114,117,110,45,121,101,97,114,49,46,112,115,49]),
    _legacy([78,69,79,69,78,71,95,89,49,95,66,85,73,76,68,95]),
]
for item in identity_files:
    if not item.is_file():
        errors.append(f'arquivo de identidade ausente: {item.relative_to(root).as_posix()}')
        continue
    text=item.read_text(encoding='utf-8',errors='replace')
    for token in legacy_tokens:
        if token in text:
            errors.append(f'identidade anterior presente em {item.relative_to(root).as_posix()}: {token}')
cmake_identity=(root/'CMakeLists.txt').read_text(encoding='utf-8',errors='replace')
for required_identity in ('project(NeoEngDCore ', 'add_library(neoeng_dcore ', 'add_library(NeoEng::DCore ALIAS neoeng_dcore)'):
    if required_identity not in cmake_identity:
        errors.append(f'identidade CMake obrigatória ausente: {required_identity}')
manifest_identity=json.loads((root/'vcpkg.json').read_text(encoding='utf-8'))
if manifest_identity.get('name')!='neoeng-d-core':
    errors.append('nome do manifesto vcpkg divergente: esperado neoeng-d-core')
if not (root/'scripts/windows/run-dcore.ps1').is_file():
    errors.append('runner Windows NeoEng D-Core ausente')
if (root/'scripts/windows'/(_legacy([114,117,110,45,121,101,97,114,49,46,112,115,49]))).exists():
    errors.append('runner Windows com identidade anterior ainda presente')

if errors:
    print('\n'.join(errors)); sys.exit(1)
print(f"OK: {len(expected)} arquivos obrigatórios conferem com a origem; identidade NeoEng D-Core validada; CMake cobre todas as fontes; sem dependências Y2/render.")
