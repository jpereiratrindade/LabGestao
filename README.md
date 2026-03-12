# LabGestao

Build nativo para Fedora Silverblue, sem `nix develop`.

## Fluxo recomendado no Silverblue

O caminho mais simples no Silverblue e usar um `toolbox`, para manter as dependencias de desenvolvimento fora da imagem base:

```bash
toolbox create
toolbox enter
sudo dnf install cmake gcc-c++ pkgconf-pkg-config SDL2-devel mesa-libGL-devel curl ninja-build
./build.sh
cd build
./LabGestao
```

## Alternativa com `rpm-ostree`

Se voce quiser instalar direto no host imutavel:

```bash
sudo rpm-ostree install cmake gcc-c++ pkgconf-pkg-config SDL2-devel mesa-libGL-devel curl ninja-build
systemctl reboot
./build.sh
```

## O que o `build.sh` faz

- Baixa Dear ImGui para `vendor/imgui` se os arquivos ainda nao existirem.
- Configura o projeto com CMake em `build/` ou `build-ninja/`, conforme o gerador disponivel.
- Compila o executavel `LabGestao`.

## Dependencias

- `cmake`
- `gcc-c++`
- `pkgconf-pkg-config`
- `SDL2-devel`
- `mesa-libGL-devel`
- `curl`
- `ninja-build`
