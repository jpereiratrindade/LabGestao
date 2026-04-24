# DDD - Ontology Profile

## Objetivo

Definir o modelo conceitual que permite ao LabGestao representar clareza e compatibilidade ontologica entre repositorios.

## Aggregate

`OntologyProfile`

Representa um snapshot ontologico de um repositorio inventariado.

## Componentes

- `entities`
  Entidades detectadas e normalizadas, separando declaradas e implementadas.
- `relations`
  Relacoes detectadas e normalizadas, separando declaradas e implementadas.
- `validity_rules`
  Indicadores de existencia de invariantes ou regras de validade.
- `identity_rules`
  Indicadores de existencia de regras de identidade.
- `doc_code_alignment`
  Coerencia entre DDD/ADR/README e sinais estruturais do codigo, modulos, includes/imports, build e APIs.
- `ontology_clarity_score`
  OCI do repositorio.
- `ontology_confidence_score`
  Grau de confianca da medicao ontologica.

## Fontes de Evidencia

### Fonte primaria

- `DDD`
- estrutura implementada do projeto

### Fontes secundarias

- `ADR`
- documentacao arquitetural
- `README`
- artefatos explicitos de ontologia
- codigo fonte
- arquivos de build e CI

### Fontes de contexto e confianca

- `DAI`
- `CI`
- contratos
- evidencias de auditoria

## Regras

1. `DDD` tem prioridade sobre outras fontes para inferir entidades e relacoes esperadas.
2. `ADR` e `DAI` nao substituem a ontologia do repositorio.
3. `CI` nao aumenta diretamente o `OCI`; ele afeta a confianca da medicao.
4. Um repositorio pode ter `OCI` alto e `confidence` baixo.
5. Um repositorio sem `DDD` formal pode ter perfil ontologico inferido, mas com maior risco de sub ou superestimacao.
6. Artefatos como `DDD`, `ADR`, `DAI`, `CI`, policies e evidence logs sao evidencias; nao sao entidades ontologicas do dominio por padrao.
7. Um termo so deve entrar como entidade quando tiver identidade distinguivel, persistencia conceitual, capacidade relacional, validade avaliavel e relevancia de dominio.
8. Divergencia entre entidades declaradas e implementadas reduz `doc_code_alignment` e, por consequencia, o `OCI`.

## Metricas

### OCI

```text
base = media(entities, relations, validity, identity)
OCI = base*0.75 + doc_code_alignment*0.25
```

### Confidence

```text
confidence = f(DDD, ADR, DAI, CI, tests, tool_contracts, audit_evidence)
```

### OCS

```text
OCS(A, B) = compatibilidade entre entidades, relacoes, validade e identidade
```

## Papel no sistema

O `OntologyProfile` nao serve apenas para score.
Ele serve para:

- comparabilidade entre repositorios
- leitura de convergencia
- base do motor de recomendacao
- exportacao estruturada

## Proximos passos

1. Introduzir ontologias canonicas por dominio.
2. Permitir baseline ontologico manual por projeto.
3. Reduzir dependencia de heuristica textual.
