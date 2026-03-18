# ADR-0005: Extrair Application Services para orquestracao de casos de uso

- Status: proposed
- Data: 2026-03-18

## Contexto
As regras de dominio ja estao concentradas em `ProjectStore`, mas a orquestracao de fluxos
de uso ainda esta distribuida entre `src/app/AppRuntime.cpp` e `src/ui/AppUI.cpp`.
Hoje, por exemplo, a reclassificacao automatica do Kanban existe em mais de um ponto,
o que aumenta custo de manutencao, risco de divergencia e dificulta testes de cenarios
de aplicacao ponta-a-ponta sem depender da camada de UI.

## Decisao
Introduzir uma camada explicita de `Application Services` para concentrar casos de uso,
com API orientada a comandos (ex.: `syncAutoDiscoveredProjects`, `reclassifyKanbanAuto`,
`selectWorkspace`, `exportFlowMetrics`), mantendo:

- `src/domain`: regras de negocio e invariantes.
- `src/app` e `src/ui`: adaptadores de entrada/saida, sem orquestracao duplicada.

Implementacao incremental:
- Fase 1: extrair reclassificacao automatica para service unico reutilizado por startup e UI.
- Fase 2: extrair sincronizacao de descoberta automatica e operacoes de workspace.
- Fase 3: mover demais fluxos com cobertura de testes de aplicacao.

## Consequencias
- Positivas:
  - reduz duplicacao de logica de fluxo entre runtime e UI
  - melhora testabilidade de casos de uso sem dependencias visuais
  - cria fronteira clara entre "orquestracao" e "regra de dominio"
- Negativas:
  - aumenta numero de tipos/camadas no curto prazo
  - exige migracao cuidadosa para evitar regressao de comportamento

## Alternativas consideradas
- Manter orquestracao na UI/runtime com funcoes utilitarias compartilhadas.
- Mover tudo para `ProjectStore`, misturando regra de dominio com fluxo de aplicacao.
