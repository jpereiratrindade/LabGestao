# DAI Log (Decision / Action / Impediment)

## Como usar
Registrar entradas curtas para manter rastreabilidade de execucao.

Formato recomendado:
- `Decision`: escolha tecnica/negocial com racional
- `Action`: proxima acao concreta com dono e prazo
- `Impediment`: bloqueio atual e estrategia de remocao

## Entradas

### 2026-04-24
- Decision: tratar o LabGestao como ambiente laboratorial de analise, caracterizacao e avaliacao de sistemas, e nao apenas como sistema de gestao ou painel de metricas.
- Decision: interpretar `OCI` e `OCS` como expressoes formais de observacao ontologica e estrutural, vinculadas ao papel analitico do LabGestao.
- Action: registrar a decisao em `ADR-0008` e refletir a formulacao em `DDD.md`, `METRICS_REFERENCE.md` e nos documentos de fundamentacao ontologica.
- Action: incorporar `docs/labgestao_doc_base.md` como documento-base conceitual, mantendo ADR, DDD, manual de metricas e fundamentacao OCI/OCS como especializacoes operacionais.
- Action: usar o IdeaWalker como teste aplicado das recomendacoes geradas pelo LabGestao, mantendo OCS como frente de alinhamento conceitual entre repositorios.

### 2026-03-24
- Decision: tratar governanca assistida por IA como capacidade operacional baseada em evidencia, e nao apenas como presenca de artefatos.
- Decision: ampliar a leitura do inventario para distinguir governanca superficial de governanca normativa/operacional.
- Action: reforcar o bootstrap `C++ Governado` com `context_boundary_policy`, `evidence_and_audit_policy`, `governance_task_packet` e `evidence_log`.
- Action: incluir novo contrato `review_change.contract.json` para revisao independente orientada a politica e risco.
- Action: ajustar exportacoes e plano de acao do inventario para considerar `policies`, `tool contracts`, `approval matrix` e `audit/evidence`.
- Action: atualizar README, DDD, ADRs e manuais para refletir a nova leitura de governanca forte.

### 2026-03-18
- Decision: consolidar avaliacao documental/codigo para alinhar plano com estado real do projeto.
- Decision: registrar formalmente a necessidade de extrair `Application Services` para eliminar duplicacao de orquestracao.
- Action: adicionar ADR-0005 propondo camada de aplicacao para fluxos de sincronizacao/reclassificacao.
- Action: atualizar `DDD.md` e `README.md` para refletir funcionalidades ja implementadas (testes de dominio, metricas de fluxo e gestao de DAI/ADR na UI).
- Action: registrar no DAI que o impedimento "sem testes automatizados" de 2026-03-16 foi parcialmente mitigado com suite de dominio/persistencia.

### 2026-03-16
- Decision: formalizar arquitetura com DDD e ADR para reduzir conhecimento tacito no repositorio.
- Action: criar base documental em `docs/architecture`, `docs/adr` e `docs/dai`.
- Action: incluir pipeline de CI para build automatico em push e pull request.
- Impediment: nao existem testes automatizados ainda.
- Action: abrir frente de trabalho para testes de dominio e integracao de persistencia.
- Decision: workspace passa a ser fonte de verdade para persistencia (`data_dir`) e roots monitoradas.
- Action: adicionar menu `Tools -> Selecionar Workspace...` com seletor interno e dialogo de sistema.
- Action: remover criacao forcada de `data/` no diretorio do binario (CMake pos-build).
- Decision: aplicar reclassificacao automatica do Kanban para projetos auto-descobertos.
- Action: executar reclassificacao automatica no startup e disponibilizar gatilho manual em `Tools`.
- Action: habilitar ordenacao por clique em colunas da tabela de projetos (Nome, Categoria, Criado em).

## Template

Data: YYYY-MM-DD
- Decision:
- Action:
- Impediment:
