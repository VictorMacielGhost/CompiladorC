# Domus Compiler

Transpilador da linguagem Domus para C padrao, desenvolvido em C.

## Compilar

Na raiz do projeto, execute `make` ou use a tarefa **Build Domus Compiler** do VS Code.

## Transpilar

Os caminhos padrao sao relativos ao diretorio de trabalho: ao executar o binario a partir de `builds/`, a entrada e `../tests/new.domus` e a saida e `../tests/build.c`. Os caminhos podem ser alterados:

```text
builds/domusc.exe --in tests/assignment2.domus --out tests/build.c
```

O transpilador imprime a tabela de simbolos e gera C com `<stdio.h>`. Compile o resultado com qualquer compilador C99, por exemplo:

```text
gcc -std=c99 -Wall -Wextra -pedantic tests/build.c -o builds/domus_program.exe
```

Erros lexicos, sintaticos e semanticos incluem a linha de origem. Sensores e atuadores devem ser declarados no inicio do programa; variaveis comuns sao criadas na primeira atribuicao ou leitura. Sensores sao somente leitura, atuadores somente escrita e atribuicoes a atuadores digitais verificam os valores `0` e `1` durante a execucao.

## Testes

Com Python 3 e GCC instalados, execute `make test` para compilar o transpilador com avisos estritos e validar analise semantica, erros, geracao de C e execucao dos programas gerados. O modo detalhado de log pode ser ativado com `--debug`; sem essa opcao, o arquivo `compile.log` contem somente eventos de nivel INFO e superiores.