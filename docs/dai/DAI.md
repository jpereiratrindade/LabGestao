# DAI Log (Decision / Action / Impediment)

## Como usar
Registrar entradas curtas para manter rastreabilidade de execucao.

Formato recomendado:
- `Decision`: escolha tecnica/negocial com racional
- `Action`: proxima acao concreta com dono e prazo
- `Impediment`: bloqueio atual e estrategia de remocao

## Entradas

### 2026-03-16
- Decision: formalizar arquitetura com DDD e ADR para reduzir conhecimento tacito no repositorio.
- Action: criar base documental em `docs/architecture`, `docs/adr` e `docs/dai`.
- Action: incluir pipeline de CI para build automatico em push e pull request.
- Impediment: nao existem testes automatizados ainda.
- Action: abrir frente de trabalho para testes de dominio e integracao de persistencia.
- Decision: workspace passa a ser fonte de verdade para persistencia (`data_dir`) e roots monitoradas.
- Action: adicionar menu `Tools -> Selecionar Workspace...` com seletor interno e dialogo de sistema.
- Action: remover criacao forcada de `data/` no diretorio do binario (CMake pos-build).
- Decision: aplicar reclassificacao automatica do Kanban para projetos auto-descobertos.
- Action: executar reclassificacao automatica no startup e disponibilizar gatilho manual em `Tools`.
- Action: habilitar ordenacao por clique em colunas da tabela de projetos (Nome, Categoria, Criado em).

## Template

Data: YYYY-MM-DD
- Decision:
- Action:
- Impediment:
