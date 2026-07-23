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

- 52 headers canônicos;
- 46 fontes da biblioteca `neoeng_dcore` (`NeoEng::DCore`);
- 65 fontes de aplicações/benchmarks/fuzz/probes, todas cobertas pelo CMake; nove benchmarks de alocação usam `GNU ld --wrap` e ficam explicitamente indisponíveis no Windows;
- suíte unitária consolidada e 36 testes Year-1 registrados quando o toolset completo está ativo;
- 166 registros documentais exatos do Ano 1;
- 23 scripts de campanha no caminho operacional original;
- evidências originais v0.12-v0.28 preservadas;
- binários Windows originais preservados apenas como proveniência, não como resultado do novo build.

Consulte `audit/NEOENG_DCORE_AUDIT_COMPLETE_REPORT.md`, `audit/DCORE_FILE_CATALOG.csv` e `docs/AUDIT_STATUS.md`.
