# Evidência do CS011

Campanha imutável `TEST-CS011-001`, fonte
`bef61a80f98f79eecb6da6bda5b36c3579500c32`.

- `windows-x86_64-clang-20260728`: execução host-local no PC registrado;
- `linux-x86_64-gcc-20260728`: execução GitHub Actions GCC;
- `linux-x86_64-clang-20260728`: execução GitHub Actions Clang;
- `cross-compiler-20260728`: comparação byte a byte dos resultados semânticos;
- `github-actions-run-30345022035.json`: identidade do run, jobs e artefatos.

Cada diretório de campanha contém identidade de fonte/build, configuração,
saídas brutas, resumo, manifesto SHA-256, verificação independente e
limitações. O run aprovado é `30345022035`; seus três jobs concluíram com
sucesso e o resultado semântico GCC/Clang tem SHA-256
`dc2347e7a3b9f0a391a99ef6764c1100a86f2a11847822c1bbc6df10a74bbb6f`.

O resultado prova somente o escopo e ambientes registrados. Não qualifica
ARM64, hardware universal, Y1-O4 como predictor de produção ou certificado
numérico global.
