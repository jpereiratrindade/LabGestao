# Manual: Referencia de Metricas do LabGestao

Este documento consolida as metricas usadas pelo LabGestao para fluxo, inventario, governanca, confiabilidade e ontologia.

As metricas fazem parte de um contexto mais amplo: o LabGestao opera como ambiente laboratorial de analise, caracterizacao e avaliacao de sistemas. Ele observa repositorios, extrai sinais estruturais, cruza documentos com codigo, compara perfis e produz recomendacoes. Assim, indicadores como `OCI` e `OCS` nao sao apenas contagens; eles formalizam principios de observacao ontologica e estrutural aplicados a outros sistemas.

## Escalas

- Scores numericos usam escala `0..100`, salvo quando indicado.
- Percentuais de cobertura usam `0..100%`.
- Tempos sao expressos em dias.
- Leituras heuristicas devem ser interpretadas como apoio a decisao, nao como prova formal isolada.

## Resumo Ontologico Operacional

Os indicadores ontologicos nao devem ser interpretados como contagem simples de termos. Em especial, `Entidades` nao significa "quantos nomes, arquivos ou classes foram encontrados".

### Entidades

`ontology_entities_score` mede a caracterizacao das entidades ontologicamente relevantes do projeto. O score cruza:

- entidades declaradas em DDD, README, ADR, documentacao arquitetural ou artefatos explicitos de ontologia;
- entidades implementadas no codigo e na estrutura do projeto;
- coerencia `Doc/Codigo`, isto e, quanto o que foi declarado aparece de forma compativel na implementacao.

```text
ontology_entities_score =
  declared_entities_coverage*0.35
  + implemented_entities_coverage*0.35
  + doc_code_alignment*0.30
```

Limites aplicados:

- so implementacao sem declaracao: maximo `55`;
- so declaracao sem implementacao: maximo `60`;
- `Doc/Codigo < 40`: maximo `70`.

Assim, um repositorio com muitos tipos/classes/arquivos nao deve receber `Entidades=100` automaticamente.

### Relacoes

`ontology_relations_score` segue a mesma ideia, cruzando relacoes declaradas com relacoes implementadas por includes/imports, links de build, eventos, persistencia e APIs.

```text
ontology_relations_score =
  declared_relations_coverage*0.35
  + implemented_relations_coverage*0.35
  + doc_code_alignment*0.30
```

### Doc/Codigo

`doc_code_alignment` mede a coerencia entre a ontologia declarada e a estrutura implementada.

```text
doc_code_alignment =
  declared_entity_coverage*0.70
  + declared_relation_coverage*0.30
```

### OCI

`OCI` consolida entidades, relacoes, validade e identidade, ajustando o resultado pela coerencia `Doc/Codigo`.

```text
base =
  (ontology_entities_score
  + ontology_relations_score
  + ontology_validity_score
  + ontology_identity_score) / 4

OCI = base*0.75 + doc_code_alignment*0.25
```

### OCS

`OCS` compara dois repositorios por similaridade de entidades, relacoes, validade e identidade. Ele aparece na UI como `Top OCS` e `Par`.

```text
OCS =
  entity_similarity*0.40
  + relation_similarity*0.30
  + validity_similarity*0.15
  + identity_similarity*0.15
```

## Metricas de Fluxo

### Projetos totais

Quantidade total de projetos carregados no `ProjectStore`.

### Projetos em WIP

Quantidade de projetos em `Doing` ou `Review`.

```text
WIP = count(status in {Doing, Review})
```

### Projetos concluidos

Quantidade de projetos em `Done`.

### Throughput

Quantidade de transicoes para `Done` dentro da janela selecionada.

Janelas usadas na UI: `7`, `14` e `30` dias.

```text
throughput(window) = count(status_history.to == Done and today - moved_at <= window)
```

### Aging

Idade do projeto desde `created_at` ate a data de referencia.

```text
aging_days = days(today - created_at)
```

### Lead Time

Tempo entre criacao e primeira conclusao (`Done`), quando existe.

```text
lead_time_days = days(done_at - created_at)
```

