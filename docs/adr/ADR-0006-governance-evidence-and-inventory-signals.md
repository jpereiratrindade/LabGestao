# ADR-0006: Governanca orientada a evidencia no scaffold e no inventario

- Status: accepted
- Data: 2026-03-24

## Contexto

O projeto ja possuia DDD, ADR, DAI e sinais gerais de governanca tecnica, mas a leitura do sistema ainda estava mais proxima de "presenca de artefatos" do que de governanca operacional.

Isso criava um risco importante: tratar repositorios institucionais como se bastasse adicionar documentos e templates para que houvesse responsabilizacao real.

Ao mesmo tempo, o template `C++ Governado` do `LabGestao` precisava refletir melhor a diferenca entre:

- automacao para produtividade
- sistema governado, com fronteiras, aprovacao e rastreabilidade

## Decisao

Reforcar o bootstrap governado e a leitura do inventario com foco em accountability e auditabilidade:

- o scaffold `C++ Governado` passa a incluir politicas de fronteira de contexto e auditoria
- contratos de ferramenta passam a exigir evidencias minimas proporcionais ao risco
- o bootstrap passa a incluir `Governance Task Packet` e `evidence_log.json`
- o inventario do `LabGestao` passa a detectar tambem:
  - `policies`
  - `tool contracts`
  - `approval matrix`
  - `audit/evidence`

## Consequencias

- Positivas:
  - melhora a distincao entre governanca cosmetica e governanca operacional
  - reduz risco de “vibe coding institucional”
  - torna o inventario mais aderente a contexto critico e responsabilidade formal
  - orienta o usuario para workflows com evidencia e aprovacao explicitas
- Negativas:
  - aumenta o volume inicial de documentacao e artefatos no scaffold
  - exige manutencao mais rigorosa dos documentos normativos
  - pode reduzir velocidade em cenarios informais de prototipacao

## Alternativas consideradas

- Manter inventario e scaffold focados apenas em ADR/DDD/DAI e sinais basicos de governanca.
- Tratar governanca apenas como manual externo, sem enforcement no bootstrap e sem reflexo no inventario.
