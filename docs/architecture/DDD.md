# DDD - LabGestao

## Objetivo
Definir um modelo de dominio claro para gestao de projetos, fluxo de trabalho e decisoes tecnicas/operacionais.

## Contextos Delimitados

### 1) Portfolio de Projetos
Responsavel por cadastro, classificacao e estado do projeto.

- Entidade: `Project`
- Value Objects principais:
  - `ProjectStatus` (Backlog, Em Andamento, Revisao, Concluido, Pausado)
  - `Tag`
- Regras:
  - projeto tem identificador unico
  - mudanca de status deve respeitar DoR/DoD (quando aplicavel)

### 2) Decisoes Arquiteturais (ADR)
Responsavel por registrar decisoes de arquitetura e impactos.

- Entidade: `AdrRecord` (por projeto)
- Regras:
  - cada ADR precisa de contexto, decisao e consequencias
  - ADR deve ser imutavel como historico (preferencia por append)

### 3) Fluxo DAI (Decision/Action/Impediment)
Responsavel por registrar ciclos curtos de execucao e bloqueios.

- Entidade: `DaiEntry`
- Tipos:
  - Decision
  - Action
  - Impediment
- Regras:
  - impedimentos devem gerar acao de mitigacao
  - toda acao relevante deve ter responsavel e prazo

### 4) Metricas de Fluxo
Responsavel por indicadores de operacao (WIP, throughput, lead/cycle time).

- Entidade: `FlowMetricSnapshot`
- Regras:
  - metricas precisam ser calculadas por janela de tempo (7/14/30 dias)
  - exportacao deve manter formato estavel para analise externa

### 5) Workspace e Descoberta Automatica
Responsavel por configurar raiz monitorada e persistencia ativa.

- Entidade: `WorkspaceConfig`
- Regras:
  - `workspace_root` define a raiz funcional do usuario
  - `data_dir` deve apontar para `<workspace_root>/data` quando workspace esta definido
  - roots monitoradas devem vir de configuracao, nunca hardcoded
  - limpar projetos remove somente estado do LabGestao (arquivo JSON), sem apagar codigo-fonte

## Agregados e Fronteiras

- Agregado principal: `Project`
- Objetos internos agregados ao projeto:
  - ADRs
  - DAIs
  - conexoes entre projetos
- Repositorio de agregados:
  - `ProjectStore` (persistencia JSON)

## Linguagem Ubiqua
- Projeto
- Status
- ADR
- DAI
- WIP
- Throughput
- Lead Time
- Cycle Time
- Scaffold
- Projeto Auto-descoberto
- Workspace Root
- Data Dir Ativo
- Reclassificacao Automatizada

## Aplicacao
A camada de UI (`src/ui`) orquestra casos de uso e aciona regras de dominio (`src/domain`).
A inicializacao (`src/app/AppRuntime.cpp`) sincroniza auto-descoberta e aplica reclassificacao automatica do Kanban.
Persistencia atual via JSON em `<data_dir>/projects.json`, com configuracao em `~/.config/labgestao/settings.json`.

## Politica de Reclassificacao (Kanban Auto)
Aplicada para projetos auto-descobertos:

1. `Impediment` DAI aberto -> `Paused`
2. DAI aberto (sem impedimento) -> `Doing`
3. Sem DAI aberto: usa ultimo commit Git
4. `<= 7 dias` -> `Doing`
5. `<= 30 dias` -> `Review`
6. `<= 120 dias` -> `Paused`
7. `> 120 dias` -> `Backlog`

Transicoes validas devem passar por `canMoveToStatus` e ser registradas em `status_history`.

## Proximos passos DDD
1. Extrair casos de uso explicitos (`Application Services`) para reduzir logica em UI.
2. Criar validadores de invariantes de dominio fora da camada de apresentacao.
3. Introduzir testes de unidade no dominio e testes de integracao para persistencia.