### Cycle Time

Tempo entre primeira entrada em `Doing` e conclusao. Se ainda nao concluiu, mede de `Doing` ate hoje.

```text
cycle_time_days = days(done_at - first_doing_at)
cycle_time_days_open = days(today - first_doing_at)
```

### Transicoes de status

Quantidade de registros em `status_history`.

### DAI abertos

Quantidade de itens DAI nao fechados.

### Impedimentos abertos

Quantidade de DAI abertos com `kind == "Impediment"`.

## Metricas de Inventario

### Score operacional

Mede prontidao operacional basica do repositorio.

```text
scoreOperational =
  README(20)
  + CI(20)
  + Testes(15)
  + Build system(20)
  + .gitignore(10)
  + LICENSE(5)
  + Sem artefatos rastreados(10)
```

Artefatos rastreados incluem extensoes e diretorios como `.o`, `.so`, `.a`, `.exe`, `.bin`, `.zip`, `build`, `dist`, `out` e `bin`.

### Score de maturidade

Mede presenca de artefatos estruturais e governanca operacional.

```text
scoreMaturity =
  ADR(25)
  + DDD(25)
  + DAI(25)
  + governanceSignals/8 * 25
```

### Sinais de governanca

Contagem `0..8`:

- `CONTRIBUTING`
- `CODEOWNERS`
- template de Pull Request
- template de Issue
- policies
- contratos de ferramentas/agentes
- matriz/politica de aprovacao
- evidencias de auditoria

### Confiabilidade configurada

Mede presenca de guardas de qualidade em arquivos de build/configuracao.

```text
scoreReliability =
  ASan/UBSan(15)
  + Leak check(10)
  + Static analysis(20)
  + Strict warnings(15)
  + Complexity guard(15)
  + Cycle guard(10)
  + Format/lint(15)
```

### Confiabilidade executada

Usa a mesma formula da confiabilidade configurada, mas procura os sinais em arquivos de CI. A intencao e diferenciar uma ferramenta apenas configurada de uma ferramenta efetivamente executada no pipeline.

### Score total

Composicao sintetica usada para ordenar e priorizar repositorios.

```text
scoreTotal =
  scoreOperational*0.45
  + scoreMaturity*0.30
  + scoreReliabilityConfig*0.15
  + scoreReliabilityExec*0.10
```

### Engineering Discipline

Leitura sintetica da disciplina de engenharia.

```text
scoreEngineeringDiscipline =
  scoreOperational*0.20
  + scoreMaturity*0.35
  + scoreReliabilityConfig*0.25
  + scoreReliabilityExec*0.20
  + ADR bonus(3)
  + DDD bonus(3)
  + DAI bonus(2)
  + governanceSignals/8*4
  - min(artifacts, 20)*0.5
```

### Vibe Risk

Proxy do risco de improviso, baixa rastreabilidade e pouca validacao.

```text
scoreVibeRisk =
  100 - scoreEngineeringDiscipline
  + ausencia ADR(8)
  + ausencia DDD(10)
  + ausencia DAI(6)
  + governanca fraca(0..8)
  + ausencia CI(8)
  + ausencia testes(10)
  + ausencia strict warnings(5)
  + ausencia complexity guard(7)
  + ausencia cycle guard(5)
  + min(artifacts, 15)
```

## Metricas de Governanca por Projeto

O `GovernanceProfile` do projeto mede governanca no contexto do projeto cadastrado.

### Maturity score

```text
maturity_score =
  ADR(25)
  + DDD(25)
  + DAI(25)
  + governanceSignals/8*25
```

### Vibe risk

```text
vibe_risk =
  100 - maturity_score
  + ausencia policies(8)
  + ausencia tool contracts(10)
  + ausencia approval policy(8)
  + ausencia audit evidence(8)
  + path indisponivel(6)
```

### Rotulo de maturidade

- `>= 80`: Governanca forte
- `>= 55`: Governanca intermediaria
- `< 55`: Governanca incipiente

## Metricas Ontologicas

