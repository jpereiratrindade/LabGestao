# Ontologia no LabGestao

## Base conceitual

O LabGestao passa a tratar `OCI` e `OCS` como metricas estruturais do inventario de projetos.

- `OCI` (`Ontological Clarity Index`) mede o grau de explicitacao ontologica de um repositorio.
- `OCS` (`Ontological Compatibility Score`) mede a compatibilidade ontologica entre dois repositorios.

O LabGestao tambem deve ser entendido como um ambiente laboratorial de analise, caracterizacao e avaliacao de sistemas. Ele nao e apenas um sistema de gestao de projetos: funciona como contexto analitico estruturado no qual ferramentas, metricas, artefatos e processos tornam explicitos criterios de observacao sobre outros repositorios.

Nesse sentido, `OCI` e `OCS` nao sao metricas isoladas. Elas expressam principios formais de observacao: o `OCI` observa como um sistema explicita entidades, relacoes, validade e identidade; o `OCS` observa como dois sistemas podem ser comparados, alinhados ou integrados a partir desses mesmos criterios. O LabGestao, portanto, opera como um laboratorio virtual de analise ontologica e estrutural de sistemas.

A referencia formal desta abordagem esta registrada em:

- [OCI_OCS_FOUNDATION.tex](./OCI_OCS_FOUNDATION.tex)

## Estrutura minima adotada

Uma ontologia minima responde a quatro perguntas:

1. O que existe no sistema?
2. Como essas coisas se relacionam?
3. O que pode ser considerado valido?
4. Como distinguir e identificar elementos?

No LabGestao, isso aparece como:

- `Entidades`
- `Relacoes`
- `Regras de validade`
- `Regras de identidade`

## OCI no produto

O `OCI` cruza a ontologia declarada em documentos com sinais estruturais do repositorio. DDD, ADR, DAI, CI, policies e evidencias funcionam como fontes de leitura, rastreabilidade ou confianca; eles nao entram automaticamente como entidades do dominio.

Uma entidade operacional deve ter identidade propria, persistencia conceitual, capacidade relacional, validade avaliavel e relevancia de dominio. O inventario separa:

- entidades de dominio;
- entidades tecnicas;
- artefatos de governanca e documentacao;
- eventos ou estados derivados.

O `OCI` e calculado a partir de quatro componentes normalizados no intervalo `0..100`, ajustados pela coerencia entre documento e codigo:

```text
base = (entities + relations + validity + identity) / 4
OCI = base*0.75 + doc_code_alignment*0.25
```

Componentes usados na implementacao atual:

- `ontology_entities_score`
  Caracterizacao de entidades a partir de entidades declaradas, entidades implementadas e coerencia documento/codigo. Nao satura apenas por quantidade de nomes encontrados.
- `ontology_relations_score`
  Caracterizacao de relacoes a partir de relacoes declaradas, relacoes implementadas e coerencia documento/codigo.
- `ontology_validity_score`
  Presenca de invariantes, regras, validacao ou criterios de verdade.
- `ontology_identity_score`
  Presenca de regras de identidade, identificadores canonicos ou referencia explicita de identificacao.
- `ontology_alignment_score`
  Coerencia entre entidades/relacoes declaradas e entidades/relacoes encontradas no codigo e na estrutura do projeto.

Quando ha muitos elementos detectados no codigo, mas pouca declaracao ontologica correspondente, os scores de entidades e relacoes ficam limitados. Isso evita que o OCI seja inflado por contagem de tokens, classes, arquivos ou artefatos instrumentais.

## OCS no produto

O `OCS` compara dois repositorios usando:

- similaridade de entidades
- similaridade de relacoes
- compatibilidade de regras de validade
- compatibilidade de regras de identidade

Como o conjunto de entidades agora filtra artefatos instrumentais, o `OCS` tende a refletir menos coincidencia superficial de documentos e mais compatibilidade ontologica efetiva.

Formula pratica atual:

```text
OCS = entities*0.40 + relations*0.30 + validity*0.15 + identity*0.15
```

Interpretacao usada na UI:

- `>= 70`: integrar
- `>= 40`: alinhar
- `< 40`: manter separado

## Onde aparece na UI

- Aba `Ontologia`
  Resumo do workspace, tabela por repositorio, detalhe do projeto selecionado e grafo ontologico.
- Aba `Inventario`
  `OCI` e `OCS` aparecem de forma indireta no detalhe do repositorio e nas exportacoes.
- Exportacoes especificas
  - `ontology-YYYY-MM-DD.csv`
  - `ontology-YYYY-MM-DD.json`
  - `ontology-report-YYYY-MM-DD.md`

## Limites da versao atual

Esta nao e ainda uma ontologia formal completa.
O que existe hoje e uma leitura heuristica e operacional, suficiente para:

- comparar repositorios
- priorizar alinhamento
- identificar oportunidades de integracao
- tornar criterios menos subjetivos

## Proximo passo esperado

Aplicar a mesma base a projetos do workspace `cpp`, com refinamento progressivo de:

- entidades canonicas por dominio
- relacoes explicitadas por projeto
- regras de validade
- regras de identidade
