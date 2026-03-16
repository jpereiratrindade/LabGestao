# ADR-0003: Workspace como fonte de verdade para data_dir e roots monitoradas

- Status: accepted
- Data: 2026-03-16

## Contexto
Havia ambiguidade sobre onde salvar dados (`data/`) e quais pastas monitorar.
Em execucoes diferentes, o app podia usar diretorios distintos conforme pasta corrente.

## Decisao
Adotar `workspace_root` como referencia principal de operacao:

- `workspace_root` definido no menu `Tools -> Selecionar Workspace...`
- `data_dir` passa a ser `<workspace_root>/data`
- `monitored_roots` passa a conter a raiz escolhida
- configuracao persistida em `~/.config/labgestao/settings.json`

## Consequencias
- Positivas:
  - comportamento previsivel entre execucoes
  - isolamento de dados por workspace
  - menor risco de gravar `data/` na pasta errada
- Negativas:
  - exige escolha explicita de workspace para melhor experiencia
  - precisa migracao de setups legados (tratada automaticamente)

## Alternativas consideradas
- Manter `./data` relativo ao diretorio de execucao
- Forcar um unico `data_dir` global fixo sem conceito de workspace
