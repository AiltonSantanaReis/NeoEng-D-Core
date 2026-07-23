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
authorized_path=root/'audit/AUTHORIZED_SOURCE_MODIFICATIONS.json'
authorized_entries=[]
if authorized_path.is_file():
    authorized_document=json.loads(authorized_path.read_text(encoding='utf-8'))
    authorized_entries=authorized_document.get('entries', [])
authorized={row.get('path'): row for row in authorized_entries}
exact_source_matches=0
authorized_source_modifications=0
expected_paths={row['path'] for row in expected}
for row in expected:
    p=root/row['path']
    if not p.is_file():
        errors.append(f"ausente: {row['path']}")
        continue
    actual=sha(p)
    if actual==row['source_sha256']:
        exact_source_matches+=1
        if row['path'] in authorized:
            errors.append(f"exceção obsoleta para arquivo idêntico à origem: {row['path']}")
        continue
    exception=authorized.get(row['path'])
    if exception is None:
        errors.append(f"hash divergente da origem sem autorização: {row['path']}")
        continue
    if exception.get('original_source_sha256')!=row['source_sha256']:
        errors.append(f"hash original incorreto na autorização: {row['path']}")
    if exception.get('current_sha256')!=actual:
        errors.append(f"hash atual incorreto na autorização: {row['path']}")
    if not exception.get('authorized_changeset') or not exception.get('reason'):
        errors.append(f"autorização incompleta: {row['path']}")
    authorized_source_modifications+=1
for path in authorized:
    if path not in expected_paths:
        errors.append(f"autorização não corresponde ao corpus obrigatório: {path}")
# The canonical core remains free of render/Year-2 implementation details.
# The only active exception is the read-only companion under modules/view_lab,
# which must depend on neoeng_dcore and must never be referenced by the core target.
for rel in ('include','src','apps','tests'):
    p=root/rel
    for item in p.rglob('*'):
        if not item.is_file(): continue
        low=item.relative_to(root).as_posix().lower()
        if '/render/' in '/'+low or Path(low).name.startswith('y2_'):
            errors.append(f'arquivo Y2/render dentro do núcleo canônico: {low}')
        if item.suffix.lower() in {'.cpp','.hpp','.h','.cmake','.txt'}:
            text=item.read_text(encoding='utf-8',errors='replace').lower()
            for token in ('neoeng/render','neoeng_render','neoeng_y2','y2_o1_'):
                if token in text: errors.append(f"referência render/Y2 proibida no núcleo {token!r}: {low}")

ledger_path=root/'audit/YEAR2_EXTRACTION_LEDGER.json'
if not ledger_path.is_file():
    errors.append('ledger de extração modular do Ano 2 ausente')
else:
    ledger=json.loads(ledger_path.read_text(encoding='utf-8'))
    for row in ledger.get('active_imports', []):
        current=root/row['current_path']
        if not current.is_file():
            errors.append(f"fonte modular extraída ausente: {row['current_path']}")
            continue
        actual=sha(current)
        if actual!=row.get('current_sha256') or actual!=row.get('source_sha256'):
            errors.append(f"fonte modular divergente da origem: {row['current_path']}")
        if row.get('byte_for_byte') is not True:
            errors.append(f"fonte modular não marcada como byte-for-byte: {row['current_path']}")
    for row in ledger.get('selected_evidence', []):
        current=root/row['current_path']
        if not current.is_file():
            errors.append(f"evidência Y2 selecionada ausente: {row['current_path']}")
        elif sha(current)!=row.get('sha256'):
            errors.append(f"evidência Y2 selecionada divergente: {row['current_path']}")

view_cmake_path=root/'modules/view_lab/CMakeLists.txt'
if not view_cmake_path.is_file():
    errors.append('módulo opcional modules/view_lab ausente')
