# Sobrevivendo Em GS (PSP / PPSSPP)

Jogo de sobrevivencia zumbi em primeira pessoa, 100% C com PSPSDK (sceGu).

## Como compilar
1. Suba esta pasta inteira para um repositorio no GitHub (mantenha `.github/workflows/build.yml`).
2. Aba **Actions** > rode o workflow (ou faca um push). Baixe o artefato `SobrevivendoEmGS`:
   `EBOOT.PBP` (jeito mais seguro no PPSSPP) e `SobrevivendoEmGS.iso`.
3. Localmente, com o pspdev instalado: `make`.

## Controles
| Acao | Botao |
|---|---|
| Andar / (no carro) acelerar e esterçar | Analogico |
| Olhar | D-Pad (esq/dir gira, cima/baixo inclina) |
| Atirar / Mirar (zoom) | R / L |
| Recarregar (no carro: freio de mao) | Quadrado |
| Trocar arma / Correr | Circulo / X |
| Recrutar, entrar e sair do carro | Triangulo |
| Kit medico / Pausa | Select / Start |

## Arquivos
- `main.c` – todo o jogo (3D, mira, IA, sobreviventes, carros, loja, rede preparada)
- `cars_data.h` – 20 sprites de carros (128x64, 5551) gerados de `assets/carros.png`
- `tools/gerar_carros.py` – regenera `cars_data.h` e `ICON0.PNG` (Python + Pillow + numpy + scipy)
- `Makefile`, `.github/workflows/build.yml` – build (EBOOT.PBP + ISO)

## Multiplayer (preparado)
`main.c` tem pacotes binarios (`NetPacket`, 20 Hz), jogadores remotos renderizados e um modo
**loopback** (Multiplayer > Quadrado) que simula um jogador-fantasma. Falta so implementar o
transporte Ad-Hoc real em `net_tick` (`net.mode == 2`) com `pspnet_adhoc`.

## Loja
Armas, itens e todos os 20 carros, pagos com o dinheiro ganho matando zumbis. O save fica em
`ms0:/PSP/GAME/SobrevivendoEmGS/save.bin`.
