# Manual: C++ Governado

## Objetivo

Descrever como o template `C++ Governado` do `LabGestao` deve ser usado quando o projeto exige mais do que produtividade de desenvolvimento.

Esse template existe para cenarios em que:

- erro nao e apenas bug tecnico
- contexto institucional importa
- auditoria e justificativa fazem parte da entrega

## O que o template gera

Ao selecionar `C++ Governado`, o `LabGestao` executa o script `init_ai_governance.sh`.

O scaffold criado inclui:

- base C++ com `CMake`, testes e quality gates
- `adr/` e `ddd/`
- `policies/`
- `mcp/tool_schema.json` e `mcp/contracts/`
- `prompts/` com `task_template` e `governance_task_packet`
- `examples/evidence_log.json`
- validadores:
  - `scripts/validate_tool_contracts.py`
  - `scripts/validate_governance_repo.py`

## Modo `GSDD`

No `LabGestao`, o template `C++ Governado` agora pode ser criado em dois modos:

- `Governanca base`
- `Governanca + GSDD`

Quando `Governanca + GSDD` e selecionado, o bootstrap canônico adiciona artefatos extras de especificacao governada:

- `examples/spec_packet.md`
- `examples/execution_plan.md`
- `examples/feedback_log.md`

E amplia os prompts:

- `prompts/task_template.md`
- `prompts/governance_task_packet.md`

Esse modo existe para quando o projeto precisa de uma camada explicita entre intencao e execucao, sem transformar a IA em decisora arquitetural.

## Artefatos principais

### Politicas

- `policies/context_boundary_policy.md`
- `policies/evidence_and_audit_policy.md`
- `policies/approval_matrix.md`

Esses arquivos explicitam:

- quais contextos sao criticos
- quando aprovacao humana e exigida
- quais evidencias minimas devem existir

### Contratos

- `mcp/contracts/modify_code.contract.json`
- `mcp/contracts/review_change.contract.json`

Esses contratos definem:

- modos permitidos
- nivel de risco
- precondicoes
- evidencias obrigatorias
- acoes proibidas

### Evidencias

- `prompts/governance_task_packet.md`
- `examples/evidence_log.json`

Esses artefatos ajudam a registrar:

- owner humano
- contexto afetado
- referencias normativas
- aprovacao
- status final

## Workflow recomendado

1. Criar o projeto com `C++ Governado`.
2. Revisar os contratos e politicas antes do primeiro uso de IA.
3. Preencher um `Governance Task Packet` para mudancas relevantes.
4. Executar a mudanca apenas no modo operacional adequado.
5. Registrar evidencias antes da promocao.
6. Rodar:
   - `python3 scripts/validate_tool_contracts.py`
   - `python3 scripts/validate_governance_repo.py`
   - `./scripts/run_quality.sh`

Quando estiver em modo `GSDD`, recomenda-se tambem iniciar o trabalho por:

1. `examples/spec_packet.md`
2. `examples/execution_plan.md`
3. `examples/feedback_log.md`

## Quando usar

Use `C++ Governado` quando o repositorio precisar de:

- rastreabilidade
- revisao proporcional ao risco
- limites claros para agentes
- enforcement minimo via contratos/validadores

Use `Governanca + GSDD` quando, alem disso, o projeto precisar de:

- transformar intencao em especificacao executavel antes de implementar
- distinguir zona formal e zona adaptativa
- manter um ciclo explicito entre especificacao, execucao e feedback

Para prototipos simples, os templates `C++` ou `Python` podem ser suficientes.
