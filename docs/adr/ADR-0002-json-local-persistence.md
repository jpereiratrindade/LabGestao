# ADR-0002: Persistencia local em JSON com nlohmann/json

- Status: accepted
- Data: 2026-03-16

## Contexto
Precisamos de persistencia simples, transparente e sem banco dedicado para uso individual/local.

## Decisao
Persistir dados de dominio em arquivo JSON (`data/projects.json`) usando `nlohmann/json` (vendored).

## Consequencias
- Positivas:
  - facil inspecao e backup
  - custo operacional minimo
  - onboarding rapido
- Negativas:
  - concorrencia e transacoes limitadas
  - validacao de schema depende de disciplina de codigo

## Alternativas consideradas
- SQLite embarcado
- Banco remoto (PostgreSQL)
- Serializacao binaria customizada
