# Manual: Referencia de Metricas do LabGestao

Este documento consolida as metricas usadas pelo LabGestao para fluxo, inventario, governanca, confiabilidade e ontologia.

As metricas fazem parte de um contexto mais amplo: o LabGestao opera como ambiente laboratorial de analise, caracterizacao e avaliacao de sistemas. Ele observa repositorios, extrai sinais estruturais, cruza documentos com codigo, compara perfis e produz recomendacoes. Assim, indicadores como `OCI` e `OCS` nao sao apenas contagens; eles formalizam principios de observacao ontologica e estrutural aplicados a outros sistemas.

O enquadramento conceitual completo esta em `docs/labgestao_doc_base.md`. Este manual deve permanecer como referencia operacional das metricas; ele deriva desse enquadramento, mas nao substitui DDD, ADR ou a fundamentacao formal de OCI/OCS.

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

A aba `Inventario` avalia repositorios encontrados no workspace. Ela combina tres tipos de dados:

- `scores`: valores numericos `0..100` usados para ordenacao, priorizacao e diagnostico;
- `sinais`: campos booleanos exibidos como `OK` ou `-`;
- `contagens`: valores numericos diretos, como `Gov 0/8` e `Artefatos`.

As colunas visiveis na tabela sao:

| Coluna | Campo/exportacao | Tipo | Sentido |
| --- | --- | --- | --- |
| `Repo` | `repo` | texto | Nome do repositorio. |
| `Total(Escore)` | `score_total` | score | Prioridade sintetica geral. Maior e melhor. |
| `Disc` | `score_engineering_discipline` | score | Disciplina de engenharia. Maior e melhor. |
| `Vibe` | `score_vibe_risk` | risco | Risco de improviso/vibe coding. Menor e melhor. |
| `Oper` | `score_operational` | score | Prontidao operacional basica. |
| `Matur` | `score_maturity` | score | Maturidade estrutural/governanca. |
| `ConfCfg` | `score_reliability_config` | score | Guardas de confiabilidade configuradas no repositorio. |
| `ConfExec` | `score_reliability_exec` | score | Guardas de confiabilidade executadas no CI. |
| `Build` | `build` | texto | Sistema de build detectado. |
| `ADR` | `adr` | sinal | Presenca de ADR. |
| `DDD` | `ddd` | sinal | Presenca de DDD/modelagem de dominio. |
| `DAI` | `dai` | sinal | Presenca de DAI. |
| `Gov` | `governance_signals` | contagem | Sinais de governanca detectados, `0..8`. |
| `ASan/UB` | `asan_ubsan_cfg` | sinal | Sanitizers configurados. |
| `Leak` | `leak_check_cfg` | sinal | Leak check configurado. |
| `Static` | `static_analysis_cfg` | sinal | Analise estatica configurada. |
| `Warn` | `strict_warnings_cfg` | sinal | Warnings estritos configurados. |
| `Complex` | `complexity_guard_cfg` | sinal | Guarda de complexidade configurada. |
| `Cycles` | `cycle_guard_cfg` | sinal | Verificacao de ciclos configurada. |
| `Format` | `format_lint_cfg` | sinal | Format/lint configurado. |
| `README` | `readme` | sinal | README detectado. |
| `CI` | `ci` | sinal | CI detectado. |
| `Testes` | `tests` | sinal | Estrutura de testes detectada. |
| `Artefatos` | `artifacts` | contagem | Artefatos de build/binarios rastreados. Menor e melhor. |

O detalhe lateral do repositorio ainda mostra `OCI`, `Doc/Codigo`, `Top OCS`, sinais de governanca fortes, confiabilidade executada no CI e o plano de acao sugerido.

O botao `Exportar Caracterizacao`, disponivel no detalhe lateral do repositorio selecionado, gera um Markdown com a caracterizacao completa daquele projeto. O relatorio inclui todos os blocos do painel: resumo, operacao, ontologia, maturidade/governanca, confiabilidade configurada, confiabilidade executada, Top OCS, diagnostico e plano de acao.

