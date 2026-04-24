# ADR-0007: OCI e OCS como metricas estruturais do inventario

## Status
accepted

## Contexto

O inventario do LabGestao ja avaliava sinais operacionais, maturidade, confiabilidade e risco de improviso.
Faltava, porem, uma leitura explicita da estrutura ontologica dos repositorios:

- o que existe
- como se relaciona
- o que e valido
- como os elementos sao identificados

Sem isso, a comparacao entre projetos permanecia forte em sinais de engenharia e fraca em convergencia conceitual.

## Decisao

Adotar duas metricas novas no inventario e na UI:

- `OCI` (`Ontological Clarity Index`)
- `OCS` (`Ontological Compatibility Score`)

O documento de referencia passa a ser:

- `docs/ontology/OCI_OCS_FOUNDATION.tex`

A operacionalizacao inicial no produto sera heuristica, extraindo sinais a partir de documentacao e artefatos do repositorio.

## Consequencias

- a aplicacao passa a ter aba propria `Ontologia`
- o inventario passa a exportar dados ontologicos dedicados
- o relatorio ontologico passa a registrar parametros por projeto e compatibilidades entre pares
- o grafo de afinidade passa a considerar `OCI` e `OCS`

## Limites aceitos

- a leitura atual nao substitui uma ontologia formal completa
- a heuristica pode superestimar entidades em repositorios com muita documentacao textual
- refinamentos futuros devem introduzir ontologias canonicas por dominio
