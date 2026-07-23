# NeoEng D-Core — pacote standalone auditado para Windows

Este pacote isola o **Ano 1 - núcleo imutável, física numérica e rollback determinístico** do `v0342.zip`. A árvore ativa não contém renderização, Visibility Buffer, SDF, voxel, GPU ou unidades `y2_*`.

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

- 57 headers canônicos (52 da base e 5 adicionados pelo ChangeSet 001);
- 51 fontes da biblioteca `neoeng_dcore` (`NeoEng::DCore`), sendo 46 da base e 5 do ChangeSet 001;
- 69 fontes de aplicações/benchmarks/fuzz/probes, todas cobertas pelo CMake; nove benchmarks de alocação usam `GNU ld --wrap` e ficam explicitamente indisponíveis no Windows;
- 2 fontes de teste e 39 testes registrados quando o toolset completo está ativo;
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