else:
    view_cmake=view_cmake_path.read_text(encoding='utf-8')
    if 'target_link_libraries(neoeng_dcore_view_lab' not in view_cmake or 'neoeng_dcore' not in view_cmake:
        errors.append('dependência view_lab -> neoeng_dcore não declarada')
    for source_root in ('modules/view_lab/src','modules/view_lab/apps','modules/view_lab/tests','modules/view_lab/vendor/year2/src'):
        for item in sorted((root/source_root).glob('*.cpp')):
            rel=item.relative_to(root/'modules/view_lab').as_posix()
            if rel not in view_cmake:
                errors.append(f'fonte do view lab sem cobertura CMake: {item.relative_to(root).as_posix()}')

cmake_top=(root/'CMakeLists.txt').read_text(encoding='utf-8')
core_block=cmake_top.split('add_library(neoeng_dcore ${NEOENG_DCORE_CORE_SOURCES})',1)[0]
for forbidden in ('modules/view_lab','neoeng_dcore_view_lab','neoeng_dcore_view_reference','neoeng/render'):
    if forbidden in core_block:
        errors.append(f'dependência reversa do núcleo para o view lab: {forbidden}')
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


# ChangeSet 004 cryptographic-evidence boundary and identity checks.
cs004_required = [
    'include/neoeng/core/crypto_hash.hpp',
    'src/crypto_hash.cpp',
    'include/neoeng/core/state_evidence.hpp',
    'src/state_evidence.cpp',
    'tests/state_evidence_tests.cpp',
    'apps/state_evidence_probe.cpp',
    'apps/state_evidence_fuzz.cpp',
    'docs/contracts/STATE_EVIDENCE_V1.md',
    'docs/architecture/STATE_EVIDENCE_BOUNDARY.md',
    'docs/changesets/004/CHANGESET.md',
    'docs/changesets/004/TEST_STATUS.md',
]
for rel in cs004_required:
    if not (root/rel).is_file():
        errors.append(f'arquivo obrigatório do ChangeSet 004 ausente: {rel}')
if 'project(NeoEngDCore VERSION 1.4.0 ' not in cmake:
    errors.append('versão CMake divergente: esperado NeoEng D-Core 1.4.0')
for rel in ('src/crypto_hash.cpp', 'src/state_evidence.cpp', 'apps/state_evidence_probe.cpp', 'apps/state_evidence_fuzz.cpp'):
    if rel not in cmake:
        errors.append(f'fonte CS004 sem cobertura CMake: {rel}')
state_contract = root/'docs/contracts/STATE_EVIDENCE_V1.md'
state_source = root/'src/state_evidence.cpp'
for item in (state_contract, state_source):
    if item.is_file() and 'neoeng.dcore.state-evidence-chain.v1' not in item.read_text(encoding='utf-8', errors='replace'):
        errors.append(f'schema de evidência obrigatório ausente: {item.relative_to(root).as_posix()}')
network_security = root/'src/network_security.cpp'
if network_security.is_file():
    network_text = network_security.read_text(encoding='utf-8', errors='replace')
    if '#include "neoeng/core/crypto_hash.hpp"' not in network_text:
        errors.append('network_security.cpp não reutiliza o SHA-256 compartilhado')
    if 'class Sha256' in network_text or 'struct Sha256' in network_text:
        errors.append('implementação SHA-256 duplicada permaneceu em network_security.cpp')
for rel in ('include/neoeng/core/state_evidence.hpp', 'src/state_evidence.cpp'):
    item=root/rel
    if item.is_file():
        text=item.read_text(encoding='utf-8',errors='replace').lower()
        for forbidden in ('neoeng/render', 'neoeng_dcore_view_lab', 'modules/view_lab'):
            if forbidden in text:
                errors.append(f'dependência visual proibida na evidência canônica: {rel}: {forbidden}')

if errors:
    print('\n'.join(errors)); sys.exit(1)
print(f"OK: {len(expected)} arquivos obrigatórios cobertos ({exact_source_matches} idênticos à origem; {authorized_source_modifications} modificações autorizadas e hashadas); núcleo NeoEng D-Core sem dependência reversa de render; View Lab opcional e fontes Y2 selecionadas verificadas por hash.")
