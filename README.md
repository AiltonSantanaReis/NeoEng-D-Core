# NeoEng D-Core — pacote standalone auditado para Windows

Este pacote preserva o **NeoEng D-Core como autoridade canônica** sobre estado imutável, física numérica e rollback determinístico. O target `neoeng_dcore` continua sem dependência de renderização, GPU, SDF ou voxel. A partir do ChangeSet 003, o pacote inclui um módulo opcional e unidirecional `modules/view_lab`, que consome snapshots e traces somente para diagnóstico visual.

A fonte normativa obrigatória está em `docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`, apoiada pelos ledgers de máquina em `audit/`. Os documentos originais permanecem como proveniência histórica e não prevalecem quando houver conflito.

## Fonte de verdade e estado atual — ChangeSet 009

A versão 1.9.0 preserva a governança de fechamento estabelecida na 1.8.0 e fecha o escopo de evidência ECS Y1-O2 sem alterar a autoridade canônica, a transição, o fixed tick, o rollback, a serialização ou os hashes:

- fonte normativa obrigatória e hierarquia de precedência;
- 36 requisitos rastreáveis, 20 claims classificados e 41 limitações conhecidas;
- matriz de responsabilidade e backlog finito de fechamento;
- padrão normativo de conclusão e padrão de testes/asseguração;
- verificadores fail-closed com autotestes de adulteração;
- regra explícita de que meta-verificação não substitui prova técnica da capacidade;
- fechamento verificável de alocação geral, arena, copy-on-write e manutenção de índices;
- reconciliação entre o contrato interno do Ano 1 e a ABI C pública 1.0;
- CS010 e etapas posteriores permanecem abertos.

Comandos de verificação:

```powershell
python .\scripts\verify_product_contract.py --check-report
python .\scripts\verify_product_contract.py --self-test
python .\scripts\verify_product_assurance.py --check-report
python .\scripts\verify_product_assurance.py --self-test
```

O produto ainda não é declarado comercialmente concluído nem certificado. Consulte `docs/governance/PRODUCT_CLOSURE_PLAN.md` e `audit/PRODUCT_CLAIMS_LEDGER.json`.

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

O Windows é a plataforma operacional inicial. A fonte autoritativa 1.7.0 foi compilada e exercitada em Windows físico, com evidência independente preservada em `docs/changesets/008/evidence/WINDOWS_1_7_0_INDEPENDENT_VERIFICATION.md`. A baseline 1.9.0 deste ChangeSet foi validada em Linux x86_64 com GCC e Clang; **a regressão proporcional 1.9.0 ainda não foi executada em Windows físico**.

## Conteúdo auditado

- 64 headers canônicos do núcleo e 2 headers do View Lab, incluindo 1 header Year-2 preservado byte a byte;
- 58 fontes da biblioteca `neoeng_dcore` (`NeoEng::DCore`) e 2 fontes do View Lab;
- 78 fontes de aplicações/benchmarks/fuzz/probes no pacote (77 em `apps/` e o CLI do View Lab), todas cobertas pelo CMake; nove benchmarks de alocação usam `GNU ld --wrap` e ficam explicitamente indisponíveis no Windows;
- 8 fontes C++ de teste, 3 verificadores normativos Python e 64 testes CTest registrados quando o toolset completo, o View Lab e o Host SDK estão ativos;
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


## ChangeSet 004 — integridade criptográfica do estado

A versão 1.4.0 adiciona uma camada de evidência para checkpoints sem substituir o hash rápido do hot path:

- SHA-256 sobre a serialização canônica existente;
- Merkle Tree SHA-256 por chunks e provas de inclusão;
- envelope `neoeng.dcore.state-evidence-chain.v1`;
- detecção de remoção interna, reordenação, alteração e quebra da cadeia; truncamento final e anexação não autorizada são detectados contra uma âncora confiável;
- branches vinculadas ao envelope pai após rollback;
- interfaces desacopladas para assinatura externa;
- eventos correlacionáveis de criação e falha de evidência.

`stable_hash()` continua sendo o mecanismo operacional de 64 bits. SHA-256, Merkle e assinatura são calculados somente quando a política de checkpoint solicitar. HMAC-SHA256 existe apenas como autenticador de regressão controlada e não é apresentado como assinatura assimétrica.

Documentação: `docs/contracts/STATE_EVIDENCE_V1.md`, `docs/architecture/STATE_EVIDENCE_BOUNDARY.md` e `docs/changesets/004/`.


## ChangeSet 005 — observabilidade transversal e pacote de suporte

A versão 1.5.0 adiciona:

