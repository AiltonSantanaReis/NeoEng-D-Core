# Evidência do CS012

A campanha `TEST-CS012-001` foi aprovada sobre a fonte
`7dc8a6f66190382adf7ee45097745d3b1c811e42`.

| Evidência | Ambiente | Resultado |
|---|---|---|
| `windows-x86_64-clang-20260728` | Windows 11 x86_64, clang-cl | aprovado |
| `github-actions-run-30349979328/cs012-linux-gcc` | Linux x86_64, GCC | aprovado |
| `github-actions-run-30349979328/cs012-linux-clang` | Linux x86_64, Clang | aprovado |
| `github-actions-run-30349979328/cs012-cross-compiler-comparison` | comparação GCC/Clang | byte a byte idêntico |

Cada ambiente registra identidade de fonte e build, configuração, resultados
brutos, resumo, limitações, `SHA256SUMS.txt` e verificação independente.
Resultados descrevem somente o host/configuração registrados e não autorizam
inferência para outra máquina ou arquitetura. ARM64 não foi executado.
