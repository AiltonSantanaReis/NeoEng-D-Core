# CS012 — status de testes

Estado: **aprovado**

Baseline: `1.12.0`

Campanha: `TEST-CS012-001`

Fonte comum: `7dc8a6f66190382adf7ee45097745d3b1c811e42`

## Resultados

- Windows 11 x86_64 com clang-cl: regressão configurada `29/29` e campanha
  temporal aprovadas.
- Linux x86_64 GCC: campanha e verificação independente aprovadas.
- Linux x86_64 Clang: campanha e verificação independente aprovadas.
- GitHub Actions:
  [run 30349979328](https://github.com/AiltonSantanaReis/NeoEng-D-Core/actions/runs/30349979328),
  concluído com sucesso.
- Comparação GCC/Clang: saída semântica byte a byte idêntica, SHA-256
  `451ba76738a7b2572e731a980360452d1726b3ecc5a0d52e3fabc0084203dc20`.

Em cada ambiente, o probe registrou 512 segmentos duráveis, 4.096 efeitos
externos, 4.096 commits, 2.048 compensações, os nove caminhos obrigatórios e
seis campos canônicos. O digest final durável foi
`1d78efae8d396de2d6c50746b8f392e911b1ccf9c656e3e0b8a8dc9780a019f8`.

## Evidência

- `evidence/windows-x86_64-clang-20260728`
- `evidence/github-actions-run-30349979328/cs012-linux-gcc`
- `evidence/github-actions-run-30349979328/cs012-linux-clang`
- `evidence/github-actions-run-30349979328/cs012-cross-compiler-comparison`

Os três manifestos foram recalculados pelo verificador independente e
aprovados sem inferir resultados nativos ou externos.

## Limites da aprovação

Os resultados descrevem apenas a fonte, o host e a configuração registrados.
O hardware Windows foi AMD Ryzen 7 5700X3D, 31,92 GiB de memória e Windows 11
Pro; a GPU não participa do contrato temporal. Máquinas de menor ou maior
potência podem apresentar resultados melhores ou piores, sem regra monotônica
ou requisito universal derivado desta campanha.

ARM64 não foi executado. Âncora externa, custódia independente e armazenamento
idempotente do executor permanecem responsabilidades de deployment/host.
Rollback não desfaz efeito irreversível já confirmado, e o produto não declara
`exactly once` sem host conforme.
