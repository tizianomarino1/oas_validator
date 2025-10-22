# oas_validator

Validatore OpenAPI 3.0

## Compilazione

### Windows (MSVC)
1. Apri il **Developer Command Prompt for Visual Studio** (la shell con gli strumenti di compilazione `cl.exe`).
2. Spostati nella cartella del progetto:
   ```cmd
   cd \percorso\alla\cartella\oas_validator
   ```
3. Lancia il compilatore **C**:
> ```cmd
> cl /W4 /O2 /std:c11 /Iinclude /Iexternal src\main.c src\fileutil.c src\oas_extract.c src\jsonschema.c external\cJSON.c /Fe:oas_validator.exe
> ```

### Linux (GCC o Clang)
1. Assicurati di avere installato un compilatore C11 (ad esempio `gcc` o `clang`) e gli strumenti di base per la build (`build-essential` su Debian/Ubuntu).
2. Dalla cartella del progetto esegui:
   ```bash
   gcc -std=c11 -Wall -Wextra -O2 -Iinclude -Iexternal src/*.c external/cJSON.c -o oas_validator
   ```
   (Sostituisci `gcc` con `clang` se preferisci.)

L'eseguibile risultante (`oas_validator.exe` su Windows oppure `oas_validator` su Linux) accetta due argomenti:

```bash
./oas_validator richiesta.json openapi.json
```

Il programma stampa `VALIDO ✅` quando il JSON fornito rispetta lo schema individuato nella specifica OpenAPI 3.x, altrimenti indica l'errore.
