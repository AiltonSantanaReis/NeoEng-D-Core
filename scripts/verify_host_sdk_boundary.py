from pathlib import Path
import hashlib
import json
import re
import sys

root = Path(__file__).resolve().parents[1]
errors = []


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


required = [
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
    'docs/contracts/HOST_SDK_C_ABI_V1.md',
    'docs/architecture/HOST_SDK_BOUNDARY.md',
    'docs/changesets/007/EXISTING_CAPABILITY_RECONCILIATION.md',
    'audit/HOST_SDK_GAP_ANALYSIS_1_6_0.json',
    'audit/CHANGESET_007_CORE_INVARIANT_LEDGER.json',
    'audit/CHANGESET_009_CORE_INVARIANT_LEDGER.json',
]
for rel in required:
    if not (root / rel).is_file():
        errors.append(f'arquivo obrigatório do Host SDK ausente: {rel}')

cmake_path = root / 'CMakeLists.txt'
cmake = cmake_path.read_text(encoding='utf-8', errors='replace')
if 'project(NeoEngDCore VERSION 1.11.0 LANGUAGES C CXX)' not in cmake:
    errors.append('identidade/versão CMake 1.11.0 com C e CXX ausente')
for token in (
    'add_subdirectory(modules/host_sdk)',
    'NeoEngDCoreConfig.cmake.in',
    'install(EXPORT NeoEngDCoreTargets',
    'neoeng_host_sdk_install_consumer',
):
    if token not in cmake:
        errors.append(f'cobertura CMake Host SDK ausente: {token}')

module_cmake_path = root / 'modules/host_sdk/CMakeLists.txt'
if module_cmake_path.is_file():
    module_cmake = module_cmake_path.read_text(encoding='utf-8', errors='replace')
    for token in (
        'add_library(neoeng_dcore_host_sdk STATIC',
        'add_library(NeoEng::DCoreHostSdk ALIAS neoeng_dcore_host_sdk)',
        'target_link_libraries(neoeng_dcore_host_sdk PUBLIC neoeng_dcore)',
    ):
        if token not in module_cmake:
            errors.append(f'declaração do companion Host SDK ausente: {token}')

# The canonical target must not consume the Host SDK.
core_prefix = cmake.split('add_library(neoeng_dcore ${NEOENG_DCORE_CORE_SOURCES})', 1)[0]
if 'modules/host_sdk' in core_prefix or 'dcore_host' in core_prefix:
    errors.append('Host SDK apareceu dentro da lista de fontes do núcleo')
for rel in ('include/neoeng/core', 'src'):
    for item in (root / rel).glob('*'):
        if not item.is_file() or item.suffix.lower() not in {'.hpp', '.h', '.cpp'}:
            continue
        text = item.read_text(encoding='utf-8', errors='replace')
        if 'neoeng/dcore_host.h' in text or 'neoeng_dcore_host_sdk' in text:
            errors.append(f'dependência reversa do núcleo para Host SDK: {item.relative_to(root)}')

header_path = root / 'modules/host_sdk/include/neoeng/dcore_host.h'
if header_path.is_file():
    header = header_path.read_text(encoding='utf-8', errors='replace')
    for token in ('namespace ', 'std::', 'template<', '#include <vector>', '#include <string>'):
        if token in header:
            errors.append(f'construção C++ proibida no header C: {token}')
    for token in (
        'NEOENG_DCORE_HOST_ABI_MAJOR UINT16_C(1)',
        'NEOENG_DCORE_RUNTIME_VERSION_MINOR UINT16_C(9)',
        'typedef struct neoeng_dcore_host neoeng_dcore_host;',
        'neoeng_dcore_host_correct_input_and_resimulate',
        'neoeng_dcore_host_acknowledge_recovery',
    ):
        if token not in header:
            errors.append(f'contrato C ABI incompleto: {token}')

# No vertical adapter or renderer dependency may be introduced in the companion.
for item in (root / 'modules/host_sdk').rglob('*'):
    if not item.is_file() or item.suffix.lower() not in {'.h', '.hpp', '.c', '.cpp', '.txt'}:
        continue
    text = item.read_text(encoding='utf-8', errors='replace').lower()
    for forbidden in (
        'unrealengine',
        'unityengine',
        'ros/ros.h',
        'rclcpp',
        'neoeng/render',
        'neoeng_dcore_view_lab',
        'modules/view_lab',
    ):
        if forbidden in text:
            errors.append(f'dependência vertical/render proibida no Host SDK: {forbidden}: {item.relative_to(root)}')

ledger_path = root / 'audit/CHANGESET_009_CORE_INVARIANT_LEDGER.json'
if ledger_path.is_file():
    try:
        ledger = json.loads(ledger_path.read_text(encoding='utf-8'))
        if ledger.get('schema') != 'neoeng.dcore.changeset-core-invariant-ledger.v1':
            errors.append('schema do ledger de invariantes CS009 divergente')
        if ledger.get('changeset') != '009':
            errors.append('identidade do ledger de invariantes CS009 divergente')
        authorized = {row.get('path') for row in ledger.get('authorized_changes', [])}
        for row in ledger.get('entries', []):
            path = root / row['path']
            if not path.is_file():
                errors.append(f'arquivo do núcleo ausente no ledger CS009: {row["path"]}')
                continue
            actual = sha(path)
            if actual != row.get('result_sha256'):
                errors.append(f'hash atual diverge do ledger de invariantes CS009: {row["path"]}')
            changed = row.get('baseline_sha256') != row.get('result_sha256')
            if changed and row.get('path') not in authorized:
                errors.append(f'alteração canônica não autorizada no CS009: {row["path"]}')
            if not changed and row.get('path') in authorized:
                errors.append(f'ledger autoriza alteração inexistente no CS009: {row["path"]}')
        match = re.search(r'set\(NEOENG_DCORE_CORE_SOURCES\n(.*?)\n\)', cmake, re.S)
        current_sources = [line.strip() for line in match.group(1).splitlines() if line.strip()] if match else []
        if current_sources != ledger.get('core_source_list'):
            errors.append('lista de fontes canônicas foi alterada no CS009')
        result = ledger.get('result', {})
        if result.get('status') != 'passed' or not result.get('canonical_source_units_unchanged'):
            errors.append('ledger de invariantes CS009 não aprova preservação das unidades canônicas')
    except Exception as exc:
        errors.append(f'ledger de invariantes CS009 inválido: {exc}')

for json_path in (
    root / 'audit/HOST_SDK_GAP_ANALYSIS_1_6_0.json',
    root / 'audit/CHANGESET_007_CORE_INVARIANT_LEDGER.json',
    root / 'audit/CHANGESET_009_CORE_INVARIANT_LEDGER.json',
):
    if json_path.is_file():
        try:
            json.loads(json_path.read_text(encoding='utf-8'))
        except Exception as exc:
            errors.append(f'JSON inválido {json_path.relative_to(root)}: {exc}')

if errors:
    print('\n'.join(errors))
    sys.exit(1)
print('OK: Host SDK é companion unidirecional; C ABI v1, empacotamento e invariantes do núcleo verificados.')
