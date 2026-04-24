# DDD - Ontology Recommendation Engine

## Objetivo

Definir o bounded context responsavel por transformar `OCI`, `OCS` e sinais de governanca em recomendacoes acionaveis.

## Entradas

- `OntologyProfile`
- `GovernanceProfile`
- `ProjectStatus`
- `OCI`
- `OCS`
- componentes de `OCI`
- fator de confianca

## Saidas

- `structural_recommendations`
- `relational_recommendations`
- `operational_recommendations`

## Tipos de recomendacao

### Estrutural

Baseada em:

- entidades
- relacoes
- validade
- identidade
- confianca

Exemplo:

```text
OCI baixo + relacoes ausentes -> explicitar relacoes formais
```

### Relacional

Baseada em:

- `OCS`
- composicao do `OCI`
- diferencas entre perfis ontologicos

Exemplo:

```text
OCS alto + OCI baixo -> evitar integracao prematura
```

### Operacional

Baseada em:

- status
- governanca
- maturidade ontologica

Exemplo:

```text
Projeto em andamento + ontologia fraca -> pausar expansao funcional e estabilizar estrutura
```

## Regras de Dominio

1. Recomendacao nao e decisao automatica.
2. `OCS` alto nao implica integracao obrigatoria.
3. `OCI` baixo em projeto em andamento indica risco de expansao desorganizada.
4. Confianca baixa deve reduzir assertividade da recomendacao.
5. DDD ausente reduz a capacidade de recomendacao estrutural forte.

## Papel de DDD, ADR, DAI e CI

- `DDD`
  Principal fundamento para recomendacoes estruturais.
- `ADR`
  Reforca justificativas arquiteturais.
- `DAI`
  Informa urgencia, impedimentos e janela de intervencao.
- `CI`
  Reforca confianca operacional, mas nao substitui clareza ontologica.

## Resultado esperado

Transformar analise ontologica em:

- recomendacao de integracao
- recomendacao de alinhamento
- recomendacao de estabilizacao
- recomendacao de manter separado