Detalhes formais estao em `docs/ontology/OCI_OCS_FOUNDATION.tex`.

### Entidade operacional

Uma entidade deve ter identidade propria, persistencia conceitual, capacidade relacional, validade avaliavel e relevancia de dominio. DDD, ADR, DAI, CI, policies e evidencias sao fontes de leitura ou confianca; nao sao entidades ontologicas automaticamente.

### Entidades e relacoes declaradas

Extraidas de DDD, documentacao de arquitetura, README, ADR e artefatos explicitos de ontologia.

### Entidades e relacoes implementadas

Extraidas de codigo, estrutura de diretorios, includes/imports, links de build, eventos, persistencia e APIs.

### Doc/Codigo

Coerencia entre ontologia declarada e estrutura implementada.

```text
doc_code_alignment =
  declared_entity_coverage*0.70
  + declared_relation_coverage*0.30
```

Quando nao ha declaracao nem implementacao, o valor neutro usado e `50`. Quando nao ha declaracao, mas ha implementacao, o valor base usado e `35`.

### OCI

`Ontological Clarity Index`: mede clareza ontologica do repositorio.

```text
base =
  (ontology_entities_score
  + ontology_relations_score
  + ontology_validity_score
  + ontology_identity_score) / 4

OCI = base*0.75 + doc_code_alignment*0.25
```

Componentes:

- `ontology_entities_score`: caracterizacao de entidades a partir de entidades declaradas, entidades implementadas e coerencia `Doc/Codigo`.
- `ontology_relations_score`: caracterizacao de relacoes a partir de relacoes declaradas, relacoes implementadas e coerencia `Doc/Codigo`.
- `ontology_validity_score`: presenca de invariantes, regras ou validacoes.
- `ontology_identity_score`: presenca de regras de identidade ou identificadores canonicos.
- `ontology_confidence_score`: confianca na leitura, derivada de DDD, ADR, DAI, CI, testes, contratos e evidencias.

```text
ontology_entities_score =
  declared_entities_coverage*0.35
  + implemented_entities_coverage*0.35
  + doc_code_alignment*0.30

ontology_relations_score =
  declared_relations_coverage*0.35
  + implemented_relations_coverage*0.35
  + doc_code_alignment*0.30
```

Limites aplicados:

- se so ha implementacao sem declaracao, o score maximo e `55`;
- se so ha declaracao sem implementacao, o score maximo e `60`;
- se `Doc/Codigo < 40`, o score maximo e `70`.

### Confidence ontologica

```text
ontology_confidence_score =
  DDD(45)
  + ADR(15)
  + DAI(10)
  + CI(10)
  + Testes(5)
  + Tool contracts(10)
  + Audit evidence(5)
```

### OCS

`Ontological Compatibility Score`: mede compatibilidade entre dois repositorios.

```text
OCS =
  entity_similarity*0.40
  + relation_similarity*0.30
  + validity_similarity*0.15
  + identity_similarity*0.15
```

Entidades e relacoes usam similaridade de Jaccard sobre os conjuntos normalizados.

Interpretacao:

- `>= 70`: integrar
- `>= 40`: alinhar
- `< 40`: manter separado

## Cobertura de Boas Praticas

Na aba de metricas, a cobertura mostra o percentual de repositorios do inventario com:

- CI
- testes
- ASan/UBSan
- static analysis
- format/lint

## Tendencia

A tendencia compara a media atual de `scoreTotal` com o ultimo snapshot `inventory-*.json` exportado.

```text
variacao = media_scoreTotal_atual - media_scoreTotal_snapshot
```

## Exportacoes

- `metrics-YYYY-MM-DD-Nd.csv`: metricas de fluxo globais e por projeto.
- `inventory-YYYY-MM-DD.csv/json`: inventario, scores e sinais por repositorio.
- `ontology-YYYY-MM-DD.csv/json`: perfil ontologico e compatibilidades.
- `ontology-report-YYYY-MM-DD.md`: relatorio ontologico.
- `executive-report-YYYY-MM-DD.md`: consolidado executivo com fluxo, inventario e ontologia.
