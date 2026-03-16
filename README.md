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

## Workspace e pastas monitoradas (configuravel)

As roots monitoradas nao ficam mais hardcoded no codigo.
O app usa `~/.config/labgestao/settings.json` e cria um arquivo default na primeira execucao.
Se existir configuracao legada em `./data/settings.json`, o app migra automaticamente.
Por padrao, `monitored_roots` inicia vazio (nenhuma auto-descoberta).

Voce pode definir workspace pelo menu `Tools -> Selecionar Workspace...`.
Nesse fluxo, o app define:
- `workspace_root` = pasta escolhida
- `data_dir` = `<workspace_root>/data`
- `monitored_roots` com a propria raiz do workspace

Quando `workspace_root` e `data_dir` nao sao informados, o LabGestao cria a pasta `data` na raiz comum das roots monitoradas.
Exemplos:
- uma root `/home/user/dev/cpp` => `/home/user/dev/cpp/data`
- duas roots `/home/user/dev/cpp` e `/home/user/dev/python` => `/home/user/dev/data`
- sem roots validas => fallback para `./data`

Exemplo:

```json
{
  "workspace_root": "/home/seu-usuario/dev/labeco",
  "data_dir": "/home/seu-usuario/dev/data",
  "monitored_roots": [
    {
      "path": "/home/seu-usuario/dev/cpp",
      "category": "C++",
      "tags": ["Auto", "C++"],
      "marker_files": ["CMakeLists.txt", "meson.build", "Makefile", "compile_commands.json"]
    },
    {
      "path": "/home/seu-usuario/dev/python",
      "category": "Python",
      "tags": ["Auto", "Python"],
      "marker_files": ["pyproject.toml", "requirements.txt", "setup.py", "Pipfile"]
    }
  ]
}
```

## Tools (menu)

Opcoes principais:

- `Selecionar Workspace...`: define workspace, atualiza `settings.json`, cria `<workspace>/data` e passa a usar esse `data_dir`.
- `Reclassificar Kanban (Auto)`: recalcula status de projetos auto-descobertos com base em DAI e atividade Git.
- `Abrir pasta de dados`: abre o diretorio ativo de dados no sistema.
- `Abrir lista de projetos`: abre o arquivo `projects.json`.
- `Criar Projeto`: atalho para abrir criacao (`Ctrl+N`).
- `Limpar Projetos`: limpa apenas o arquivo de dados do LabGestao (nao remove pastas de projetos no disco).

O topo da UI mostra `Data ativo: ...` para indicar exatamente qual pasta esta sendo usada.

## Reclassificacao automatica de Kanban

A classificacao automatica (manual pelo menu e aplicada no startup) segue esta prioridade para projetos auto-descobertos:

1. Se houver DAI aberto do tipo `Impediment` -> `Pausado`
2. Senao, se houver DAI aberto -> `Em Andamento`
3. Senao, usa data do ultimo commit Git:
- `<= 7 dias` -> `Em Andamento`
- `<= 30 dias` -> `Revisao`
- `<= 120 dias` -> `Pausado`
- `> 120 dias` -> `Backlog`

Mudancas validas registram historico de transicoes no projeto.

## Tabela de projetos

Na aba `Projetos`, o clique no cabecalho agora ordena por:
- `Nome`
- `Categoria`
- `Criado em`

Se voce quiser zerar o estado local (sem projetos antigos), remova:

```bash
rm -f ~/.config/labgestao/settings.json
rm -f /caminho/do/workspace/data/projects.json
```
