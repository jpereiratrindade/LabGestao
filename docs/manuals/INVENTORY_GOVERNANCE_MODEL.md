# Manual: Modelo de Governanca do Inventario

## Objetivo

Explicar como a aba `Inventario` do `LabGestao` passou a ler governanca de forma mais forte.

## O que mudou

Antes, a leitura de governanca estava mais concentrada em sinais leves, como:

- `CONTRIBUTING`
- `CODEOWNERS`
- templates de PR/Issue

Esses sinais continuam sendo uteis, mas nao bastam para caracterizar um sistema governado.

Agora, o inventario tambem detecta:

- `ADR`
- `DDD`
- `DAI`
- `policies`
- `tool contracts`
- `approval matrix`
- `audit/evidence`

## Escala de sinais

Os `governanceSignals` passaram de `0..4` para `0..8`.

Exemplos de leitura:

- `0..2`: organizacao inicial ou governanca superficial
- `3..5`: base documental razoavel, mas enforcement parcial
- `6..8`: governanca operacional mais robusta, com contratos, aprovacao e evidencia

## Impacto nos scores

Os novos sinais afetam:

- `scoreMaturity`
- `scoreEngineeringDiscipline`
- `scoreVibeRisk`
- plano de acao por repositorio
- exportacoes `inventory-*.csv/json`

Essa leitura agora tambem aparece no detalhe do projeto, em um painel `Governance Profile`, para aproximar o diagnostico agregado do contexto operacional de cada repositorio.

## Interpretacao recomendada

### Projeto com ADR/DDD/DAI, mas sem contracts/evidence

Leitura:
- existe formalizacao
- ainda falta enforcement e auditabilidade

### Projeto com policies, contracts e evidence

Leitura:
- o repositorio ja consegue operar com responsabilizacao mais explicita
- ha melhores condicoes para revisao, auditoria e uso assistido por IA

## Evolucao arquitetural

Essa leitura deixou de existir apenas como heuristica da UI.

Agora o `LabGestao` possui um `governance_profile` explicito por projeto, persistido junto ao modelo e atualizado pela camada de aplicacao.

Isso permite:

- manter snapshot de maturidade/governanca no proprio projeto
- reaproveitar a analise fora da tela de inventario
- aproximar diagnostico visual, persistencia e servicos de aplicacao

Ainda assim, a heuristica continua dependente da observacao do repositorio local. O passo seguinte natural e aprofundar isso como bounded context proprio de governanca, com regras e workflows ainda mais explicitos.
