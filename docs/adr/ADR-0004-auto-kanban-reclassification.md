# ADR-0004: Reclassificacao automatica de Kanban para projetos auto-descobertos

- Status: accepted
- Data: 2026-03-16

## Contexto
Com muitos projetos auto-descobertos, o Kanban ficava concentrado em `Backlog`,
sem refletir atividade real dos repositorios nem bloqueios operacionais.

## Decisao
Aplicar politica de reclassificacao automatica para projetos auto-descobertos:

1. DAI aberto do tipo `Impediment` -> `Paused`
2. DAI aberto (sem impedimento) -> `Doing`
3. Sem DAI aberto: usar ultimo commit Git
4. `<= 7 dias` -> `Doing`
5. `<= 30 dias` -> `Review`
6. `<= 120 dias` -> `Paused`
7. `> 120 dias` -> `Backlog`

A politica pode ser executada manualmente em `Tools -> Reclassificar Kanban (Auto)`
e tambem e aplicada no startup do app.

## Consequencias
- Positivas:
  - Kanban mais aderente ao estado real do portfolio
  - menor manutencao manual de status
  - historico de transicoes continua preservado
- Negativas:
  - depende de disponibilidade de metadados Git
  - pode divergir da percepcao humana em casos especiais

## Alternativas consideradas
- Manter classificacao apenas manual
- Reclassificar todos os projetos (incluindo nao auto-descobertos)
