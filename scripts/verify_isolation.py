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
if 'project(NeoEngDCore VERSION 1.7.0 ' not in cmake:
    errors.append('versão CMake divergente: esperado NeoEng D-Core 1.7.0')
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


# ChangeSet 005 observability/support-bundle boundary and deferred validation gates.
cs005_required = [
    'include/neoeng/core/diagnostics.hpp',
    'src/diagnostics.cpp',
    'include/neoeng/core/support_bundle.hpp',
    'src/support_bundle.cpp',
    'tests/observability_support_tests.cpp',
    'apps/support_bundle_probe.cpp',
    'apps/support_bundle_fuzz.cpp',
    'scripts/verify_support_bundle.py',
    'scripts/windows/collect-support-bundle.ps1',
    'audit/DEFERRED_VALIDATION_GATES.json',
    'config/support_bundle_policy.json',
    'docs/contracts/OBSERVABILITY_V2.md',
    'docs/contracts/SUPPORT_BUNDLE_V1.md',
    'docs/architecture/OBSERVABILITY_SUPPORT_BOUNDARY.md',
    'docs/changesets/005/CHANGESET.md',
    'docs/changesets/005/TEST_STATUS.md',
]
for rel in cs005_required:
    if not (root/rel).is_file():
        errors.append(f'arquivo obrigatório do ChangeSet 005 ausente: {rel}')
for rel in ('src/diagnostics.cpp', 'src/support_bundle.cpp',
            'apps/support_bundle_probe.cpp', 'apps/support_bundle_fuzz.cpp'):
    if rel not in cmake:
        errors.append(f'fonte CS005 sem cobertura CMake: {rel}')
gates_path = root/'audit/DEFERRED_VALIDATION_GATES.json'
if gates_path.is_file():
    try:
        gates = json.loads(gates_path.read_text(encoding='utf-8'))
        if gates.get('schema') != 'neoeng.dcore.deferred-validation-gates.v1':
            errors.append('schema do ledger de validações diferidas divergente')
        gate_rows = gates.get('gates', [])
        if not gate_rows:
            errors.append('ledger de validações diferidas vazio')
        for row in gate_rows:
            if row.get('blocking_for_current_changeset') is not False:
                errors.append(f"gate diferido bloqueia indevidamente o CS005: {row.get('gate_id')}")
            if row.get('category') == 'native_validation_pending'                     and row.get('blocking_for_profile_qualification') is not True:
                errors.append(f"gate nativo não bloqueia qualificação: {row.get('gate_id')}")
    except Exception as exc:
        errors.append(f'ledger de validações diferidas inválido: {exc}')
support_contract = root/'docs/contracts/SUPPORT_BUNDLE_V1.md'
if support_contract.is_file() and 'neoeng.dcore.support-bundle.v1' not in support_contract.read_text(encoding='utf-8'):
    errors.append('schema de support bundle ausente no contrato')
policy_path = root/'config/support_bundle_policy.json'
if policy_path.is_file():
    try:
        policy = json.loads(policy_path.read_text(encoding='utf-8'))
        if policy.get('time_travel_payload_requires_explicit_authorization') is not True:
            errors.append('política de support bundle não exige autorização explícita para time-travel')
    except Exception as exc:
        errors.append(f'política de support bundle inválida: {exc}')
for rel in ('include/neoeng/core/diagnostics.hpp', 'include/neoeng/core/support_bundle.hpp',
            'src/diagnostics.cpp', 'src/support_bundle.cpp'):
    item = root/rel
    if item.is_file():
        text = item.read_text(encoding='utf-8', errors='replace').lower()
        for forbidden in ('neoeng/render', 'neoeng_dcore_view_lab', 'modules/view_lab'):
            if forbidden in text:
                errors.append(f'dependência visual proibida no núcleo CS005: {rel}: {forbidden}')


