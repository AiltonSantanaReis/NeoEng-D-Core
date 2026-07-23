# NeoEng D-Core — pacote standalone auditado para Windows

Este pacote preserva o **NeoEng D-Core como autoridade canônica** sobre estado imutável, física numérica e rollback determinístico. O target `neoeng_dcore` continua sem dependência de renderização, GPU, SDF ou voxel. A partir do ChangeSet 003, o pacote inclui um módulo opcional e unidirecional `modules/view_lab`, que consome snapshots e traces somente para diagnóstico visual.

A fonte normativa completa está em `docs/original/Plano_Deep_Tech_NeoEng_Ano1_Completo.pdf`, contendo as páginas físicas 12-15 (escopo técnico e gates) e 29-30 (plano operacional dos primeiros 180 dias e transição para a matriz global).

## Execução inicial no Windows

Pré-requisitos: Windows 10/11 x64, Developer PowerShell for VS 2022 com Windows SDK, CMake 3.25+, Ninja, LLVM/clang-cl x64, Git e vcpkg no baseline fixado.

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\windows\run-dcore.ps1 -BootstrapDependencies
```

Para compilar toda a superfície e executar a suíte Year-1 registrada:

```powershell
.\scripts\windows\run-dcore.ps1 -BootstrapDependencies -FullTestSuite
```

O Windows é a plataforma operacional inicial. O pacote foi compilado e testado nesta auditoria em Linux x86_64 para validar a integridade do projeto, mas **o novo empacotamento ainda não foi executado em Windows físico neste ambiente**.

## Conteúdo auditado

- 60 headers canônicos do núcleo e 2 headers do View Lab, incluindo 1 header Year-2 preservado byte a byte;
- 54 fontes da biblioteca `neoeng_dcore` (`NeoEng::DCore`) e 2 fontes do View Lab;
- 73 fontes de aplicações/benchmarks/fuzz/probes, todas cobertas pelo CMake; nove benchmarks de alocação usam `GNU ld --wrap` e ficam explicitamente indisponíveis no Windows;
- 5 fontes de teste e 46 testes registrados quando o toolset completo e o View Lab estão ativos;
- 166 registros documentais exatos do Ano 1;
- 23 scripts de campanha no caminho operacional original;
- evidências originais v0.12-v0.28 preservadas;
- binários Windows originais preservados apenas como proveniência, não como resultado do novo build.

Consulte `audit/NEOENG_DCORE_AUDIT_COMPLETE_REPORT.md`, `audit/DCORE_FILE_CATALOG.csv` e `docs/AUDIT_STATUS.md`.

## ChangeSet 001 — hardening operacional

A versão 1.1.0 adiciona módulos opt-in para:

- autenticação HMAC-SHA256, anti-replay, rate limiting, timeout e parsing total de inputs;
- trace correlacionável e time-travel debugging textual;
- fallback para device lost, I/O stall, indisponibilidade de rede, pacote malformado e OOM;
- qualificação explícita dos perfis P0-P3, vinculando os limites de 2,0 ms e 0,1 ms exclusivamente ao P1 registrado.

A integração end-to-end está em `neoeng::core::OperationalRuntime`. A documentação completa do patch está em `docs/changesets/001/`.

Para executar apenas os testes do reforço:

```powershell
ctest --test-dir build/windows-clang-release -R "operational_hardening|network_packet_fuzz|recovery_fault" --output-on-failure
```

Para registrar uma qualificação de hardware no Windows, use `scripts/windows/qualify-hardware-profile.ps1`. O script recusa aprovação quando o baseline está incompleto, o ambiente não coincide ou as medições obrigatórias estão ausentes. Os perfis permanecem `UNQUALIFIED` até campanha em hardware físico.


## ChangeSet 002 — sessão autenticada e recuperação formal

A versão 1.2.0 adiciona:

- handshake HMAC-SHA256 cliente/servidor com proteção anti-replay e anti-downgrade;
- derivação separada de chaves client→server e server→client;
- root key lifecycle, rotação, expiração, papéis e revogação conjunta de sessões;
- modo estrito do gateway, que exige sessão estabelecida por origem;
- contrato `neoeng.dcore.recovery.v1` com códigos estáveis, geração e acknowledgements tipados;
- restauração real de checkpoint e truncamento da linha temporal após rollback.

A documentação está em `docs/changesets/002/`. Para executar os testes específicos:

```powershell
ctest --test-dir build/windows-clang-release -R "session_recovery_contract|session_security|session_handshake|recovery_contract" --output-on-failure
```

O protocolo v1 autentica e protege a integridade, mas não cifra o tráfego e não oferece forward secrecy. Nonces e session IDs precisam vir de CSPRNG do host; HSM/TPM, transporte real, adapters Unreal/Unity e auditoria criptográfica independente permanecem etapas posteriores.

## ChangeSet 003 — extração modular de diagnóstico visual

A versão 1.3.0 recupera somente o subconjunto do Ano 2 necessário aos objetivos atuais de observabilidade e time-travel:

- Visibility Buffer CPU de referência, preservado byte a byte em `modules/view_lab/vendor/year2`;
- View Lab somente leitura, com BMPs determinísticos, viewer HTML e inspeção de entidades/traces;
- contrato `neoeng.dcore.visual-correlation.v1`;
- perfil P4 para compatibilidade e degradação segura em GPUs de 8 GB;
- evidências históricas selecionadas e verificadas por SHA-256.

O módulo é opcional no CMake bruto, habilitado nos presets oficiais e possui dependência apenas no sentido `view_lab -> neoeng_dcore`. GPU/EGL, SDF, voxel e modelos de pesquisa do Ano 2 continuam fora do núcleo.

No Windows:

```powershell
.\scripts\windows\run-view-lab.ps1 -BootstrapDependencies
```

Abra o `index.html` indicado pelo script. A documentação completa está em `docs/changesets/003/` e `docs/architecture/VIEW_LAB_BOUNDARY.md`.