### PCoA das metricas de projeto

A aba `Metricas` apresenta uma PCoA exploratoria dos repositorios do inventario. O objetivo e mostrar proximidade, agrupamento e outliers considerando o conjunto completo de parametros aplicados a cada projeto.

Cada repositorio e convertido em um vetor numerico com:

- scores principais: `Total`, `Disc`, `Vibe` invertido, `Oper`, `Matur`, `ConfCfg` e `ConfExec`;
- indicadores ontologicos: `OCI`, entidades, relacoes, validade, identidade, confianca, `Doc/Codigo` e melhor `OCS`;
- governanca: `Gov 0/8` e sinais como ADR, DDD, DAI, policies, contratos, aprovacao, auditoria e evidencia de alinhamento OCS;
- confiabilidade configurada e executada: ASan/UBSan, leak check, analise estatica, warnings, complexidade, ciclos e format/lint;
- operacao basica: README, CI, testes e penalizacao por artefatos rastreados.

Os sinais booleanos sao tratados como `0` ou `100`. `Vibe` e `Artefatos` sao invertidos porque, nesses casos, valores menores indicam melhor situacao. As colunas sao padronizadas por z-score, a distancia euclidiana entre repositorios e calculada e a matriz de distancias e submetida a dupla centralizacao. Os dois primeiros eixos positivos formam o diagrama `PCo1 x PCo2`.

Essa PCoA nao e uma nova nota. Ela e uma leitura comparativa: projetos proximos possuem perfis metricos semelhantes; projetos isolados indicam perfis estruturais atipicos, lacunas especificas ou alta maturidade em relacao ao conjunto.

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

Sinais usados:

- `README`: arquivos como `README.md`, `README` ou `readme.md`.
- `CI`: diretorios/arquivos de CI, como `.github/workflows`, `.gitlab-ci.yml` ou `.circleci`.
- `Testes`: diretorios/arquivos de teste, incluindo `tests`, `test`, `src/test`, `src/tests`, `CTestTestfile.cmake` e equivalentes detectados pela estrutura.
- `Build system`: `CMakeLists.txt`, `Makefile`, `meson.build`, `pyproject.toml`, `package.json` ou sinais similares de build/projeto.
- `.gitignore`: arquivo `.gitignore` na raiz do repositorio.
- `LICENSE`: arquivos de licenca na raiz.

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

Critérios de deteccao:

- `ADR`: diretorios `adr/`, `docs/adr/` ou arquivos Markdown com padrao `ADR`.
- `DDD`: diretorios ou arquivos relacionados a `ddd`, `domain`, `architecture`, `domain-model`, `context-map` ou `bounded-context`.
- `DAI`: diretorios `dai/`, `docs/dai/` ou arquivos com token `dai`.
- `policies`: diretorios `policies/`, `docs/policies/` ou arquivos com tokens como `policy`, `policies`, `approval_matrix` ou `safety_constraints`.
- `tool contracts`: `mcp/contracts`, `mcp/tool_schema.json` ou arquivos com tokens como `contract`, `tool_schema` ou `agent_interface`.
- `approval policy`: `approval_matrix.md`, `approval-matrix.md`, `approval-policy` ou `CODEOWNERS`.
- `audit/evidence`: `evidence_log`, `audit`, `traceability`, `governance_task_packet` ou validadores de governanca.
- `OCS alignment evidence`: documento explicito de alinhamento ontologico, como `docs/ontology/OCS_ALIGNMENT_LABGESTAO.md`, usado para evitar que o plano continue pedindo mapeamento OCS ja registrado.

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

Arquivos considerados relevantes para `Config`:

- `.github/workflows/*`, `.gitlab-ci.yml`;
- `CMakeLists.txt`, `Makefile`, `meson.build`, `build.sh`;
- `.clang-tidy`, `.clang-format`;
- arquivos/caminhos que mencionem `cppcheck`, `valgrind`, `sanitizer` ou `quality`.