# ChangeSet 006 strict P0-P4 qualification harness and architectural boundary.
cs006_required = [
    'include/neoeng/core/hardware_profile.hpp',
    'src/hardware_profile.cpp',
    'apps/hardware_profile_probe.cpp',
    'apps/ecs_maintenance_benchmark.cpp',
    'tests/hardware_qualification_tests.cpp',
    'config/hardware_profiles.template.json',
    'config/qualification_campaign.template.json',
    'config/qualification_workloads.v1.json',
    'scripts/qualification/run_qualification_campaign.py',
    'scripts/qualification/verify_qualification_campaign.py',
    'scripts/qualification/compare_architectures.py',
    'scripts/run_hardware_qualification.sh',
    'scripts/windows/qualify-hardware-profile.ps1',
    'scripts/windows/verify-hardware-qualification.ps1',
    'docs/contracts/HARDWARE_QUALIFICATION_V2.md',
    'docs/architecture/QUALIFICATION_BOUNDARY.md',
    'docs/changesets/006/CHANGESET.md',
    'docs/changesets/006/PLAN_ADDENDUM.md',
    'docs/changesets/006/TEST_STATUS.md',
    'docs/changesets/006/DEFERRED_NATIVE_CAMPAIGNS.md',
]
for rel in cs006_required:
    if not (root/rel).is_file():
        errors.append(f'arquivo obrigatório do ChangeSet 006 ausente: {rel}')
for rel in ('apps/hardware_profile_probe.cpp', 'apps/ecs_maintenance_benchmark.cpp'):
    if rel not in cmake:
        errors.append(f'fonte CS006 sem cobertura CMake: {rel}')
if 'tests/hardware_qualification_tests.cpp' not in cmake:
    errors.append('teste CS006 sem cobertura CMake: tests/hardware_qualification_tests.cpp')

hardware_contract = root/'docs/contracts/HARDWARE_QUALIFICATION_V2.md'
if hardware_contract.is_file():
    contract_text = hardware_contract.read_text(encoding='utf-8', errors='replace')
    if 'neoeng.dcore.hardware-qualification.v2' not in contract_text:
        errors.append('schema de qualificação v2 ausente no contrato')
    for required_phrase in ('native_physical', 'engineering_baseline', 'No profile was qualified'):
        # The last phrase lives in CHANGESET.md; checked separately below.
        if required_phrase != 'No profile was qualified' and required_phrase not in contract_text:
            errors.append(f'regra obrigatória ausente no contrato de qualificação: {required_phrase}')
changeset006 = root/'docs/changesets/006/CHANGESET.md'
if changeset006.is_file() and 'No profile was qualified in this ChangeSet.' not in changeset006.read_text(encoding='utf-8', errors='replace'):
    errors.append('ChangeSet 006 não declara explicitamente que nenhum perfil foi qualificado')

profile_registry_path = root/'config/hardware_profiles.template.json'
if profile_registry_path.is_file():
    try:
        registry = json.loads(profile_registry_path.read_text(encoding='utf-8'))
        if registry.get('schema') != 'neoeng.dcore.hardware-profile-registry.v2':
            errors.append('schema do registro P0-P4 divergente')
        if registry.get('project_version') != '1.7.0':
            errors.append('versão do registro P0-P4 divergente')
        profiles = {row.get('profile'): row for row in registry.get('profiles', [])}
        if set(profiles) != {'P0', 'P1', 'P2', 'P3', 'P4'}:
            errors.append('registro de perfis não contém exatamente P0-P4')
        for profile_id, row in profiles.items():
            if row.get('status') != 'UNQUALIFIED':
                errors.append(f'perfil pré-qualificado indevidamente no template: {profile_id}')
        p1_req = profiles.get('P1', {}).get('requirements', {})
        if p1_req.get('rollback_p99_limit_ns') != 2000000 or p1_req.get('ecs_maintenance_p99_limit_ns') != 100000:
            errors.append('budgets P1 divergentes do plano')
        if p1_req.get('minimum_rollback_samples') != 1000 or p1_req.get('minimum_ecs_samples') != 1000:
            errors.append('mínimo de amostras P1 divergente')
        for profile_id in ('P2', 'P4'):
            if profiles.get(profile_id, {}).get('requirements', {}).get('performance_limits') != 'not_inherited_from_P1':
                errors.append(f'{profile_id} herda ou omite indevidamente a separação dos budgets P1')
    except Exception as exc:
        errors.append(f'registro de perfis inválido: {exc}')