- eventos com subsistema, severidade, subject token pseudonimizável, hash relacionado e código de detalhe;
- budgets executáveis com emissão automática de `BudgetSampled` e `BudgetExceeded`;
- diagnóstico hierárquico de divergência por stable hash, SHA-256, Merkle e diff semântico;
- pacote `neoeng.dcore.support-bundle.v1` com manifesto SHA-256, limites, redaction report e verificador independente;
- autorização explícita para incluir captures de time-travel;
- ledger `audit/DEFERRED_VALIDATION_GATES.json`, que separa lacuna de implementação de validação nativa pendente.

No Windows, após o build:

```powershell
.\scripts\windows\collect-support-bundle.ps1
```

A campanha física Windows/ARM64 e a qualificação P0-P4 permanecem diferidas. Os resultados virtualizados são baseline de engenharia e não qualificam hardware. Documentação: `docs/contracts/OBSERVABILITY_V2.md`, `docs/contracts/SUPPORT_BUNDLE_V1.md` e `docs/changesets/005/`.

## ChangeSet 006 — harness de qualificação P0–P4

A versão 1.6.0 transforma os perfis de hardware em campanhas reproduzíveis sem declarar resultados que não foram executados:

- contrato `neoeng.dcore.hardware-qualification.v2`;
- estados distintos para evidência incompleta, baseline de engenharia e candidato nativo à qualificação;
- proibição explícita de `passed` em virtualização ou container;
- verificação de compatibilidade P0–P4, arquitetura, inventário, térmica, clocks, testes, hashes e amostras brutas;
- workload determinístico de manutenção ECS esparsa com CSV bruto;
- runner de campanha, verificador independente e comparador semântico x86_64/ARM64;
- wrappers Windows e Linux;
- registro formal de campanhas físicas diferidas.

Para executar uma campanha, complete uma cópia de `config/qualification_campaign.template.json` e use:

```powershell
.\scripts\windows\qualify-hardware-profile.ps1 -RequestPath .\qualification-request.json
```

Resultados virtualizados são aceitos apenas como `engineering_baseline`. Nenhum perfil P0–P4 foi qualificado no ChangeSet 006. Para P1, o benchmark de manutenção de índices está automatizado, mas evidências consolidadas de alocação geral, arena e copy-on-write continuam obrigatórias antes de qualquer aprovação. Documentação: `docs/contracts/HARDWARE_QUALIFICATION_V2.md`, `docs/architecture/QUALIFICATION_BOUNDARY.md` e `docs/changesets/006/`.


## ChangeSet 007 — integração com hosts e SDK instalável

A versão 1.7.0 formaliza a fronteira de integração já prevista sem alterar a semântica do núcleo:

- target companheiro `NeoEng::DCoreHostSdk`, dependente unidirecionalmente de `NeoEng::DCore`;
- ABI C 1.0 com handle opaco, códigos de status e layouts de estruturas fixos;
- lifecycle, avanço determinístico, correção/ressimulação, restauração de checkpoint, immutable views, hashes, traces e contrato de recovery;
- política explícita de thread proprietária e ausência de ponteiros internos;
- instalação/exportação CMake com `find_package(NeoEngDCore 1.7 CONFIG REQUIRED)`;
- host de referência em C e teste de consumo a partir de prefixo instalado limpo;
- verificadores de fronteira e ledger que comprovam que os 122 arquivos canônicos do núcleo permaneceram byte a byte inalterados.

O Host SDK recebe comandos confiáveis de um adapter no mesmo processo; dados de rede hostis continuam obrigatoriamente no gateway autenticado existente. Unreal, Unity e outras verticais não foram incorporados. Documentação: `docs/contracts/HOST_SDK_C_ABI_V1.md`, `docs/architecture/HOST_SDK_BOUNDARY.md` e `docs/changesets/007/`.

## ChangeSet 009 — evidência ECS completa e reconciliação de contratos

O benchmark ECS produz quatro streams aceitos pelo contrato `neoeng.dcore.ecs-scope-evidence.v1`: alocação geral, arena, copy-on-write e manutenção de índices. `scripts/qualification/verify_ecs_scope_evidence.py` recalcula sem confiar no benchmark as sequências, percentis, alocações, capacidade/overflow, reconstrução copy-on-write, hashes e vínculos de identidade.

A presença do pipeline completo não promove automaticamente o perfil P1. Campanhas sem hardware nativo, amostras mínimas, térmica, clocks ou zero-alocação permanecem `UNQUALIFIED`.

### Manifest portability correction

The CS009 rebuilt distribution also updates `scripts/generate_manifest.py` to use a case-sensitive, component-wise relative path key. The emitted path remains POSIX-formatted and the manifest is read and written as byte-exact UTF-8/LF content. This prevents host `WindowsPath` ordering from changing the release identity.
