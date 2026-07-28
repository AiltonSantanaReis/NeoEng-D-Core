# CS011 — status de testes

Estado: aprovada no escopo declarado

Baseline: 1.11.0

Campanha: `TEST-CS011-001`

Fonte executada: `bef61a80f98f79eecb6da6bda5b36c3579500c32`

GitHub Actions aprovado:
`https://github.com/AiltonSantanaReis/NeoEng-D-Core/actions/runs/30345022035`

## Resultados

| Classe | Cobertura | Estado |
|---|---|---|
| positivo | primitivas representáveis, oracle racional pequeno e classificações aceitas | passou em Windows clang-cl, Linux GCC e Linux Clang |
| negativo/fail-closed | overflow, divisão por zero, domínio/tamanho inválido e claim não suportado | passou |
| adversarial | extremos `int64_t`, RAA disjunto e mutações do verificador | passou |
| fault injection/recovery | rejeição antes do resultado e fallback operacional sem promoção | passou |
| long run | 4.096 amostras, 20.480 decisões primitivas e 4.096 produtos RAA | passou nos três builds |
| integridade/verificação independente | manifesto SHA-256, recálculo semântico e autoteste de adulteração | passou nos três builds |
| regressão configurada Windows | núcleo, governança, Host SDK e referência distribuída | 24/24 testes |
| cross compiler | GCC/Clang x86_64 | passou; `raw-probe.json` byte a byte idêntico |
| cross architecture | ARM64 | não executado; nenhuma equivalência foi promovida |

O resultado semântico GCC/Clang tem SHA-256
`dc2347e7a3b9f0a391a99ef6764c1100a86f2a11847822c1bbc6df10a74bbb6f`.
O Windows registrou os mesmos hashes decisórios:

- Q32.32: `0xE256739A08E1EE4`;
- RAA: `0x8E16B390EF5EE325`;
- oracle racional: `0xC89FAE52652B9BF8`.

## Decisão comprovada

A campanha comprova o contrato estreito registrado, inclusive que:

- Y1-O4 não é permitido como claim de runtime;
- não existe promoção de certificado numérico global composto;
- o oracle racional é certificado somente no domínio pequeno declarado;
- o fallback conectado é operacional e não certificado.

Ela não comprova um estimador de Lyapunov de produção, todas as composições
Q32.32, certificação global do solver, ARM64 ou desempenho independente de
hardware.

## Evidência

- `evidence/windows-x86_64-clang-20260728/`
- `evidence/linux-x86_64-gcc-20260728/`
- `evidence/linux-x86_64-clang-20260728/`
- `evidence/cross-compiler-20260728/cross-compiler-summary.json`
- `evidence/github-actions-run-30345022035.json`

Cada campanha contém identidade de fonte/build, configuração, resultados
brutos, resumo, limitações, manifesto SHA-256 e relatório do verificador
independente.

Os dados Windows descrevem apenas o PC e a configuração registrados. Máquinas
inferiores ou mais potentes podem produzir tempos e capacidade observada
melhores ou piores; isso não é uma regra de requisito de hardware nem permite
inferir resultado para outra máquina.

O host registrado usou AMD Ryzen 7 5700X3D (8 núcleos/16 threads), 31,92 GiB
de memória física, NVIDIA GeForce RTX 3070 Ti 8 GiB e Windows 11 Pro build
26200. A GPU não participa do contrato numérico testado; ela é registrada
somente para tornar o ambiente reproduzível.