request_template_path = root/'config/qualification_campaign.template.json'
if request_template_path.is_file():
    try:
        request_template = json.loads(request_template_path.read_text(encoding='utf-8'))
        if request_template.get('schema') != 'neoeng.dcore.qualification-campaign-request.v1':
            errors.append('schema do request de campanha divergente')
        if request_template.get('project_version') != '1.7.0':
            errors.append('versão do request de campanha divergente')
    except Exception as exc:
        errors.append(f'template de campanha inválido: {exc}')

workload_registry_path = root/'config/qualification_workloads.v1.json'
if workload_registry_path.is_file():
    try:
        workloads_doc = json.loads(workload_registry_path.read_text(encoding='utf-8'))
        if workloads_doc.get('schema') != 'neoeng.dcore.qualification-workload-registry.v1':
            errors.append('schema do registro de workloads divergente')
        ids = {row.get('workload_id') for row in workloads_doc.get('workloads', [])}
        required_ids = {'Y1-O2-SPARSE-COMPONENT-MAINTENANCE-V1', 'Y1-O3-CANONICAL-ROLLBACK-8-V1',
                        'ECS-B01', 'ECS-B02', 'ECS-B03', 'ECS-B04', 'ECS-B05'}
        if not required_ids.issubset(ids):
            errors.append('registro de workloads omite objetivos obrigatórios Y1-O2/Y1-O3/ECS-B01-B05')
    except Exception as exc:
        errors.append(f'registro de workloads inválido: {exc}')

if gates_path.is_file():
    try:
        gates = json.loads(gates_path.read_text(encoding='utf-8'))
        if gates.get('project_version') != '1.7.0':
            errors.append('versão do ledger diferido divergente para CS006')
        rows = {row.get('gate_id'): row for row in gates.get('gates', [])}
        for gate_id in ('PROFILE-P0-001', 'PROFILE-P1-NVIDIA-001', 'PROFILE-P2-AMD-001',
                        'PROFILE-P3-ARM64-001', 'PROFILE-P4-8GB-001', 'ECS-SCOPE-COMPLETE-001'):
            if gate_id not in rows:
                errors.append(f'gate obrigatório CS006 ausente: {gate_id}')
        ecs_gap = rows.get('ECS-SCOPE-COMPLETE-001', {})
        if ecs_gap.get('category') != 'implementation_gap' or ecs_gap.get('blocking_for_profile_qualification') is not True:
            errors.append('lacuna de escopo ECS não bloqueia formalmente a qualificação P1')
    except Exception as exc:
        errors.append(f'ledger diferido CS006 inválido: {exc}')

for rel in ('include/neoeng/core/hardware_profile.hpp', 'src/hardware_profile.cpp',
            'apps/hardware_profile_probe.cpp', 'apps/ecs_maintenance_benchmark.cpp'):
    item = root/rel
    if item.is_file():
        text = item.read_text(encoding='utf-8', errors='replace').lower()
        for forbidden in ('neoeng/render', 'neoeng_dcore_view_lab', 'modules/view_lab'):
            if forbidden in text:
                errors.append(f'dependência visual proibida no núcleo/harness CS006: {rel}: {forbidden}')

hardware_header = root/'include/neoeng/core/hardware_profile.hpp'
hardware_source = root/'src/hardware_profile.cpp'
if hardware_header.is_file():
    header_text = hardware_header.read_text(encoding='utf-8', errors='replace')
    for token in ('ExecutionEnvironmentKind', 'QualificationEvidenceDisposition',
                  'NativeExecutionRequired', 'P4EightGbCompatibility', 'EcsScopeIncomplete'):
        if token not in header_text:
            errors.append(f'contrato CS006 incompleto; token ausente: {token}')