Arquivos considerados para `Exec`:

- `.github/workflows/*`;
- `.gitlab-ci.yml`;
- `.circleci/*`.

Padroes principais por sinal:

| Sinal | Configurado | Executado no CI |
| --- | --- | --- |
| `ASan/UBSan` | `-fsanitize=address`, `-fsanitize=undefined`, `asan`, `ubsan` em arquivos relevantes | mesmos padroes em arquivos de CI |
| `Leak check` | `valgrind`, `lsan_options`, `detect_leaks=1`, `asan_options=detect_leaks=1` | mesmos padroes em arquivos de CI |
| `Static analysis` | `clang-tidy`, `cppcheck`, `scan-build`, `include-what-you-use` | mesmos padroes em arquivos de CI |
| `Strict warnings` | `-Wall` + `-Wextra` + (`-Wpedantic` ou `-Werror`) | mesma combinacao em arquivos de CI |
| `Complexity guard` | `lizard`, `oclint`, `function-size`, `cyclomatic`, `cognitive complexity` | `lizard`, `oclint`, `cyclomatic`, `cognitive complexity` em CI |
| `Cycle guard` | `include-cycle`, `include_cycles`, `check_include_cycles`, `dependency cycle`, `cycle check`, `deptrac`, `module deps` | mesmos padroes em CI |
| `Format/lint` | `.clang-format`, `clang-format`, `format-check` ou sinais equivalentes | `clang-format` ou `format-check` em CI |

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
  + governanca ausente(8) ou fraca <=2 sinais(4)
  + ausencia CI(8)
  + ausencia testes(10)
  + ausencia strict warnings(5)
  + ausencia complexity guard(7)
  + ausencia cycle guard(5)
  + min(artifacts, 15)
```

### Leituras sinteticas no detalhe

O painel lateral transforma scores em leituras qualitativas:

- `Engineering Discipline`: `alta` quando `>= 80`, `media` quando `>= 60`, `baixa` abaixo disso.
- `Vibe Risk`: `alto` quando `>= 70`, `medio` quando `>= 40`, `baixo` abaixo disso.
- `Spaghetti Risk (proxy)`: `atencao` quando falta `Complexity guard`, falta `Cycle guard` ou falta `DDD`; caso contrario, `controlado`.

O diagnostico textual usa:

- disciplina `>= 80` e vibe `< 30`: engenharia deliberada forte;
- disciplina `>= 60`: zona de atencao;
- total `>= 40`: disciplina insuficiente e risco moderado;
- abaixo disso: projeto critico.

### Plano de acao sugerido

O plano de acao da aba `Inventario` deriva dos sinais ausentes e dos scores baixos. Exemplos:

- sem `README`, `CI`, `Testes`, `.gitignore`, `LICENSE` ou com artefatos rastreados: recomenda baseline operacional;
- sem `ADR`, `DDD`, `DAI` ou governanca forte: recomenda documentar decisoes, dominio, backlog, politicas e completar governanca ate `8/8`;
- `OCI`, `Doc/Codigo`, entidades, relacoes, validade ou identidade baixos: recomenda explicitar ontologia e reconciliar documentacao com codigo;
- `OCS` baixo ou apenas medio: recomenda mapear entidades equivalentes, relacoes compartilhadas e regras divergentes;
- sem guardas de confiabilidade: recomenda sanitizers, leak check, analise estatica, warnings, complexidade, ciclos e format-check.

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
- `repo-characterization-<repo>-YYYY-MM-DD.md`: caracterizacao completa do repositorio selecionado na aba `Inventario`.
- `ontology-YYYY-MM-DD.csv/json`: perfil ontologico e compatibilidades.
- `ontology-report-YYYY-MM-DD.md`: relatorio ontologico.
- `executive-report-YYYY-MM-DD.md`: consolidado executivo com fluxo, inventario e ontologia.
