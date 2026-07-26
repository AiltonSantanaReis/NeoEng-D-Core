# Evidência do CS010

Campanha imutável `TEST-CS010-001`, fonte
`c3efbcc54e8317da82e645f7b223d49e25b3d579`.

- `windows-x86_64-clang-20260726`: execução host-local no PC registrado;
- `linux-x86_64-gcc-20260726`: execução GitHub Actions GCC;
- `linux-x86_64-clang-20260726`: execução GitHub Actions Clang;
- `cross-compiler-20260726`: comparação byte a byte dos resultados semânticos.
- `github-actions-run-30187433814.json`: identidade do run, jobs e artefatos.

Cada diretório de campanha contém identidade de fonte/build, configuração,
saídas brutas, resumo, manifesto SHA-256, verificação independente e
limitações. O run aprovado é `30187433814`; seus três jobs concluíram com
sucesso e o resultado semântico GCC/Clang tem SHA-256
`a5782797532cfba07d52fe2f28f4453452aba566127e4d7da010fcbd057e63f2`.

O resultado prova somente o escopo e ambientes registrados. Não qualifica
ARM64, hardware universal ou transporte remoto de produção.
