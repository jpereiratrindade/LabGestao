# ADR-0001: Aplicacao Desktop com SDL2 + Dear ImGui

- Status: accepted
- Data: 2026-03-16

## Contexto
O produto precisa de uma interface local, rapida e com baixo atrito para evolucao iterativa de features de gestao.

## Decisao
Adotar stack desktop C++ com SDL2 + OpenGL + Dear ImGui.

## Consequencias
- Positivas:
  - alta produtividade para prototipar UX de ferramenta interna
  - binario nativo sem dependencia de navegador
  - baixo overhead arquitetural
- Negativas:
  - maior acoplamento da UI no codigo C++
  - testes de interface menos triviais que em web

## Alternativas consideradas
- App web local (Electron/Tauri)
- Frameworks nativos completos (Qt/wxWidgets)
