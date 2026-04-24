# ADR-0008: LabGestao como ambiente laboratorial de analise estrutural

## Status
accepted

## Contexto

O LabGestao deixou de atuar apenas como sistema de gestao de projetos e passou a observar repositorios como objetos de analise. O inventario, os perfis de governanca, o `OntologyProfile`, as metricas `OCI` e `OCS`, os relatorios e os planos de acao formam um conjunto de instrumentos para caracterizar outros sistemas.

Essa mudanca exige uma definicao mais precisa do papel do proprio LabGestao. Chama-lo apenas de ferramenta de gestao ou painel de metricas reduz a natureza da solucao: o sistema tambem explicita criterios de observacao, compara estruturas, avalia maturidade, identifica compatibilidade e recomenda acoes.

## Decisao

Tratar o LabGestao como um ambiente laboratorial de analise, caracterizacao e avaliacao de sistemas.

Esse laboratorio nao deve ser entendido como laboratorio experimental classico, mas como contexto analitico estruturado no qual praticas, processos, ferramentas, documentos e metricas operam em conjunto para:

- observar repositorios;
- descrever e caracterizar estruturas;
- comparar perfis;
- avaliar clareza, governanca, confiabilidade e compatibilidade;
- recomendar alinhamento, integracao ou separacao.

Nesse modelo, `OCI` e `OCS` nao sao metricas isoladas. Elas expressam principios formais de observacao ontologica e estrutural:

- `OCI` observa como um sistema explicita entidades, relacoes, validade e identidade;
- `OCS` observa como dois sistemas podem ser comparados, alinhados ou integrados a partir desses mesmos criterios.

## Consequencias

- A documentacao deve apresentar o LabGestao como laboratorio virtual de analise ontologica e estrutural de sistemas.
- O DDD deve reconhecer a capacidade analitica como parte do dominio, nao como detalhe de UI.
- A referencia de metricas deve explicar o papel das metricas dentro desse contexto laboratorial.
- Recomendacoes geradas pelo inventario devem ser tratadas como resultado analitico rastreavel, nao apenas como checklist.
- Evolucoes futuras de `OntologyProfile`, `GovernanceProfile`, relatorios e planos de acao devem preservar a distincao entre evidencia, entidade ontologica, mecanismo tecnico e recomendacao.

## Alternativas consideradas

- Manter o LabGestao descrito apenas como gestor de projetos.
  - Rejeitada porque nao representa o papel atual do inventario, da ontologia e das recomendacoes.
- Chamar genericamente de laboratorio.
  - Rejeitada por ser amplo demais e potencialmente metaforico.
- Adotar a formulacao "ambiente laboratorial de analise, caracterizacao e avaliacao de sistemas".
  - Aceita por ser mais precisa e defensavel.
