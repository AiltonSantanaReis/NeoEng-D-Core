# Entrada Windows — NeoEng D-Core

Execute a partir de **Developer PowerShell for VS 2022**:

```powershell
.\scripts\windows\run-dcore.ps1 -BootstrapDependencies
```

Ou use `RUN_WINDOWS.cmd`. O bootstrap não instala Visual Studio, Windows SDK, CMake, Ninja ou LLVM; ele apenas valida esses componentes e, quando autorizado, baixa o vcpkg no baseline fixado.

Falhas de configuração, compilação e testes geram código de saída diferente de zero e logs em `artifacts/local/<preset>`.

## View Lab

Os presets oficiais habilitam o módulo opcional de diagnóstico visual. Para compilar, executar os testes e gerar o viewer estático:

```powershell
.\scripts\windows\run-view-lab.ps1 -BootstrapDependencies
```

O script produz BMPs, `visual-correlation.json` e `index.html` em `artifacts/local/<preset>/view-lab`. O View Lab é somente leitura e não altera o estado canônico.