if hardware_source.is_file():
    source_text = hardware_source.read_text(encoding='utf-8', errors='replace')
    if 'ExecutionEnvironmentKind::NativePhysical' not in source_text:
        errors.append('avaliador CS006 não exige explicitamente execução nativa física')
    if 'QualificationEvidenceDisposition::EngineeringBaseline' not in source_text:
        errors.append('avaliador CS006 não distingue baseline de engenharia')

# ChangeSet 007 host integration boundary, install package and ABI governance.
cs007_required = [
    'modules/host_sdk/CMakeLists.txt',
    'modules/host_sdk/include/neoeng/dcore_host.h',
    'modules/host_sdk/src/dcore_host.cpp',
    'modules/host_sdk/tests/host_sdk_tests.cpp',
    'modules/host_sdk/tests/host_sdk_header_c.c',
    'modules/host_sdk/apps/host_sdk_reference.c',
    'cmake/NeoEngDCoreConfig.cmake.in',
    'tests/cmake/run_host_sdk_install_consumer.cmake',
    'tests/cmake/host_sdk_consumer/CMakeLists.txt',
    'tests/cmake/host_sdk_consumer/main.c',
    'scripts/verify_host_sdk_boundary.py',
    'scripts/windows/install-host-sdk.ps1',
    'docs/contracts/HOST_SDK_C_ABI_V1.md',
    'docs/architecture/HOST_SDK_BOUNDARY.md',
    'docs/changesets/007/CHANGESET.md',
    'docs/changesets/007/EXISTING_CAPABILITY_RECONCILIATION.md',
    'docs/changesets/007/TEST_STATUS.md',
    'audit/HOST_SDK_GAP_ANALYSIS_1_6_0.json',
    'audit/CHANGESET_007_CORE_INVARIANT_LEDGER.json',
]
for rel in cs007_required:
    if not (root/rel).is_file():
        errors.append(f'arquivo obrigatório do ChangeSet 007 ausente: {rel}')
module_cmake_path = root/'modules/host_sdk/CMakeLists.txt'
if module_cmake_path.is_file():
    module_cmake = module_cmake_path.read_text(encoding='utf-8', errors='replace')
    for rel in ('src/dcore_host.cpp', 'tests/host_sdk_tests.cpp', 'tests/host_sdk_header_c.c',
                'apps/host_sdk_reference.c'):
        if rel not in module_cmake:
            errors.append(f'fonte Host SDK sem cobertura CMake: {rel}')
    if 'target_link_libraries(neoeng_dcore_host_sdk PUBLIC neoeng_dcore)' not in module_cmake:
        errors.append('dependência Host SDK -> D-Core ausente')
for rel in ('include/neoeng/core', 'src'):
    for item in (root/rel).glob('*'):
        if item.is_file() and item.suffix.lower() in {'.hpp', '.h', '.cpp'}:
            text = item.read_text(encoding='utf-8', errors='replace')
            if 'neoeng/dcore_host.h' in text or 'neoeng_dcore_host_sdk' in text:
                errors.append(f'dependência reversa do núcleo para Host SDK: {item.relative_to(root)}')
host_contract = root/'docs/contracts/HOST_SDK_C_ABI_V1.md'
if host_contract.is_file():
    contract_text = host_contract.read_text(encoding='utf-8', errors='replace')
    for phrase in ('ABI major: 1', 'owned by the creating thread', 'not a hostile-network ingress',
                   'no shared-library distribution claim'):
        if phrase not in contract_text:
            errors.append(f'contrato Host SDK incompleto: {phrase}')

if errors:
    print('\n'.join(errors)); sys.exit(1)
print(f"OK: {len(expected)} arquivos obrigatórios cobertos ({exact_source_matches} idênticos à origem; {authorized_source_modifications} modificações autorizadas e hashadas); núcleo NeoEng D-Core sem dependência reversa de render/Host SDK; companions View Lab e Host SDK verificados.")
