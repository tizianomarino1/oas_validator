# oas_validator

Validatore OpenAPI 3.0

## Compilazione

### Windows (MSVC)
1. Apri il **Developer Command Prompt for Visual Studio** (la shell con gli strumenti di compilazione `cl.exe`).
2. Spostati nella cartella del progetto:
   ```cmd
   cd \percorso\alla\cartella\oas_validator
   ```
3. Lancia lo script incluso per compilare tutte le sorgenti con MSVC:
   ```cmd
   compile.bat
   ```
   Verrà generato l'eseguibile `oas_validator.exe`.

> In alternativa puoi lanciare direttamente il comando usato dallo script:
> ```cmd
> cl /W4 /O2 /std:c11 main.c fileutil.c oas_extract.c jsonschema.c external\cJSON.c /Fe:oas_validator.exe
> ```

### Linux (GCC o Clang)
1. Assicurati di avere installato un compilatore C11 (ad esempio `gcc` o `clang`) e gli strumenti di base per la build (`build-essential` su Debian/Ubuntu).
2. Dalla cartella del progetto esegui:
   ```bash
   gcc -std=c11 -Wall -Wextra -O2 main.c fileutil.c oas_extract.c jsonschema.c external/cJSON.c -o oas_validator
   ```
   (Sostituisci `gcc` con `clang` se preferisci.)

L'eseguibile risultante (`oas_validator.exe` su Windows oppure `oas_validator` su Linux) accetta due argomenti:

```bash
./oas_validator richiesta.json openapi.json
```

Il programma stampa `VALIDO ✅` quando il JSON fornito rispetta lo schema individuato nella specifica OpenAPI 3.x, altrimenti indica l'errore.
