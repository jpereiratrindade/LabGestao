# LabGestao

Build nativo para Fedora Silverblue, com opcao de ambiente via Nix.

## Fluxo recomendado no Silverblue

O caminho mais simples no Silverblue e usar um `toolbox`, para manter as dependencias de desenvolvimento fora da imagem base:

```bash
toolbox create
toolbox enter
sudo dnf install cmake gcc-c++ pkgconf-pkg-config SDL2-devel mesa-libGL-devel curl ninja-build
./build.sh
cd build
./LabGestao
```

## Alternativa com `rpm-ostree`

Se voce quiser instalar direto no host imutavel:

```bash
sudo rpm-ostree install cmake gcc-c++ pkgconf-pkg-config SDL2-devel mesa-libGL-devel curl ninja-build
systemctl reboot
./build.sh
```

## Opcional: ambiente com Nix (flake)

Se preferir reproducibilidade com Nix:

```bash
nix develop
./build.sh
cd build-ninja
./LabGestao
```

## O que o `build.sh` faz

- Baixa Dear ImGui para `vendor/imgui` se os arquivos ainda nao existirem.
- Configura o projeto com CMake em `build/` ou `build-ninja/`, conforme o gerador disponivel.
- Compila o executavel `LabGestao`.

## Dependencias

- `cmake`
- `gcc-c++`
- `pkgconf-pkg-config`
- `SDL2-devel`
- `mesa-libGL-devel`
- `curl`
- `ninja-build`

## Scaffold de novo projeto (C++/Python)

Na tela `Projetos`, ao clicar em `+ Novo Projeto`, voce pode:

- Escolher `Template`: `Nenhum`, `C++` ou `Python`
- Marcar `Criar estrutura base no disco`
- Informar o `Diretorio base` (ou usar o padrao)

Estrutura minima gerada:

- `C++`: `CMakeLists.txt`, `README.md`, `.gitignore`, `src/main.cpp`, `include/`, `tests/`
- `Python`: `pyproject.toml`, `README.md`, `.gitignore`, `src/<pacote>/`, `tests/test_smoke.py`

## Governanca tecnica

- DDD: `docs/architecture/DDD.md`
- ADRs: `docs/adr/README.md`
- DAI log: `docs/dai/DAI.md`

## CI

Pipeline de CI via GitHub Actions em `.github/workflows/ci.yml`:

- dispara em `push` para `master/main` e em `pull_request`
- valida configuracao CMake + build com Ninja em Ubuntu
- executa `ctest` automaticamente quando houver testes configurados

## Testes

Executar localmente:

```bash
cmake -S . -B build-ninja -G Ninja
cmake --build build-ninja --parallel
ctest --test-dir build-ninja --output-on-failure
```
